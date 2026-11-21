// app_server: the phone's window into the hat. Lowest priority process; brim never waits on it.
//
//   libwebsockets (one service thread):
//     /api/*   REST (api.cpp)            — LWSMPRO_CALLBACK mount -> protocol "http"
//     /ws      WebSocket "brim-events"   — live events pushed to paired phones
//     /*       static PWA from --static  — LWSMPRO_FILE mount (app/dist)
//   relay thread: bus -> validate -> SQLite -> broadcast queue -> lws_cancel_service() wakes lws.
//
// usage: app_server [--port 8080] [--db data/brim.db] [--data-dir data] [--migrations data/migrations]
//                   [--static app/dist] [--seed-demo] [--pin 123456 (dev only)]
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libwebsockets.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/stat.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "app_server/api.hpp"
#include "app_server/db.hpp"
#include "app_server/pairing.hpp"
#include "app_server/relay.hpp"
#include "common/config.hpp"
#include "common/events.hpp"
#include "common/log.hpp"
#include "dashboard/dashboard.hpp"
#include "ipc/bus.hpp"

#if !defined(LWS_WITH_HTTP_UNCOMMON_HEADERS) && !defined(LWS_HTTP_HEADERS_ALL)
// TODO(qnx-check): if the oss.qnx.com libwebsockets 4.5.8 build lacks PUT/PATCH/DELETE tokens,
// those requests arrive as unknown methods. The server still accepts POST with an
// "X-HTTP-Method-Override: PATCH|PUT|DELETE" header; set USE_METHOD_OVERRIDE=true in
// app/src/api/client.ts for that build (or rebuild lws with -DLWS_WITH_HTTP_UNCOMMON_HEADERS=ON).
#warning "libwebsockets built without LWS_WITH_HTTP_UNCOMMON_HEADERS: use X-HTTP-Method-Override"
#define BRIM_LWS_NO_UNCOMMON_METHODS 1
#endif

using namespace brim;

