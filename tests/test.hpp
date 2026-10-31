// Mini framework de testes (sem dependencias externas).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tt {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
} // namespace tt

#define TEST(name) \
  static void name(); \
  static ::tt::Reg reg_##name(#name, name); \
  static void name()

#define CHECK(cond) \
  do { if (!(cond)) { std::printf("    FALHA %s:%d  %s\n", __FILE__, __LINE__, #cond); ++::tt::failures(); } } while (0)
