// Laptop backend: AF_UNIX datagram sockets. Framing: first byte 'E' (event, JSON follows) or
// 'P' (pulse, followed by int32 code + int32 value in host byte order).
// scripts/fake_events.py speaks this framing too.
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "ipc/bus.hpp"

namespace brim::ipc {

namespace {

constexpr int kMaxDatagram = 64 * 1024;

std::string run_dir() {
  const char* env = std::getenv("BRIM_RUN_DIR");
  return env && *env ? env : "/tmp/brim";
}

std::string socket_path(const std::string& channel) { return run_dir() + "/" + channel + ".sock"; }

bool make_addr(const std::string& path, sockaddr_un& addr) {
  std::memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof addr.sun_path) return false;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  return true;
}

class UdsEndpoint final : public Endpoint {
 public:
  explicit UdsEndpoint(int fd) : fd_(fd), buf_(kMaxDatagram) {}
  ~UdsEndpoint() override { ::close(fd_); }

  std::optional<Message> receive(int timeout_ms) override {
    pollfd p{fd_, POLLIN, 0};
    int r = ::poll(&p, 1, timeout_ms);
    if (r <= 0) return std::nullopt;
    ssize_t n = ::recv(fd_, buf_.data(), buf_.size(), 0);
    if (n <= 0) return std::nullopt;
    Message m;
    if (buf_[0] == 'P' && n >= 9) {
      m.kind = Message::Kind::Pulse;
      std::int32_t code, value;
      std::memcpy(&code, &buf_[1], 4);
      std::memcpy(&value, &buf_[5], 4);
      m.code = code;
      m.value = value;
      return m;
    }
    if (buf_[0] == 'E') {
      m.kind = Message::Kind::Event;
      m.payload.assign(&buf_[1], static_cast<std::size_t>(n - 1));
      return m;
    }
    return std::nullopt;  // unknown framing: ignore
  }

 private:
  int fd_;
  std::vector<char> buf_;
};

class UdsConnection final : public Connection {
 public:
  explicit UdsConnection(std::string path) : path_(std::move(path)) {
    fd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd_ >= 0) {
      int sz = kMaxDatagram;
      ::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
      ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);
    }
    make_addr(path_, addr_);
  }
  ~UdsConnection() override {
    if (fd_ >= 0) ::close(fd_);
  }

  bool send(std::string_view payload) override {
    out_.resize(payload.size() + 1);
    out_[0] = 'E';
    std::memcpy(&out_[1], payload.data(), payload.size());
    return send_raw();
  }

  bool pulse(int code, int value) override {
    out_.resize(9);
    out_[0] = 'P';
    std::int32_t c = code, v = value;
    std::memcpy(&out_[1], &c, 4);
    std::memcpy(&out_[5], &v, 4);
    return send_raw();
  }

 private:
  bool send_raw() {
    if (fd_ < 0) return false;
    ssize_t n = ::sendto(fd_, out_.data(), out_.size(), 0, reinterpret_cast<sockaddr*>(&addr_), sizeof addr_);
    return n == static_cast<ssize_t>(out_.size());
  }

  std::string path_;
  sockaddr_un addr_{};
  int fd_ = -1;
  std::vector<char> out_;
};

}  // namespace

std::unique_ptr<Endpoint> listen(const std::string& channel, std::string* err) {
  std::string dir = run_dir();
  ::mkdir(dir.c_str(), 0700);
  std::string path = socket_path(channel);
  sockaddr_un addr{};
  if (!make_addr(path, addr)) {
    if (err) *err = "socket path too long: " + path;
    return nullptr;
  }
  int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) {
    if (err) *err = std::strerror(errno);
    return nullptr;
  }
  int sz = 512 * 1024;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
  ::unlink(path.c_str());  // stale socket from a crashed previous instance
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    if (err) *err = "bind " + path + ": " + std::strerror(errno);
    ::close(fd);
    return nullptr;
  }
  ::chmod(path.c_str(), 0600);  // only our user may inject events
  return std::make_unique<UdsEndpoint>(fd);
}

std::unique_ptr<Connection> connect(const std::string& channel) {
  return std::make_unique<UdsConnection>(socket_path(channel));
}

const char* backend_name() { return "uds"; }

}  // namespace brim::ipc