namespace {

std::atomic<bool> g_stop{false};
lws_context* g_ctx = nullptr;
app::Api* g_api = nullptr;
app::ServerState* g_state = nullptr;

constexpr std::size_t kMaxBody = 64 * 1024;

// ------------------------------------------------------------------ broadcast plumbing
// Relay thread -> pending (mutex) -> lws thread moves into ring -> each WS session sends from ring.
std::mutex g_pending_mu;
std::deque<std::string> g_pending;
struct RingMsg {
  std::uint64_t seq;
  std::string json;
};
std::deque<RingMsg> g_ring;  // lws thread only
std::uint64_t g_ring_next_seq = 1;
constexpr std::size_t kRingSize = 256;

void broadcast_from_any_thread(std::string json) {
  {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    g_pending.push_back(std::move(json));
    if (g_pending.size() > kRingSize) g_pending.pop_front();
  }
  if (g_ctx) lws_cancel_service(g_ctx);  // thread-safe wakeup
}

// ------------------------------------------------------------------ HTTP (REST)
struct HttpPending {
  app::HttpRequest req;
  app::HttpResponse resp;
  int stage = 0;  // 0 idle/reading body, 1 headers to write, 2 body to write
  bool too_large = false;
};
struct HttpSession {  // lws zero-allocates this POD; we own the pointer
  HttpPending* p;
};

std::string hdr(lws* wsi, lws_token_indexes tok) {
  int n = lws_hdr_total_length(wsi, tok);
  if (n <= 0) return {};
  std::string s(static_cast<std::size_t>(n) + 1, '\0');
  lws_hdr_copy(wsi, &s[0], n + 1, tok);
  s.resize(static_cast<std::size_t>(n));
  return s;
}

std::string method_and_path(lws* wsi, std::string& path) {
  struct M {
    lws_token_indexes tok;
    const char* name;
  };
  static const M methods[] = {
      {WSI_TOKEN_GET_URI, "GET"},
      {WSI_TOKEN_POST_URI, "POST"},
#if !defined(BRIM_LWS_NO_UNCOMMON_METHODS)
      {WSI_TOKEN_PUT_URI, "PUT"},
      {WSI_TOKEN_PATCH_URI, "PATCH"},
      {WSI_TOKEN_DELETE_URI, "DELETE"},
#endif
      {WSI_TOKEN_OPTIONS_URI, "OPTIONS"},
  };
  for (const auto& m : methods) {
    path = hdr(wsi, m.tok);
    if (!path.empty()) return m.name;
  }
  return {};
}

std::string query_string(lws* wsi) {
  std::string q;
  char buf[256];
  for (int i = 0; lws_hdr_copy_fragment(wsi, buf, sizeof buf, WSI_TOKEN_HTTP_URI_ARGS, i) > 0; ++i) {
    if (!q.empty()) q += '&';
    q += buf;
  }
  return q;
}

void dispatch(lws* wsi, HttpPending* p) {
  if (p->too_large) {
    p->resp = app::HttpResponse{413, "application/json", R"({"error":"body too large"})", {}};
  } else {
    p->resp = g_api->handle(p->req);
  }
  p->stage = 1;
  lws_callback_on_writable(wsi);
}

int write_headers(lws* wsi, HttpPending* p) {
  unsigned char buf[LWS_PRE + 2048];
  unsigned char* start = buf + LWS_PRE;
  unsigned char* ptr = start;
  unsigned char* end = buf + sizeof buf - 1;
  if (lws_add_http_common_headers(wsi, static_cast<unsigned>(p->resp.status), p->resp.content_type.c_str(),
                                  p->resp.body.size(), &ptr, end))
    return 1;
  auto add = [&](const std::string& name, const std::string& value) {
    return lws_add_http_header_by_name(wsi, reinterpret_cast<const unsigned char*>(name.c_str()),
                                       reinterpret_cast<const unsigned char*>(value.c_str()), static_cast<int>(value.size()),
                                       &ptr, end);
  };
  if (add("cache-control:", "no-store")) return 1;
  for (const auto& h : p->resp.headers)
    if (add(h.first, h.second)) return 1;
  if (lws_finalize_write_http_header(wsi, start, &ptr, end)) return 1;
  return 0;
}

int callback_http(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len) {
  auto* sess = static_cast<HttpSession*>(user);
  switch (reason) {
    case LWS_CALLBACK_HTTP: {
      if (!sess) break;
      delete sess->p;
      sess->p = new HttpPending();
      HttpPending* p = sess->p;
      p->req.method = method_and_path(wsi, p->req.path);
      p->req.query = query_string(wsi);
      p->req.cookie_header = hdr(wsi, WSI_TOKEN_HTTP_COOKIE);
      if (p->req.method == "POST") {
        // Method override for lws builds without PUT/PATCH/DELETE tokens (see top of file).
        char ov[16] = {0};
        if (lws_hdr_custom_copy(wsi, ov, sizeof ov, "x-http-method-override:", 23) > 0) {
          std::string m(ov);
          if (m == "PUT" || m == "PATCH" || m == "DELETE") p->req.method = m;
        }
      }
      if (lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH) > 0) return 0;  // wait for body
      dispatch(wsi, p);
      return 0;
    }
    case LWS_CALLBACK_HTTP_BODY:
      if (sess && sess->p) {
        if (sess->p->req.body.size() + len > kMaxBody) sess->p->too_large = true;
        else sess->p->req.body.append(static_cast<const char*>(in), len);
      }
      return 0;
    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
      if (sess && sess->p) dispatch(wsi, sess->p);
      return 0;
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      if (!sess || !sess->p || sess->p->stage == 0) break;
      HttpPending* p = sess->p;
      if (p->stage == 1) {
        if (write_headers(wsi, p)) return -1;
        if (p->resp.body.empty()) {
          p->stage = 0;
          if (lws_http_transaction_completed(wsi)) return -1;
          return 0;
        }
        p->stage = 2;
        lws_callback_on_writable(wsi);
        return 0;
      }
      std::vector<unsigned char> b(LWS_PRE + p->resp.body.size());
      std::memcpy(b.data() + LWS_PRE, p->resp.body.data(), p->resp.body.size());
      if (lws_write(wsi, b.data() + LWS_PRE, p->resp.body.size(), LWS_WRITE_HTTP_FINAL) < 0) return -1;
      p->stage = 0;
      if (lws_http_transaction_completed(wsi)) return -1;
      return 0;
    }
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
      if (sess) {
        delete sess->p;
        sess->p = nullptr;
      }
      break;
    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

// ------------------------------------------------------------------ WebSocket
struct WsSession {
  std::uint64_t next_seq;
  bool established;  // CLOSED also fires for connections we rejected in FILTER
};

void drain_pending_into_ring() {
  std::deque<std::string> batch;
  {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    batch.swap(g_pending);
  }
  for (auto& j : batch) {
    g_ring.push_back(RingMsg{g_ring_next_seq++, std::move(j)});
    if (g_ring.size() > kRingSize) g_ring.pop_front();
  }
}

int callback_ws(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len) {
  auto* ws = static_cast<WsSession*>(user);
  switch (reason) {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
      // Same auth as REST: the HttpOnly session cookie set by POST /api/pair.
      if (!g_api->authorized(hdr(wsi, WSI_TOKEN_HTTP_COOKIE))) {
        BRIM_LOGW("ws", "rejecting unpaired WebSocket");
        return 1;
      }
      return 0;
    case LWS_CALLBACK_ESTABLISHED:
      ws->next_seq = g_ring_next_seq;  // live events only; history comes from REST
      ws->established = true;
      g_state->ws_clients++;
      break;
    case LWS_CALLBACK_CLOSED:
      if (ws && ws->established) {
        ws->established = false;
        g_state->ws_clients--;
      }
      break;
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
      drain_pending_into_ring();
      const lws_protocols* proto = lws_get_protocol(wsi);
      if (proto) lws_callback_on_writable_all_protocol(lws_get_context(wsi), proto);
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      if (g_ring.empty() || ws->next_seq >= g_ring_next_seq) break;
      if (ws->next_seq < g_ring.front().seq) ws->next_seq = g_ring.front().seq;  // slow client: skip
      const RingMsg& m = g_ring[static_cast<std::size_t>(ws->next_seq - g_ring.front().seq)];
      std::vector<unsigned char> b(LWS_PRE + m.json.size());
      std::memcpy(b.data() + LWS_PRE, m.json.data(), m.json.size());
      if (lws_write(wsi, b.data() + LWS_PRE, m.json.size(), LWS_WRITE_TEXT) < static_cast<int>(m.json.size())) return -1;
      ws->next_seq++;
      if (ws->next_seq < g_ring_next_seq) lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_RECEIVE:
      break;  // phone -> hat messages not used yet (REST covers commands)
    default:
      break;
  }
  return 0;
}

// Built field-by-field: lws_protocols gained fields between 4.x and 5.x.
lws_protocols make_protocol(const char* name, lws_callback_function* cb, size_t pss, size_t rx) {
  lws_protocols p;
  std::memset(&p, 0, sizeof p);
  p.name = name;
  p.callback = cb;
  p.per_session_data_size = pss;
  p.rx_buffer_size = rx;
  return p;
}
lws_protocols kProtocols[3];  // filled in main(); last entry stays zeroed (list terminator)

void on_signal(int) {
  g_stop = true;
  if (g_ctx) lws_cancel_service(g_ctx);
}

std::string lan_url(int port) {
  // getifaddrs: BSD/POSIX-ish, present in QNX libsocket. TODO(qnx-check) on the Pi.
  std::string ip = "<this-device-ip>";
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) == 0) {
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
      if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
      char buf[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof buf);
      if (std::strncmp(buf, "127.", 4) != 0) {
        ip = buf;
        break;
      }
    }
    freeifaddrs(ifs);
  }
  return "http://" + ip + ":" + std::to_string(port);
}

}  // namespace

