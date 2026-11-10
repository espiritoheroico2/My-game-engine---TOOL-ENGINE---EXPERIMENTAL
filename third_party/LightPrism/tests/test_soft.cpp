// Testes de ponta a ponta com o renderizador por software: clusters == todas as luzes, orçamento, teto por
// cluster, PNG válido. Gravam imagens em LP_TEST_OUTPUT_DIR (se definido) para inspeção.
#include <cmath>
#include <cstdio>
#include <string>

#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Soft/Png.hpp"

using namespace LightPrism;
using namespace LightPrism::Soft;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)

#ifndef LP_TEST_OUTPUT_DIR
#define LP_TEST_OUTPUT_DIR ""
#endif

static void SaveIfConfigured(const char* name, const std::vector<uint8_t>& rgb, uint32_t w, uint32_t h) {
    const std::string dir = LP_TEST_OUTPUT_DIR;
    if (dir.empty()) return;
    CHECK(WritePng((dir + "/" + name).c_str(), rgb, w, h));
}

static void TestClusteredEqualsAllLights() {
    const uint32_t W = 320, H = 180;
    LightingSystem lights;
    PopulateDemoLights(lights, 120, 12);
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer r(MakeDemoScene(), cam, 16, 24, 128);

    FrameParams fp;
    fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);
    CHECK(frame.pointCount == 120 && frame.spotCount == 12);

    FrameBuffer all, cl;
    RenderStats sa, sc;
    r.Render(frame, ShadeMode::AllLights, all, sa);
    r.Render(frame, ShadeMode::Clustered, cl, sc);

    CHECK(!sc.overflow);
    CHECK(sa.pixelsHit > uint64_t(W) * H / 2);
    CHECK(MaxAbsDiff(all, cl) < 1e-4);                                  // culling não perde luz
    CHECK(double(sc.lightEvaluations) < 0.25 * double(sa.lightEvaluations)); // e economiza bastante
    CHECK(sc.maxLightsInCluster > 0 && sc.maxLightsInCluster <= 128);

    SaveIfConfigured("test_lit.png", ToneMapToRgb8(cl), W, H);
    SaveIfConfigured("test_heatmap.png", HeatmapRgb8(cl, sc.maxLightsInCluster), W, H);
    SaveIfConfigured("test_diff_x100.png", DiffRgb8(all, cl, 100.0f), W, H);
}

static void TestTileSizes() {
    // Qualquer tamanho de tile e número de fatias deve dar o mesmo resultado que o laço completo.
    const uint32_t W = 200, H = 120;
    LightingSystem lights;
    PopulateDemoLights(lights, 60, 8);
    const CameraSetup cam = MakeDemoCamera(W, H);
    FrameParams fp;
    fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);

    for (uint32_t tile : {8u, 16u, 32u, 40u}) {       // 40 não divide 200 nem 120: tiles parciais nas bordas
        for (uint32_t slices : {12u, 24u, 48u}) {
            SoftRenderer r(MakeDemoScene(), cam, tile, slices, 128);
            FrameBuffer all, cl;
            RenderStats sa, sc;
            r.Render(frame, ShadeMode::AllLights, all, sa);
            r.Render(frame, ShadeMode::Clustered, cl, sc);
            CHECK(!sc.overflow);
            CHECK(MaxAbsDiff(all, cl) < 1e-4);
        }
    }
}

static void TestLightBudget() {
    LightingConfig cfg;
    cfg.maxPointLights = 40;
    cfg.maxSpotLights = 4;
    LightingSystem lights(cfg);
    PopulateDemoLights(lights, 120, 12);
    const CameraSetup cam = MakeDemoCamera(160, 90);
    SoftRenderer r(MakeDemoScene(), cam, 16, 24, 128);
    FrameParams fp;
    fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);
    CHECK(frame.pointCount == 40 && frame.spotCount == 4);
    CHECK(frame.droppedLights == 80 + 8);

    FrameBuffer all, cl;
    RenderStats sa, sc;
    r.Render(frame, ShadeMode::AllLights, all, sa);
    r.Render(frame, ShadeMode::Clustered, cl, sc);
    CHECK(MaxAbsDiff(all, cl) < 1e-4); // com orçamento aplicado, clusters continuam == todas as (restantes) luzes
}

static void TestPerClusterCapVisible() {
    // Com teto minúsculo por cluster, o resultado difere do completo (luz foi cortada) mas o teto é respeitado.
    const CameraSetup cam = MakeDemoCamera(160, 90);
    LightingSystem lights;
    PopulateDemoLights(lights, 120, 12);
    FrameParams fp;
    fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);

    SoftRenderer r(MakeDemoScene(), cam, 16, 24, /*cap*/ 3);
    FrameBuffer all, cl;
    RenderStats sa, sc;
    r.Render(frame, ShadeMode::AllLights, all, sa);
    r.Render(frame, ShadeMode::Clustered, cl, sc);
    CHECK(sc.maxLightsInCluster == 3);
    CHECK(MaxAbsDiff(all, cl) > 1e-3);
    for (uint32_t c : cl.clusterLightCount) CHECK(c <= 3);
}

static void TestPngWriter() {
    const uint32_t w = 37, h = 23; // tamanho que gera mais de um bloco deflate? (37*3+1)*23 = 2576 B: um bloco
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = uint8_t(i * 7);
    CHECK(!WritePng("/nonexistent_dir/x.png", rgb, w, h));
    CHECK(!WritePng("x.png", rgb, w, h + 1)); // tamanho não bate

    const std::string path = "lp_png_test.png";
    CHECK(WritePng(path.c_str(), rgb, w, h));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        unsigned char head[24] = {};
        CHECK(std::fread(head, 1, 24, f) == 24);
        const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        for (int i = 0; i < 8; ++i) CHECK(head[i] == sig[i]);
        const uint32_t pw = (uint32_t(head[16]) << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
        const uint32_t ph = (uint32_t(head[20]) << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
        CHECK(pw == w && ph == h);
        std::fclose(f);
        std::remove(path.c_str());
    }

    // Imagem grande o bastante para exigir vários blocos "stored" (> 65535 bytes).
    std::vector<uint8_t> big(size_t(300) * 200 * 3, 128);
    CHECK(WritePng("lp_png_big.png", big, 300, 200));
    std::remove("lp_png_big.png");
}

int main() {
    TestClusteredEqualsAllLights();
    TestTileSizes();
    TestLightBudget();
    TestPerClusterCapVisible();
    TestPngWriter();
    if (g_failures == 0) std::printf("Testes soft: todos passaram.\n");
    else std::printf("Testes soft: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
