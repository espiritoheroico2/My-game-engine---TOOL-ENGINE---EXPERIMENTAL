// Modelo da janela "Test map of chunks" (SpriteUI): contagens, aviso de mapa grande e cursor do lado do mapa.
// Nao usa SDL. Roda no ctest como Editor.TestMapModel.
#include "test_map_model.hpp"

#include <cstdio>

namespace {

using namespace editor::testmap;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
  }
}
#define CHECK(expr) Check((expr), #expr)

void TestSummaryCountsChunksAndBlocks() {
  current = "TestSummaryCountsChunksAndBlocks";
  const Summary s = Summarize(32);
  CHECK(s.chunks == 1024);
  CHECK(s.blocks == 512);
}

void TestSideIsClampedToTheSliderRange() {
  current = "TestSideIsClampedToTheSliderRange";
  CHECK(ClampSide(0) == kMinSide);
  CHECK(ClampSide(-5) == kMinSide);
  CHECK(ClampSide(1000) == kMaxSide);
  CHECK(Summarize(1000).chunks == static_cast<long long>(kMaxSide) * kMaxSide);
}

void TestLargeWarningStartsAboveFourThousandChunks() {
  current = "TestLargeWarningStartsAboveFourThousandChunks";
  CHECK(!Summarize(64).large);   // 4096 chunks: ainda sem aviso
  CHECK(Summarize(65).large);    // 4225 chunks: aviso
}

void TestFractionMapsToSideAtEnds() {
  current = "TestFractionMapsToSideAtEnds";
  CHECK(SideOfFraction(0.f) == kMinSide);
  CHECK(SideOfFraction(1.f) == kMaxSide);
  CHECK(SideOfFraction(-1.f) == kMinSide);
  CHECK(SideOfFraction(2.f) == kMaxSide);
}

void TestSideAndFractionRoundTrip() {
  current = "TestSideAndFractionRoundTrip";
  for (int side = kMinSide; side <= kMaxSide; ++side) {
    if (SideOfFraction(FractionOfSide(side)) != side) {
      CHECK(false);
      return;
    }
  }
  CHECK(true);
}

}  // namespace

int main() {
  TestSummaryCountsChunksAndBlocks();
  TestSideIsClampedToTheSliderRange();
  TestLargeWarningStartsAboveFourThousandChunks();
  TestFractionMapsToSideAtEnds();
  TestSideAndFractionRoundTrip();
  std::printf("Editor.TestMapModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
