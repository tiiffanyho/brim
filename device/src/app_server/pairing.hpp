// Pairing: a 6-digit PIN shown on the hat's dashboard; POST /api/pair with the right PIN returns an
// HttpOnly session cookie. Brute force protection: 5 wrong PINs -> locked for 60 s (doubling).
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace brim::app {

inline constexpr const char* kSessionCookie = "brim_session";

std::string random_pin();    // 6 digits, from /dev/urandom
std::string random_token();  // 32 random bytes, hex
// Extracts brim_session=<token> from a Cookie header ("a=1; brim_session=abc").
std::string session_from_cookie_header(const std::string& cookie_header);

class Pairing {
 public:
  explicit Pairing(std::string pin) : pin_(std::move(pin)) {}
  enum class Result { Ok, Wrong, Locked };
  Result attempt(const std::string& pin, std::int64_t now_ms, std::int64_t* retry_after_s = nullptr);
  const std::string& pin() const { return pin_; }

 private:
  std::mutex mu_;
  std::string pin_;
  int failures_ = 0;
  std::int64_t locked_until_ms_ = 0;
  std::int64_t lock_ms_ = 60000;
};

}  // namespace brim::app