int main(int argc, char** argv) {
  log::set_process_name("app_server");
  auto app_cfg = config::load_app(config::config_dir() + "/app.json");
  int port = app_cfg.http_port;
  std::string data_dir = app_cfg.data_dir;
  std::string db_path, migrations = "data/migrations", static_dir = "app/dist", pin;
  bool seed = false;
  for (int i = 1; i < argc; ++i) {
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", argv[i]);
        std::exit(2);
      }
      return argv[++i];
    };
    std::string a = argv[i];
    if (a == "--port") port = std::atoi(next().c_str());
    else if (a == "--db") db_path = next();
    else if (a == "--data-dir") data_dir = next();
    else if (a == "--migrations") migrations = next();
    else if (a == "--static") static_dir = next();
    else if (a == "--seed-demo") seed = true;
    else if (a == "--pin") pin = next();
    else {
      std::fprintf(stderr,
                   "usage: app_server [--port N] [--db PATH] [--data-dir DIR] [--migrations DIR] [--static DIR] "
                   "[--seed-demo] [--pin 123456]\n");
      return 2;
    }
  }
  if (db_path.empty()) db_path = data_dir + "/brim.db";
  ::mkdir(data_dir.c_str(), 0700);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  std::string err;
  auto db = app::Db::open(db_path, migrations, &err);
  if (!db) {
    BRIM_LOGE("app_server", "database: %s", err.c_str());
    return 1;
  }
  if (seed) db->seed_demo(data_dir);

  if (pin.empty()) pin = app::random_pin();
  app::Pairing pairing(pin);
  app::ServerState state;
  ipc::EventBus cmd_bus(events::kBrimCommandChannel, 32);
  app::Api api(*db, pairing, state, data_dir, [&cmd_bus](const schema::Command& c) {
    cmd_bus.publish(schema::to_json(c).dump());
  });
  g_api = &api;
  g_state = &state;

  // ---- lws setup
  char static_abs[PATH_MAX];
  if (!realpath(static_dir.c_str(), static_abs)) {
    BRIM_LOGW("app_server", "static dir %s not found (build the PWA: cd app && npm run build). API still works.",
              static_dir.c_str());
    std::snprintf(static_abs, sizeof static_abs, "%s", static_dir.c_str());
  }
  static lws_protocol_vhost_options mime_webmanifest = {nullptr, nullptr, ".webmanifest", "application/manifest+json"};
  static lws_protocol_vhost_options mime_json = {&mime_webmanifest, nullptr, ".json", "application/json"};

  lws_http_mount mount_static;
  std::memset(&mount_static, 0, sizeof mount_static);
  mount_static.mountpoint = "/";
  mount_static.mountpoint_len = 1;
  mount_static.origin = static_abs;
  mount_static.def = "index.html";
  mount_static.origin_protocol = LWSMPRO_FILE;
  mount_static.extra_mimetypes = &mime_json;

  lws_http_mount mount_api;
  std::memset(&mount_api, 0, sizeof mount_api);
  mount_api.mount_next = &mount_static;
  mount_api.mountpoint = "/api";
  mount_api.mountpoint_len = 4;
  mount_api.origin = "http";  // protocol name that handles it
  mount_api.origin_protocol = LWSMPRO_CALLBACK;

  kProtocols[0] = make_protocol("http", callback_http, sizeof(HttpSession), 0);
  kProtocols[1] = make_protocol("brim-events", callback_ws, sizeof(WsSession), 4096);
  std::memset(&kProtocols[2], 0, sizeof kProtocols[2]);

  lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
  lws_context_creation_info info;
  std::memset(&info, 0, sizeof info);
  info.port = port;
  info.protocols = kProtocols;
  info.mounts = &mount_api;
  info.vhost_name = "brim";
  info.options = LWS_SERVER_OPTION_DISABLE_IPV6;  // TODO(qnx-check): enable if the Pi image has IPv6
  g_ctx = lws_create_context(&info);
  if (!g_ctx) {
    BRIM_LOGE("app_server", "lws_create_context failed (port %d in use?)", port);
    return 1;
  }

  const std::string run_dir = std::getenv("BRIM_RUN_DIR") ? std::getenv("BRIM_RUN_DIR") : "/tmp/brim";
  ::mkdir(run_dir.c_str(), 0700);
  auto dash = dashboard::make_log_dashboard(run_dir);
  dash->show_pairing(pin, lan_url(port));
  BRIM_LOGI("app_server", "listening on :%d (lws %s), static=%s, db=%s", port, lws_get_library_version(), static_abs,
            db_path.c_str());

  std::thread relay([&] { app::run_relay(*db, state, broadcast_from_any_thread, g_stop); });

  while (!g_stop) {
    if (lws_service(g_ctx, 0) < 0) break;
  }
  g_stop = true;
  relay.join();
  lws_context_destroy(g_ctx);
  g_ctx = nullptr;
  BRIM_LOGI("app_server", "stopped");
  return 0;
}
