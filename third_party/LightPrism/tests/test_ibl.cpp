// Testes do IBL (Fase 4a): SH9, cubemap, LUT da BRDF, ambiente pré-filtrado e o split-sum completo contra uma
// integração numérica por força bruta. Tudo em CPU, sem API gráfica.
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/Post/Ssao.hpp"
#include "LightPrism/Soft/DemoScene.hpp"

using namespace LightPrism;
using namespace LightPrism::Ibl;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= double(tol))

namespace {

Vec3 RandomDir(Soft::DemoRng& rng) {
    const float z = rng.Range(-1.0f, 1.0f), p = rng.Range(0.0f, 2.0f * kPi);
    const float s = std::sqrt(1.0f - z * z);
    return {s * std::cos(p), z, s * std::sin(p)};
}

float Lum(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// Irradiância/pi por força bruta.
Vec3 BruteIrradiance(const EnvFn& env, const Vec3& n, uint32_t res = 400) {
    const uint32_t nz = res, nphi = res * 2;
    const double dw = 4.0 * double(kPi) / (double(nz) * double(nphi));
    double a[3] = {};
    for (uint32_t i = 0; i < nz; ++i) {
        const float z = 1.0f - 2.0f * (float(i) + 0.5f) / float(nz);
        const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
        for (uint32_t j = 0; j < nphi; ++j) {
            const float ph = 2.0f * kPi * (float(j) + 0.5f) / float(nphi);
            const Vec3 d{s * std::cos(ph), z, s * std::sin(ph)};
            const float c = Dot(n, d);
            if (c <= 0.0f) continue;
            const Vec3 L = env(d);
            a[0] += double(L.x) * c * dw; a[1] += double(L.y) * c * dw; a[2] += double(L.z) * c * dw;
        }
    }
    const double inv = 1.0 / double(kPi);
    return {float(a[0] * inv), float(a[1] * inv), float(a[2] * inv)};
}

const ProceduralSky kSky{};
EnvFn SkyFn() { return [](const Vec3& d) { return kSky.Radiance(d); }; }

} // namespace

static void TestShConstant() {
    const EnvFn env = [](const Vec3&) { return Vec3{0.7f, 0.4f, 0.2f}; };
    const Sh9 irr = ConvolveCosine(ProjectToSh(env, 64));
    Soft::DemoRng rng;
    for (int i = 0; i < 50; ++i) {
        const Vec3 n = RandomDir(rng);
        const Vec3 e = EvalSh(irr, n);
        CHECK_NEAR(e.x, 0.7f, 2e-3); CHECK_NEAR(e.y, 0.4f, 2e-3); CHECK_NEAR(e.z, 0.2f, 2e-3);
    }
}

static void TestShVsBruteForce() {
    const Sh9 irr = ConvolveCosine(ProjectToSh(SkyFn(), 128));
    Soft::DemoRng rng;
    double worst = 0.0;
    for (int i = 0; i < 40; ++i) {
        const Vec3 n = RandomDir(rng);
        const Vec3 truth = BruteIrradiance(SkyFn(), n);
        const Vec3 sh = EvalSh(irr, n);
        worst = std::max(worst, double(std::fabs(Lum(sh) - Lum(truth)) / std::max(Lum(truth), 0.05f)));
    }
    std::printf("  SH9 vs integral: pior erro relativo %.2f%%\n", 100.0 * worst);
    CHECK(worst < 0.05);
}

static void TestShDirectional() {
    // Um lobo cosseno: L(w) = max(0, w.z). Irradiância/pi em n=+z é conhecida: (1/pi) * int max(0,cos)^2 = 2/3.
    const EnvFn env = [](const Vec3& d) { const float v = std::max(0.0f, d.z); return Vec3{v, v, v}; };
    const Sh9 irr = ConvolveCosine(ProjectToSh(env, 128));
    const Vec3 e = EvalSh(irr, {0, 0, 1});
    std::printf("  lobo cosseno em +z: %.4f (SH9) vs %.4f\n", e.x, BruteIrradiance(env, {0, 0, 1}).x);
    CHECK_NEAR(e.x, BruteIrradiance(env, {0, 0, 1}).x, 0.02);
}

static void TestPackedMatches() {
    const Sh9 irr = ConvolveCosine(ProjectToSh(SkyFn(), 64));
    const IblGPU g = MakeIblGPU(irr, 1.0f, 6);
    Soft::DemoRng rng;
    for (int i = 0; i < 50; ++i) {
        const Vec3 n = RandomDir(rng);
        const Vec3 a = EvalSh(irr, n), b = EvaluateIrradiancePacked(g, n);
        CHECK_NEAR(a.x, b.x, 1e-4); CHECK_NEAR(a.y, b.y, 1e-4); CHECK_NEAR(a.z, b.z, 1e-4);
    }
    CHECK(g.specularMaxMip == 5.0f);
}

static void TestCubemap() {
    Soft::DemoRng rng;
    for (int i = 0; i < 500; ++i) {
        const Vec3 d = RandomDir(rng);
        uint32_t f; float a, b;
        CubeDirToFaceUV(d, f, a, b);
        CHECK(f < 6 && std::fabs(a) <= 1.0001f && std::fabs(b) <= 1.0001f);
        const Vec3 r = CubeFaceUVToDir(f, a, b);
        CHECK_NEAR(r.x, d.x, 1e-4); CHECK_NEAR(r.y, d.y, 1e-4); CHECK_NEAR(r.z, d.z, 1e-4);
    }
    // Amostrar um mip 0 de um céu suave reproduz o céu.
    const PrefilteredEnv pe = BuildPrefilteredEnv(SkyFn(), 64, 2, 8);
    double worst = 0.0;
    for (int i = 0; i < 200; ++i) {
        const Vec3 d = RandomDir(rng);
        worst = std::max(worst, double(std::fabs(Lum(SampleCubemap(pe.mips[0], d)) - Lum(kSky.Radiance(d)))));
    }
    std::printf("  cubemap 64^2 vs ambiente: erro absoluto max %.4f\n", worst);
    CHECK(worst < 0.15);
}

static void TestPrefilter() {
    // Ambiente constante -> todos os mips constantes.
    const EnvFn flat = [](const Vec3&) { return Vec3{0.5f, 0.5f, 0.5f}; };
    const PrefilteredEnv pf = BuildPrefilteredEnv(flat, 32, 5, 64);
    Soft::DemoRng rng;
    for (int i = 0; i < 40; ++i) {
        const Vec3 d = RandomDir(rng);
        const Vec3 s = SamplePrefiltered(pf, d, rng.Next01());
        CHECK_NEAR(s.x, 0.5f, 1e-3);
    }
    // Contra a integral de força bruta com o mesmo peso (D*NoH importance == integral ponderada por D*NoL*NoH...).
    // Verdade: L(R) = int env(l) D(h) NoH... usamos a definição de Karis: média ponderada por NoL das amostras GGX;
    // aqui checamos monotonia/suavização: rugosidade maior reduz o contraste do lobo do sol.
    const PrefilteredEnv pe = BuildPrefilteredEnv(SkyFn(), 64, 6, 256);
    const Vec3 sun = Normalize(kSky.sunDir);
    float prev = 1e9f;
    for (int m = 0; m < 6; ++m) {
        const float v = Lum(SampleCubemap(pe.mips[m], sun));
        std::printf("  pico do sol no mip %d (rugosidade %.1f): %.3f\n", m, m / 5.0f, v);
        CHECK(v <= prev + 1e-3f);
        prev = v;
    }
    // Conservação: o valor médio do mip na esfera fica próximo da média do ambiente (não cria nem some energia).
    double avg0 = 0, avg5 = 0; int n = 0;
    for (int i = 0; i < 600; ++i) {
        const Vec3 d = RandomDir(rng);
        avg0 += Lum(SampleCubemap(pe.mips[0], d)); avg5 += Lum(SampleCubemap(pe.mips[5], d)); ++n;
    }
    std::printf("  media de luminancia mip0 %.4f  mip5 %.4f\n", avg0 / n, avg5 / n);
    CHECK_NEAR(avg0 / n, avg5 / n, 0.06 * (avg0 / n));
}

static void TestLut() {
    // Convergência: 256 amostras ~ 16384.
    double worstConv = 0.0;
    for (float r : {0.1f, 0.3f, 0.6f, 0.9f})
        for (float nv : {0.1f, 0.4f, 0.8f, 1.0f}) {
            float a1, b1, a2, b2;
            IntegrateBrdf(nv, r, 256, a1, b1);
            IntegrateBrdf(nv, r, 16384, a2, b2);
            worstConv = std::max(worstConv, double(std::fabs(a1 - a2) + std::fabs(b1 - b2)));
            CHECK(a2 + b2 <= 1.02f);   // conservação de energia (sem multiscattering)
            CHECK(a2 >= 0.0f && b2 >= 0.0f);
        }
    std::printf("  LUT: convergencia 256 vs 16384 amostras, erro max %.4f\n", worstConv);
    CHECK(worstConv < 0.03);

    // Contra força bruta (grade na esfera, ambiente = 1). B = F0=0 ; A+B = F0=1.
    double worstGrid = 0.0;
    for (float r : {0.35f, 0.6f, 0.9f})
        for (float nv : {0.3f, 0.7f, 1.0f}) {
            Reference::SurfaceData s;
            s.N = {0, 0, 1};
            s.V = {std::sqrt(1.0f - nv * nv), 0.0f, nv};
            s.metallic = 1.0f;
            s.roughness = r;
            const EnvFn one = [](const Vec3&) { return Vec3{1, 1, 1}; };
            s.albedo = {0, 0, 0};
            const float B = IntegrateIndirectReference(one, s, 700).x;
            s.albedo = {1, 1, 1};
            const float AB = IntegrateIndirectReference(one, s, 700).x;
            float A2, B2;
            IntegrateBrdf(nv, r, 8192, A2, B2);
            worstGrid = std::max(worstGrid, double(std::fabs(B - B2) + std::fabs(AB - (A2 + B2))));
        }
    std::printf("  LUT vs integral na esfera: erro max %.4f\n", worstGrid);
    CHECK(worstGrid < 0.02);

    // LUT amostrada vs integração direta.
    const BrdfLut lut = BuildBrdfLut(32, 512);
    double worstLut = 0.0;
    Soft::DemoRng rng;
    for (int i = 0; i < 60; ++i) {
        const float nv = rng.Range(0.08f, 1.0f), r = rng.Range(0.1f, 1.0f);
        float a, b, la, lb;
        IntegrateBrdf(nv, r, 4096, a, b);
        SampleBrdfLut(lut, nv, r, la, lb);
        worstLut = std::max(worstLut, double(std::fabs(a - la) + std::fabs(b - lb)));
    }
    std::printf("  LUT 32^2 amostrada: erro max %.4f\n", worstLut);
    CHECK(worstLut < 0.04);
}

static void TestSplitSumVsIntegral() {
    const IblData ibl = BuildIblData(SkyFn(), 96, 64, 6, 32);
    struct Case { Vec3 albedo; float metallic, roughness; };
    const Case cases[] = {
        {{0.8f, 0.8f, 0.8f}, 0.0f, 0.9f}, {{0.8f, 0.3f, 0.2f}, 0.0f, 0.5f}, {{0.9f, 0.9f, 0.9f}, 1.0f, 0.4f},
        {{0.95f, 0.64f, 0.54f}, 1.0f, 0.7f}, {{0.2f, 0.5f, 0.8f}, 0.0f, 0.3f}, {{1.0f, 0.85f, 0.6f}, 1.0f, 0.3f},
    };
    Soft::DemoRng rng;
    double worst = 0.0;
    for (const Case& c : cases)
        for (int k = 0; k < 4; ++k) {
            Reference::SurfaceData s;
            s.N = RandomDir(rng);
            // V dentro do hemisfério de N, em ângulos moderados e rasantes
            Vec3 t = Normalize(Cross(s.N, std::fabs(s.N.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
            const float ang = rng.Range(0.0f, 1.3f);
            s.V = Normalize(s.N * std::cos(ang) + t * std::sin(ang));
            s.albedo = c.albedo; s.metallic = c.metallic; s.roughness = c.roughness;
            const Vec3 truth = IntegrateIndirectReference(SkyFn(), s, 400);
            const Vec3 got = EvaluateIndirect(ibl, s, 1.0f).Total();
            const double err = std::fabs(Lum(got) - Lum(truth)) / std::max(double(Lum(truth)), 0.05);
            worst = std::max(worst, err);
        }
    std::printf("  split-sum + SH vs integral completa: pior erro relativo %.1f%%\n", 100.0 * worst);
    CHECK(worst < 0.16);  // erro conhecido do split-sum em ângulos rasantes (>70 graus)

    // AO só reduz; ao=1 é identidade.
    Reference::SurfaceData s;
    s.N = {0, 1, 0}; s.V = Normalize(Vec3{0, 1, 1}); s.albedo = {0.7f, 0.7f, 0.7f}; s.roughness = 0.5f;
    const Vec3 full = EvaluateIndirect(ibl, s, 1.0f).Total();
    const Vec3 half = EvaluateIndirect(ibl, s, 0.5f).Total();
    const Vec3 zero = EvaluateIndirect(ibl, s, 0.0f).Total();
    CHECK(Lum(half) < Lum(full) && Lum(half) > Lum(zero));
    CHECK(Lum(zero) < 0.02f * Lum(full) + 1e-3f);
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Os .hlsl não são compilados aqui (sem dxc); ao menos conferimos bindings e tamanhos contra os structs C++.
static void TestShaderMirrors() {
#ifdef LP_SHADER_DIR
    const std::string root = LP_SHADER_DIR;
    const std::string ibl = ReadFile(root + "/Common/Ibl.hlsli");
    const std::string ssao = ReadFile(root + "/PostProcess/Ssao.compute.hlsl");
    CHECK(!ibl.empty() && !ssao.empty());
    for (const char* tok : {"register(b3)", "register(t3)", "register(t4)", "register(s2)", "float4 sh[9]", "float  intensity;",
                            "float  specularMaxMip;", "n.x * n.x - n.y * n.y", "3.0 * n.z * n.z - 1.0", "IblDominantDirFactor"})
        CHECK(ibl.find(tok) != std::string::npos);
    const std::string kernelDecl = "Kernel[LP_MAX_KERNEL]";
    CHECK(ssao.find(kernelDecl) != std::string::npos);
    CHECK(ssao.find("#define LP_MAX_KERNEL " + std::to_string(Ssao::kMaxKernel)) != std::string::npos);
    for (const char* tok : {"register(b4)", "register(t5)", "register(t6)", "register(t7)", "register(u0)", "void SsaoMain", "void SsaoBlur",
                            "Proj23 / (z + Proj22)", "{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}"})
        CHECK(ssao.find(tok) != std::string::npos);
#endif
}

static void TestLayout() {
    CHECK(sizeof(IblGPU) == 160);
    CHECK(offsetof(IblGPU, intensity) == 144);
    CHECK(offsetof(IblGPU, flags) == 152);
}

int main() {
    TestShConstant();
    TestShVsBruteForce();
    TestShDirectional();
    TestPackedMatches();
    TestCubemap();
    TestPrefilter();
    TestLut();
    TestSplitSumVsIntegral();
    TestShaderMirrors();
    TestLayout();
    if (g_failures == 0) std::printf("Testes de IBL: todos passaram.\n");
    else std::printf("Testes de IBL: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
