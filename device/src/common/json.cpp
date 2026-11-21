#include "common/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace brim::json {

std::int64_t Value::as_int() const { return static_cast<std::int64_t>(std::llround(n_)); }

const Value* Value::find(std::string_view key) const {
  if (kind_ != Kind::Object) return nullptr;
  for (const auto& kv : o_)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

Value* Value::find(std::string_view key) {
  return const_cast<Value*>(static_cast<const Value*>(this)->find(key));
}

void Value::set(std::string_view key, Value v) {
  if (kind_ != Kind::Object) {
    *this = object();
  }
  for (auto& kv : o_) {
    if (kv.first == key) {
      kv.second = std::move(v);
      return;
    }
  }
  o_.emplace_back(std::string(key), std::move(v));
}

bool Value::erase(std::string_view key) {
  for (auto it = o_.begin(); it != o_.end(); ++it) {
    if (it->first == key) {
      o_.erase(it);
      return true;
    }
  }
  return false;
}

std::string Value::get_string(std::string_view key, std::string def) const {
  const Value* v = find(key);
  return v && v->is_string() ? v->s_ : def;
}
double Value::get_number(std::string_view key, double def) const {
  const Value* v = find(key);
  return v && v->is_number() ? v->n_ : def;
}
std::int64_t Value::get_int(std::string_view key, std::int64_t def) const {
  const Value* v = find(key);
  return v && v->is_number() ? v->as_int() : def;
}
bool Value::get_bool(std::string_view key, bool def) const {
  const Value* v = find(key);
  return v && v->is_bool() ? v->b_ : def;
}

bool Value::operator==(const Value& o) const {
  if (kind_ != o.kind_) return false;
  switch (kind_) {
    case Kind::Null: return true;
    case Kind::Bool: return b_ == o.b_;
    case Kind::Number: return n_ == o.n_;
    case Kind::String: return s_ == o.s_;
    case Kind::Array: return a_ == o.a_;
    case Kind::Object: {
      if (o_.size() != o.o_.size()) return false;
      for (const auto& kv : o_) {
        const Value* other = o.find(kv.first);
        if (!other || !(*other == kv.second)) return false;
      }
      return true;
    }
  }
  return false;
}

static void escape_to(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void Value::dump_to(std::string& out) const {
  switch (kind_) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += b_ ? "true" : "false"; break;
    case Kind::Number: {
      char buf[32];
      if (!std::isfinite(n_)) {
        out += "null";  // JSON has no NaN/Inf
      } else if (std::fabs(n_) < 9e15 && n_ == std::floor(n_)) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n_));
        out += buf;
      } else {
        std::snprintf(buf, sizeof buf, "%.10g", n_);
        out += buf;
      }
      break;
    }
    case Kind::String: escape_to(out, s_); break;
    case Kind::Array: {
      out.push_back('[');
      for (size_t i = 0; i < a_.size(); ++i) {
        if (i) out.push_back(',');
        a_[i].dump_to(out);
      }
      out.push_back(']');
      break;
    }
    case Kind::Object: {
      out.push_back('{');
      for (size_t i = 0; i < o_.size(); ++i) {
        if (i) out.push_back(',');
        escape_to(out, o_[i].first);
        out.push_back(':');
        o_[i].second.dump_to(out);
      }
      out.push_back('}');
      break;
    }
  }
}

std::string Value::dump() const {
  std::string out;
  dump_to(out);
  return out;
}

namespace {

class Parser {
 public:
  explicit Parser(std::string_view t) : t_(t) {}

  std::optional<Value> run(std::string* err) {
    auto v = value(0);
    ws();
    if (v && i_ != t_.size()) {
      error_ = "trailing characters";
      v.reset();
    }
    if (!v && err) *err = error_ + " at offset " + std::to_string(i_);
    return v;
  }

 private:
  static constexpr int kMaxDepth = 64;

  void ws() {
    while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t')) ++i_;
  }
  bool lit(std::string_view s) {
    if (t_.substr(i_, s.size()) == s) {
      i_ += s.size();
      return true;
    }
    return false;
  }

