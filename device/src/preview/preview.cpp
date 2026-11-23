#include "preview/preview.hpp"

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "common/log.hpp"
#include "common/timing.hpp"

namespace brim::preview {
namespace {

constexpr std::int64_t kMinGapMs = 100;  // ~10 frames a second is plenty for a preview
constexpr int kHeaderSize = 54;

void put32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, 4); }  // BMP is little-endian, as is the Pi

const char* kPage =
    "<!doctype html><meta name=viewport content='width=device-width'><title>Brim camera (preview)</title>"
    "<body style='margin:0;background:#111;color:#eee;font:16px system-ui;text-align:center'>"
    "<p>Brim camera &ndash; live preview from brim (not recorded)</p>"
    "<img id=i alt='Live camera view' style='width:100%;max-width:640px'>"
    "<script>const i=document.getElementById('i');function n(){i.src='/frame.bmp?'+Date.now()}"
    "i.onload=()=>setTimeout(n,80);i.onerror=()=>setTimeout(n,500);n();</script>";

void send_all(int fd, const char* p, std::size_t n) {
  while (n > 0) {
    const ssize_t w = ::write(fd, p, n);
    if (w <= 0) return;
    p += w;
    n -= static_cast<std::size_t>(w);
  }
}

}  // namespace

void encode_bmp(const capture::Frame& f, std::vector<std::uint8_t>& out) {
  constexpr int W = Preview::kWidth, H = Preview::kHeight;
  constexpr std::uint32_t data = W * H * 3;
  out.assign(kHeaderSize + data, 0);
  std::uint8_t* p = out.data();
  p[0] = 'B';
  p[1] = 'M';
  put32(p + 2, kHeaderSize + data);
  p[10] = kHeaderSize;
  p[14] = 40;  // BITMAPINFOHEADER
  put32(p + 18, W);
  put32(p + 22, H);
  p[26] = 1;   // planes
  p[28] = 24;  // bits per pixel
  put32(p + 34, data);
  if (f.width <= 0 || f.height <= 0 || f.rgb.size() < static_cast<std::size_t>(f.width) * f.height * 3) return;
  for (int oy = 0; oy < H; ++oy) {
    const std::uint8_t* row = f.rgb.data() + static_cast<std::size_t>(oy * f.height / H) * f.width * 3;
    std::uint8_t* o = p + kHeaderSize + static_cast<std::size_t>(H - 1 - oy) * W * 3;  // bottom-up
    for (int ox = 0; ox < W; ++ox, o += 3) {
      const std::uint8_t* s = row + static_cast<std::size_t>(ox * f.width / W) * 3;
      o[0] = s[2];  // B
      o[1] = s[1];  // G
      o[2] = s[0];  // R
    }
  }
}

bool Preview::start(int port) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    BRIM_LOGE("preview", "socket failed: %s", std::strerror(errno));
    return false;
  }
  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  a.sin_addr.s_addr = INADDR_ANY;
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(listen_fd_, 4) != 0) {
    BRIM_LOGE("preview", "can't listen on port %d: %s (is camview still running?)", port, std::strerror(errno));
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_ = true;
  thread_ = std::thread(&Preview::serve, this);
  BRIM_LOGI("preview", "camera preview on http://<pi>:%d/ (demo only, nothing recorded)", port);
  return true;
}

void Preview::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  if (listen_fd_ >= 0) ::close(listen_fd_);
  listen_fd_ = -1;
}

void Preview::offer(const capture::Frame& f) {
  if (!running_) return;
  const std::int64_t now = timing::mono_ms();
  if (now - last_offer_ms_ < kMinGapMs) return;
  last_offer_ms_ = now;
  encode_bmp(f, work_);
  std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
  if (lk.owns_lock()) bmp_.swap(work_);  // a request is copying: skip this one, never wait
}

void Preview::serve() {
  std::vector<std::uint8_t> copy;
  while (running_) {
    pollfd pfd{listen_fd_, POLLIN, 0};
    if (::poll(&pfd, 1, 250) <= 0) continue;  // wake up now and then to notice stop()
    const int c = ::accept(listen_fd_, nullptr, nullptr);
    if (c < 0) continue;
    char req[512] = {0};
    pollfd cp{c, POLLIN, 0};
    if (::poll(&cp, 1, 1000) > 0) (void)::read(c, req, sizeof req - 1);
    std::string hdr;
    const char* body;
    std::size_t len;
    if (std::strncmp(req, "GET /frame.bmp", 14) == 0) {
      {
        std::lock_guard<std::mutex> lk(mu_);
        copy = bmp_;
      }
      if (copy.empty()) {
        hdr = "HTTP/1.1 503 Service Unavailable\r\n";
        body = "";
        len = 0;
      } else {
        hdr = "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\nCache-Control: no-store\r\n";
        body = reinterpret_cast<const char*>(copy.data());
        len = copy.size();
      }
    } else {
      hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
      body = kPage;
      len = std::strlen(kPage);
    }
    hdr += "Content-Length: " + std::to_string(len) + "\r\nConnection: close\r\n\r\n";
    send_all(c, hdr.data(), hdr.size());
    send_all(c, body, len);
    ::close(c);
  }
}

}  // namespace brim::preview
