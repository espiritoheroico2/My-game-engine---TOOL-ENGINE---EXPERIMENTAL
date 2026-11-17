// Politica de resolucao do atlas bitmap (Text/BitmapRasterPolicy.hpp): buckets, DPI, sobreamostragem configuravel
// e entradas invalidas. Funcao pura: nao precisa de fonte nem de GPU.
#include "SpriteUi/Text/BitmapRasterPolicy.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using ToolEngine::SpriteUi::Text::BitmapRasterPolicy;
using ToolEngine::SpriteUi::Text::ChooseBitmapRasterPixelSize;
using ToolEngine::SpriteUi::Text::kBitmapRasterBuckets;

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

bool IsBucket(std::uint32_t value) {
    for (const std::uint32_t bucket : kBitmapRasterBuckets) {
        if (bucket == value) return true;
    }
    return false;
}

// Padrao O = 1.25: 16 pt a 1x pede 20 px -> bucket 24; a 2x pede 40 px -> 48; a 1.5x pede 30 px -> 32.
void TestDefaultOversamplingPicksBucket() {
    current = "TestDefaultOversamplingPicksBucket";
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 1.0f) == 24);
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 2.0f) == 48);
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 1.5f) == 32);
    CHECK(ChooseBitmapRasterPixelSize(24.0f, 2.0f) == 64);   // 24 * 2 * 1.25 = 60 -> 64
}

// O configuravel: com O = 1.0 e DPI 1, 16 px exatos ficam no proprio bucket.
void TestConfigurableOversampling() {
    current = "TestConfigurableOversampling";
    BitmapRasterPolicy noOversampling;
    noOversampling.oversampling = 1.0f;
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 1.0f, noOversampling) == 16);

    BitmapRasterPolicy doubled;
    doubled.oversampling = 2.0f;
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 1.0f, doubled) == 32);   // 16 * 2 = 32 exato
}

// Acima de 256 o bucket satura; tamanhos extremos nao quebram nem estouram.
void TestSaturatesAtLargestBucket() {
    current = "TestSaturatesAtLargestBucket";
    CHECK(ChooseBitmapRasterPixelSize(200.0f, 1.0f) == 256);   // 250 -> 256
    CHECK(ChooseBitmapRasterPixelSize(1000.0f, 1.0f) == 256);
    CHECK(ChooseBitmapRasterPixelSize(1.0e30f, 1.0e30f) == 256);
}

// Entradas invalidas usam valores seguros em vez de falhar.
void TestInvalidInputsUseSafeDefaults() {
    current = "TestInvalidInputsUseSafeDefaults";
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(ChooseBitmapRasterPixelSize(nan, 1.0f) == 24);      // tamanho logico 16 padrao
    CHECK(ChooseBitmapRasterPixelSize(0.0f, 1.0f) == 24);
    CHECK(ChooseBitmapRasterPixelSize(-8.0f, 1.0f) == 24);
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 0.0f) == 24);    // DPI 1 padrao
    CHECK(ChooseBitmapRasterPixelSize(16.0f, inf) == 24);
    BitmapRasterPolicy badOversampling;
    badOversampling.oversampling = 0.0f;                       // volta a 1.25
    CHECK(ChooseBitmapRasterPixelSize(16.0f, 1.0f, badOversampling) == 24);
}

// Monotonia: aumentar o DPI nunca diminui o bucket, e todo resultado e um bucket valido.
void TestMonotonicAndAlwaysABucket() {
    current = "TestMonotonicAndAlwaysABucket";
    std::uint32_t previous = 0;
    for (float dpi = 0.5f; dpi <= 6.0f; dpi += 0.25f) {
        const std::uint32_t bucket = ChooseBitmapRasterPixelSize(16.0f, dpi);
        CHECK(IsBucket(bucket));
        CHECK(bucket >= previous);
        previous = bucket;
    }
}

} // namespace

int main() {
    TestDefaultOversamplingPicksBucket();
    TestConfigurableOversampling();
    TestSaturatesAtLargestBucket();
    TestInvalidInputsUseSafeDefaults();
    TestMonotonicAndAlwaysABucket();
    std::printf("bitmap raster policy: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
