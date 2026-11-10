// Testes do SSAO (Fase 4b): referência CPU contra AO por raios (verdade), propriedades básicas e layout.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
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

namespace {

Scene MakeCornerScene(bool withGeometry) {
    Scene s;
    Plane floor;
    floor.point = {0, 0, 0}; floor.normal = {0, 1, 0};
    s.planes.push_back(floor);
    if (withGeometry) {
        Plane wall;
        wall.point = {0, 0, -6}; wall.normal = {0, 0, 1};
        s.planes.push_back(wall);
        Sphere a; a.center = {-1.6f, 0.8f, -4.2f}; a.radius = 0.8f; s.spheres.push_back(a);
        Sphere b; b.center = {1.4f, 0.8f, -3.0f}; b.radius = 0.8f; s.spheres.push_back(b);
    }
    return s;
}

CameraSetup MakeCam(uint32_t w, uint32_t h) {
    CameraSetup c;
    c.eye = {0.0f, 2.4f, 0.5f}; c.target = {0.0f, 0.5f, -5.0f};
    c.fovYRad = 1.0f; c.zNear = 0.1f; c.zFar = 80.0f; c.width = w; c.height = h;
    return c;
}

struct Result {
    GBufferView gb;
    std::vector<float> raw, blurred, truth;
};

Result Run(const Scene& scene, const Ssao::SsaoConfig& cfg, uint32_t w, uint32_t h, uint32_t rays = 256) {
    SoftRenderer r(scene, MakeCam(w, h));
    Result out;
    out.gb = r.ExportGBuffer();
    Ssao::SsaoInput in;
    in.width = w; in.height = h; in.viewDepth = out.gb.viewDepth.data(); in.viewNormal = out.gb.viewNormal.data();
    in.proj00 = r.Proj().m[0][0]; in.proj11 = r.Proj().m[1][1];
    Ssao::ComputeSsaoRaw(cfg, in, out.raw);
    Ssao::BilateralBlur(cfg, in, out.raw, out.blurred);
    r.ComputeRayTracedAO(cfg.radius, rays, out.truth);
    return out;
}

struct Err { double mean = 0, max = 0, corr = 0; size_t n = 0; };

Err Compare(const Result& r, const std::vector<float>& test) {
    Err e;
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (size_t i = 0; i < test.size(); ++i) {
        if (!r.gb.hit[i]) continue;
        const double a = test[i], b = r.truth[i], d = std::fabs(a - b);
        e.mean += d; e.max = std::max(e.max, d); ++e.n;
        sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b;
    }
    const double n = double(e.n);
    e.mean /= n;
    const double cov = sab / n - (sa / n) * (sb / n), va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
    e.corr = (va > 1e-12 && vb > 1e-12) ? cov / std::sqrt(va * vb) : 1.0;
    return e;
}

double Variance(const Result& r, const std::vector<float>& v) {  // ruído: variação local dentro do piso aberto
    double s = 0, ss = 0; size_t n = 0;
    for (size_t i = 0; i < v.size(); ++i) if (r.gb.hit[i]) { s += v[i]; ss += double(v[i]) * v[i]; ++n; }
    const double m = s / double(n);
    return ss / double(n) - m * m;
}

} // namespace

static void TestFlatFloor() {
    Ssao::SsaoConfig cfg;
    const Result r = Run(MakeCornerScene(false), cfg, 160, 90);
    double mn = 1.0, sum = 0; size_t n = 0;
    for (size_t i = 0; i < r.raw.size(); ++i) if (r.gb.hit[i]) { mn = std::min<double>(mn, r.blurred[i]); sum += r.blurred[i]; ++n; }
    std::printf("  piso plano: AO medio %.4f, minimo %.4f\n", sum / double(n), mn);
    CHECK(sum / double(n) > 0.995);
    CHECK(mn > 0.95);
}

