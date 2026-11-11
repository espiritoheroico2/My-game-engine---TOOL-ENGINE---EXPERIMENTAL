#pragma once

// Infraestrutura minima de testes compartilhada entre os arquivos de teste.
// Mantem exatamente o contrato do test_main.cpp original: Check/CHECK/Near e contadores globais.

#include <cmath>
#include <iostream>

namespace SpriteUiTests {

inline int failures = 0;
inline int testsRun = 0;

inline void Check(bool condition, const char* expression, const char* testName) {
    ++testsRun;
    if (!condition) {
        ++failures;
        std::cerr << "[FAIL] " << testName << ": " << expression << '\n';
    }
}

[[nodiscard]] inline bool Near(float a, float b, float epsilon = 1.0e-4f) {
    return std::abs(a - b) <= epsilon;
}

} // namespace SpriteUiTests

#define CHECK(expr) ::SpriteUiTests::Check((expr), #expr, __func__)
