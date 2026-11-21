// Dependency-free test harness (so tests also build with the QNX toolchain and run on the Pi).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mini_test {

struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Registrar {
  Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int run_all() {
  int failed_cases = 0;
  for (auto& c : registry()) {
    int before = failures();
    c.fn();
    bool ok = failures() == before;
    if (!ok) failed_cases++;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
  }
  std::printf("%zu cases, %d failed\n", registry().size(), failed_cases);
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace mini_test

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST_CASE(name)                                                        \
  static void MT_CAT(mt_fn_, __LINE__)();                                      \
  static mini_test::Registrar MT_CAT(mt_reg_, __LINE__)(name, MT_CAT(mt_fn_, __LINE__)); \
  static void MT_CAT(mt_fn_, __LINE__)()

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);   \
      mini_test::failures()++;                                                 \
    }                                                                          \
  } while (0)

#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  %s:%d: REQUIRE(%s) failed\n", __FILE__, __LINE__, #cond); \
      mini_test::failures()++;                                                 \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define MINI_TEST_MAIN() \
  int main() { return mini_test::run_all(); }