static void TestAgainstRayTracedAO() {
    Ssao::SsaoConfig cfg;
    cfg.kernelSize = 16; cfg.radius = 0.6f;
    const uint32_t W = 320, H = 180;
    const Result r = Run(MakeCornerScene(true), cfg, W, H, 256);
    const Err eb = Compare(r, r.blurred), er = Compare(r, r.raw);
    std::printf("  SSAO vs AO por raios (%zu pixels): bruto erro medio %.4f corr %.3f | blur erro medio %.4f max %.3f corr %.3f\n", eb.n,
                er.mean, er.corr, eb.mean, eb.max, eb.corr);
    CHECK(eb.mean < 0.06);
    CHECK(eb.corr > 0.85);
    CHECK(Variance(r, r.blurred) <= Variance(r, r.raw) + 1e-6);

    // Só nos pixels realmente ocluídos (verdade < 0.9): é aqui que o SSAO precisa acertar.
    double occErr = 0, occBias = 0; size_t occN = 0;
    for (size_t i = 0; i < r.blurred.size(); ++i)
        if (r.gb.hit[i] && r.truth[i] < 0.9f) { occErr += std::fabs(r.blurred[i] - r.truth[i]); occBias += r.blurred[i] - r.truth[i]; ++occN; }
    std::printf("  pixels ocluidos (%zu): erro medio %.4f, vies %.4f\n", occN, occErr / double(occN), occBias / double(occN));

    CHECK(occN > 1500);
    CHECK(occErr / double(occN) < 0.14);
    CHECK(std::fabs(occBias / double(occN)) < 0.06);   // sem viés sistemático (nem claro nem escuro demais)

    // O AO por raios tem de ter contraste (senão o teste não prova nada).
    double minTruth = 1.0; for (size_t i = 0; i < r.truth.size(); ++i) if (r.gb.hit[i]) minTruth = std::min<double>(minTruth, r.truth[i]);
    CHECK(minTruth < 0.55);

    const char* envDir = std::getenv("LP_TEST_OUTPUT_DIR");
    const std::string dir = envDir ? envDir : LP_TEST_OUTPUT_DIR;
    if (!dir.empty()) {
        WritePng((dir + "/ssao_blur.png").c_str(), AoRgb8(r.blurred, r.gb.hit), W, H);
        WritePng((dir + "/ssao_raw.png").c_str(), AoRgb8(r.raw, r.gb.hit), W, H);
        WritePng((dir + "/ssao_truth.png").c_str(), AoRgb8(r.truth, r.gb.hit), W, H);
    }
}

static void TestRadiusMonotonic() {
    // Raio maior enxerga mais oclusão: o AO médio não pode subir.
    double prev = 2.0;
    for (float radius : {0.15f, 0.4f, 0.9f}) {
        Ssao::SsaoConfig cfg; cfg.radius = radius;
        const Result r = Run(MakeCornerScene(true), cfg, 160, 90);
        double sum = 0; size_t n = 0;
        for (size_t i = 0; i < r.blurred.size(); ++i) if (r.gb.hit[i]) { sum += r.blurred[i]; ++n; }
        const double mean = sum / double(n);
        std::printf("  raio %.2f m: AO medio %.4f\n", radius, mean);
        CHECK(mean <= prev + 1e-4);
        prev = mean;
    }
}

static void TestKernelAndLayout() {
    Vec3 k[Ssao::kMaxKernel];
    Ssao::BuildKernel(16, k);
    for (int i = 0; i < 16; ++i) {
        CHECK(k[i].z > 0.0f);
        const float len = Length(k[i]);
        CHECK(len > 0.0f && len <= 1.0001f);
    }
    CHECK(sizeof(Ssao::SsaoGPU) == 576);
    CHECK(offsetof(Ssao::SsaoGPU, proj00) == 512);
    CHECK(offsetof(Ssao::SsaoGPU, kernelSize) == 560);
    // Linearização do reverso-Z que o shader usa: d = P23 / (z + P22).
    const Mat4 P = PerspectiveRH_ReverseZ(1.0f, 1.7f, 0.1f, 100.0f);
    for (float d : {0.1f, 1.0f, 7.5f, 60.0f}) {
        const float z = (P.m[2][2] * -d + P.m[2][3]) / d;
        CHECK(std::fabs(P.m[2][3] / (z + P.m[2][2]) - d) < 1e-3f * d + 1e-4f);
    }
    // Padrão de rotação: 16 valores distintos.
    bool seen[16] = {};
    for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x) seen[int(Ssao::KernelRotation(x, y) / (2.0f * kPi / 16.0f) + 0.5f) % 16] = true;
    for (bool s : seen) CHECK(s);
}

int main() {
    TestFlatFloor();
    TestAgainstRayTracedAO();
    TestRadiusMonotonic();
    TestKernelAndLayout();
    if (g_failures == 0) std::printf("Testes de SSAO: todos passaram.\n");
    else std::printf("Testes de SSAO: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