  std::optional<Value> value(int depth) {
    if (depth > kMaxDepth) { error_ = "nesting too deep"; return std::nullopt; }
    ws();
    if (i_ >= t_.size()) { error_ = "unexpected end"; return std::nullopt; }
    char c = t_[i_];
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') {
      auto s = string();
      if (!s) return std::nullopt;
      return Value(std::move(*s));
    }
    if (lit("true")) return Value(true);
    if (lit("false")) return Value(false);
    if (lit("null")) return Value(nullptr);
    if (c == '-' || (c >= '0' && c <= '9')) return number();
    error_ = "unexpected character";
    return std::nullopt;
  }

  std::optional<Value> number() {
    size_t start = i_;
    if (t_[i_] == '-') ++i_;
    while (i_ < t_.size() && ((t_[i_] >= '0' && t_[i_] <= '9') || t_[i_] == '.' || t_[i_] == 'e' ||
                              t_[i_] == 'E' || t_[i_] == '+' || t_[i_] == '-'))
      ++i_;
    std::string tok(t_.substr(start, i_ - start));
    char* end = nullptr;
    double d = std::strtod(tok.c_str(), &end);
    if (end != tok.c_str() + tok.size() || tok == "-") {
      error_ = "bad number";
      return std::nullopt;
    }
    return Value(d);
  }

  static void utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool hex4(unsigned& out) {
    if (i_ + 4 > t_.size()) return false;
    out = 0;
    for (int k = 0; k < 4; ++k) {
      char h = t_[i_++];
      out <<= 4;
      if (h >= '0' && h <= '9') out |= h - '0';
      else if (h >= 'a' && h <= 'f') out |= h - 'a' + 10;
      else if (h >= 'A' && h <= 'F') out |= h - 'A' + 10;
      else return false;
    }
    return true;
  }

  std::optional<std::string> string() {
    ++i_;  // opening quote
    std::string out;
    while (i_ < t_.size()) {
      char c = t_[i_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) { error_ = "control char in string"; return std::nullopt; }
      if (c != '\\') { out.push_back(c); continue; }
      if (i_ >= t_.size()) break;
      char e = t_[i_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) { error_ = "bad \\u escape"; return std::nullopt; }
          if (cp >= 0xD800 && cp <= 0xDBFF && lit("\\u")) {
            unsigned lo;
            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { error_ = "bad surrogate"; return std::nullopt; }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(out, cp);
          break;
        }
        default: error_ = "bad escape"; return std::nullopt;
      }
    }
    error_ = "unterminated string";
    return std::nullopt;
  }

  std::optional<Value> array(int depth) {
    ++i_;
    Value a = Value::array();
    ws();
    if (i_ < t_.size() && t_[i_] == ']') { ++i_; return a; }
    for (;;) {
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      a.push_back(std::move(*v));
      ws();
      if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
      if (i_ < t_.size() && t_[i_] == ']') { ++i_; return a; }
      error_ = "expected , or ]";
      return std::nullopt;
    }
  }

  std::optional<Value> object(int depth) {
    ++i_;
    Value o = Value::object();
    ws();
    if (i_ < t_.size() && t_[i_] == '}') { ++i_; return o; }
    for (;;) {
      ws();
      if (i_ >= t_.size() || t_[i_] != '"') { error_ = "expected key"; return std::nullopt; }
      auto k = string();
      if (!k) return std::nullopt;
      ws();
      if (i_ >= t_.size() || t_[i_] != ':') { error_ = "expected :"; return std::nullopt; }
      ++i_;
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      o.set(*k, std::move(*v));
      ws();
      if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
      if (i_ < t_.size() && t_[i_] == '}') { ++i_; return o; }
      error_ = "expected , or }";
      return std::nullopt;
    }
  }

  std::string_view t_;
  size_t i_ = 0;
  std::string error_;
};

}  // namespace

std::optional<Value> parse(std::string_view text, std::string* err) { return Parser(text).run(err); }

std::optional<Value> parse_file(const std::string& path, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return std::nullopt;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return parse(ss.str(), err);
}

}  // namespace brim::json
