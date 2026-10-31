#include "test.hpp"

int main() {
  int ran = 0;
  for (auto& c : tt::cases()) {
    const int before = tt::failures();
    c.fn();
    std::printf("[%s] %s\n", tt::failures() == before ? " OK " : "FAIL", c.name);
    ++ran;
  }
  std::printf("\n%d testes, %d falhas\n", ran, tt::failures());
  return tt::failures() == 0 ? 0 : 1;
}
