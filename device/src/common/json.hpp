// Minimal JSON value + parser + serializer. Small on purpose: no third-party dependency to port
// to QNX, no exceptions. Objects keep insertion order. Numbers are doubles (all our integers —
// ids, monotonic ms timestamps — are < 2^53 so they round-trip exactly).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brim::json {

class Value {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : kind_(Kind::Bool), b_(b) {}
  Value(double d) : kind_(Kind::Number), n_(d) {}
  Value(int v) : kind_(Kind::Number), n_(v) {}
  Value(std::int64_t v) : kind_(Kind::Number), n_(static_cast<double>(v)) {}
  Value(const char* s) : kind_(Kind::String), s_(s) {}
  Value(std::string s) : kind_(Kind::String), s_(std::move(s)) {}
  Value(std::string_view s) : kind_(Kind::String), s_(s) {}

  static Value array() { Value v; v.kind_ = Kind::Array; return v; }
  static Value object() { Value v; v.kind_ = Kind::Object; return v; }

  Kind kind() const { return kind_; }
  bool is_null() const { return kind_ == Kind::Null; }
  bool is_bool() const { return kind_ == Kind::Bool; }
  bool is_number() const { return kind_ == Kind::Number; }
  bool is_string() const { return kind_ == Kind::String; }
  bool is_array() const { return kind_ == Kind::Array; }
  bool is_object() const { return kind_ == Kind::Object; }

  bool as_bool() const { return b_; }
  double as_double() const { return n_; }
  std::int64_t as_int() const;
  const std::string& as_string() const { return s_; }
  const Array& as_array() const { return a_; }
  Array& as_array() { return a_; }
  const Object& as_object() const { return o_; }

  // Object helpers. set() replaces an existing key.
  const Value* find(std::string_view key) const;
  Value* find(std::string_view key);
  void set(std::string_view key, Value v);
  bool erase(std::string_view key);
  void push_back(Value v) { a_.push_back(std::move(v)); }

  // Typed getters with defaults (handy for config files).
  std::string get_string(std::string_view key, std::string def = {}) const;
  double get_number(std::string_view key, double def = 0) const;
  std::int64_t get_int(std::string_view key, std::int64_t def = 0) const;
  bool get_bool(std::string_view key, bool def = false) const;

  std::string dump() const;
  bool operator==(const Value& o) const;
  bool operator!=(const Value& o) const { return !(*this == o); }

 private:
  void dump_to(std::string& out) const;
  Kind kind_ = Kind::Null;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  Array a_;
  Object o_;
};

// Returns nullopt on malformed input; *err (if given) gets a short reason with byte offset.
std::optional<Value> parse(std::string_view text, std::string* err = nullptr);

// Reads and parses a whole file.
std::optional<Value> parse_file(const std::string& path, std::string* err = nullptr);

}  // namespace brim::json
