#pragma once

// Pontos de entrada das suites de teste que vivem em arquivos proprios.
// Cada suite e registrada em main() de tests/test_main.cpp.

namespace SpriteUiTests {

void RunGraphSamplingTests();
void RunTextBreakingTests();
void RunTextWrapLayoutTests();
void RunCaretAndHitTests();
void RunFlexWrapTests();
void RunFlexBaselineTests();
void RunRobustnessTests();
void RunNineSliceTests();
void RunVisualTests();

} // namespace SpriteUiTests
