// Pi backend: QNX Neutrino native message passing.
//   listen()  -> name_attach(NULL, channel, 0); MsgReceive() with TimerTimeout for timeouts.
//   connect() -> name_open(channel, 0); MsgSendv() for events; MsgSendPulse() for heartbeats.
// Events: header {type=kBrimMsgType, len} + JSON payload; the server replies immediately with EOK
// so senders never wait on app_server's processing. A 100 ms send/reply timeout guards against a
// stopped receiver. Because QNX propagates client priority to the server thread on MsgSend,
// low-priority app_server work done on behalf of brim briefly runs at brim's priority —
// keep the receive path tiny (copy + reply), do the real work on another thread.
//
// TODO(qnx-check): this file has NOT been compiled yet. On the Pi (or with the SDP toolchain):
//   cmake --preset qnx-aarch64le && cmake --build --preset qnx-aarch64le
//   then run brim + app_server and confirm `ls /dev/name/local` shows brim_events, brim_cmd,
//   brim_supervisor, and that `pidin` shows the receive threads RECEIVE-blocked.
#include <errno.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>

#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "common/timing.hpp"
#include "ipc/bus.hpp"

namespace brim::ipc {

namespace {

constexpr std::uint16_t kBrimMsgType = _IO_MAX + 0x100;  // outside the resource-manager range
constexpr std::size_t kMaxMsg = 64 * 1024;
constexpr std::uint64_t kSendTimeoutNs = 100ULL * 1000000ULL;

struct BrimMsgHdr {
  std::uint16_t type;
  std::uint16_t subtype;
  std::uint32_t len;
};

class QnxEndpoint final : public Endpoint {
 public:
  explicit QnxEndpoint(name_attach_t* att) : att_(att), buf_(kMaxMsg + sizeof(BrimMsgHdr)) {}
  ~QnxEndpoint() override { name_detach(att_, 0); }

  std::optional<Message> receive(int timeout_ms) override {
    const std::int64_t deadline = timing::mono_ns() + static_cast<std::int64_t>(timeout_ms) * 1000000LL;
    for (;;) {
      std::int64_t left = deadline - timing::mono_ns();
      if (left <= 0) return std::nullopt;
      std::uint64_t ns = static_cast<std::uint64_t>(left);
      TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, nullptr, &ns, nullptr);
      _msg_info info{};
      rcvid_t rcvid = MsgReceive(att_->chid, buf_.data(), buf_.size(), &info);
      if (rcvid == -1) return std::nullopt;  // ETIMEDOUT or EINTR

      if (rcvid == 0) {  // pulse
        auto* p = reinterpret_cast<struct _pulse*>(buf_.data());
        if (p->code == _PULSE_CODE_DISCONNECT) {
          ConnectDetach(p->scoid);
          continue;
        }
        if (p->code == _PULSE_CODE_UNBLOCK) continue;
        if (p->code >= _PULSE_CODE_MINAVAIL) {
          Message m;
          m.kind = Message::Kind::Pulse;
          m.code = p->code - _PULSE_CODE_MINAVAIL;
          m.value = p->value.sival_int;
          return m;
        }
        continue;
      }

      auto* hdr = reinterpret_cast<std::uint16_t*>(buf_.data());
      if (*hdr == _IO_CONNECT) {  // name_open() handshake
        MsgReply(rcvid, EOK, nullptr, 0);
        continue;
      }
      if (*hdr != kBrimMsgType || info.msglen < sizeof(BrimMsgHdr)) {
        MsgError(rcvid, ENOSYS);
        continue;
      }
      auto* bh = reinterpret_cast<BrimMsgHdr*>(buf_.data());
      std::size_t len = bh->len;
      if (len > kMaxMsg || sizeof(BrimMsgHdr) + len > info.msglen) {
        MsgError(rcvid, EMSGSIZE);
        continue;
      }
      Message m;
      m.kind = Message::Kind::Event;
      m.payload.assign(buf_.data() + sizeof(BrimMsgHdr), len);
      MsgReply(rcvid, EOK, nullptr, 0);  // reply first: sender unblocks immediately
      return m;
    }
  }

 private:
  name_attach_t* att_;
  std::vector<char> buf_;
};

class QnxConnection final : public Connection {
 public:
  explicit QnxConnection(std::string channel) : channel_(std::move(channel)) {}
  ~QnxConnection() override {
    if (coid_ != -1) name_close(coid_);
  }

  bool send(std::string_view payload) override {
    if (!ensure()) return false;
    BrimMsgHdr hdr{kBrimMsgType, 0, static_cast<std::uint32_t>(payload.size())};
    iov_t iov[2];
    SETIOV(&iov[0], &hdr, sizeof hdr);
    SETIOV(&iov[1], const_cast<char*>(payload.data()), payload.size());
    std::uint64_t ns = kSendTimeoutNs;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, nullptr, &ns, nullptr);
    if (MsgSendv(coid_, iov, 2, nullptr, 0) == -1) {
      reset();
      return false;
    }
    return true;
  }

  bool pulse(int code, int value) override {
    if (!ensure()) return false;
    // priority -1: use the calling thread's priority.
    if (MsgSendPulse(coid_, -1, _PULSE_CODE_MINAVAIL + code, value) == -1) {
      reset();
      return false;
    }
    return true;
  }

 private:
  bool ensure() {
    if (coid_ != -1) return true;
    auto now = timing::mono_ms();
    if (now - last_attempt_ms_ < 200) return false;  // don't hammer name_open while receiver is down
    last_attempt_ms_ = now;
    coid_ = name_open(channel_.c_str(), 0);
    return coid_ != -1;
  }
  void reset() {
    if (coid_ != -1) name_close(coid_);
    coid_ = -1;
  }

  std::string channel_;
  int coid_ = -1;
  std::int64_t last_attempt_ms_ = -1000;
};

}  // namespace

std::unique_ptr<Endpoint> listen(const std::string& channel, std::string* err) {
  name_attach_t* att = name_attach(nullptr, channel.c_str(), 0);
  if (!att) {
    if (err) *err = std::string("name_attach failed: ") + std::strerror(errno);
    return nullptr;
  }
  return std::make_unique<QnxEndpoint>(att);
}

std::unique_ptr<Connection> connect(const std::string& channel) { return std::make_unique<QnxConnection>(channel); }

const char* backend_name() { return "qnx"; }

}  // namespace brim::ipc
