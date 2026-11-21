#include "app_server/pairing.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <random>

namespace brim::app {

namespace {
// /dev/urandom exists on QNX (random resource manager) and on macOS/Linux.
// TODO(qnx-check): confirm `random` is started in the Pi image (ls /dev/urandom).
void urandom(unsigned char* out, std::size_t n) {
  int fd = ::open("/dev/urandom", O_RDONLY);
  std::size_t got = 0;
  if (fd >= 0) {
    while (got < n) {
      ssize_t r = ::read(fd, out + got, n - got);
      if (r <= 0) break;
      got += static_cast<std::size_t>(r);
    }
    ::close(fd);
  }
  if (got < n) {  // last resort; logged by caller via weak PIN? keep running (cannot-fail)
    std::random_device rd;
    for (; got < n; ++got) out[got] = static_cast<unsigned char>(rd());
  }
}
}  // namespace

std::string random_pin() {
  unsigned char b[4];
  urandom(b, sizeof b);
  std::uint32_t v = (std::uint32_t(b[0]) << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
  char buf[8];
  std::snprintf(buf, sizeof buf, "%06u", v % 1000000u);
  return buf;
}

std::string random_token() {
  unsigned char b[32];
  urandom(b, sizeof b);
  static const char* hex = "0123456789abcdef";
  std::string s;
  for (unsigned char c : b) {
    s.push_back(hex[c >> 4]);
    s.push_back(hex[c & 15]);
  }
  return s;
}

std::string session_from_cookie_header(const std::string& h) {
  const std::string key = std::string(kSessionCookie) + "=";
  std::size_t i = 0;
  while (i < h.size()) {
    while (i < h.size() && (h[i] == ' ' || h[i] == ';')) ++i;
    std::size_t end = h.find(';', i);
    if (end == std::string::npos) end = h.size();
    if (h.compare(i, key.size(), key) == 0) return h.substr(i + key.size(), end - i - key.size());
    i = end;
  }
  return {};
}

Pairing::Result Pairing::attempt(const std::string& pin, std::int64_t now_ms, std::int64_t* retry_after_s) {
  std::lock_guard<std::mutex> lk(mu_);
  if (now_ms < locked_until_ms_) {
    if (retry_after_s) *retry_after_s = (locked_until_ms_ - now_ms + 999) / 1000;
    return Result::Locked;
  }
  // Constant-time compare.
  unsigned diff = pin.size() ^ pin_.size();
  for (std::size_t i = 0; i < pin_.size(); ++i) diff |= static_cast<unsigned>(i < pin.size() ? pin[i] : 0) ^ pin_[i];
  if (diff == 0) {
    failures_ = 0;
    lock_ms_ = 60000;
    return Result::Ok;
  }
  if (++failures_ >= 5) {
    failures_ = 0;
    locked_until_ms_ = now_ms + lock_ms_;
    if (retry_after_s) *retry_after_s = lock_ms_ / 1000;
    lock_ms_ *= 2;
    return Result::Locked;
  }
  return Result::Wrong;
}

}  // namespace brim::app
