// Testes da Fase 6: VSM, sombras suaves por SDF, neblina volumétrica, probes de GI.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Rt/Bvh.hpp"
#include "LightPrism/Rt/ProbeGrid.hpp"
#include "LightPrism/Rt/Transfer.hpp"
#include "LightPrism/Rt/RtEffects.hpp"
#include "LightPrism/Sdf/Sdf.hpp"
#include "LightPrism/Shadow/Csm.hpp"
#include "LightPrism/Shadow/Vsm.hpp"
#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Volumetric/Fog.hpp"
#include "LightPrism/Soft/Png.hpp"
#include "LightPrism/Soft/SceneMesh.hpp"

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

double NowMs() {
    using C = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(C::now().time_since_epoch()).count();
}

std::vector<uint8_t> Gray(const std::vector<float>& v, const std::vector<uint8_t>& hit) {
    std::vector<uint8_t> rgb(v.size() * 3, 0);
    for (size_t i = 0; i < v.size(); ++i) {
        if (!hit[i]) { rgb[3 * i] = 12; rgb[3 * i + 1] = 14; rgb[3 * i + 2] = 22; continue; }
        const uint8_t g = uint8_t(std::pow(Saturate(v[i]), 1.0f / 2.2f) * 255.0f + 0.5f);
        rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = g;
    }
    return rgb;
}

std::vector<uint8_t> Rgb8(const std::vector<Vec3>& c, const std::vector<uint8_t>& hit, float exposure = 1.0f) {
    std::vector<uint8_t> rgb(c.size() * 3, 0);
    auto enc = [&](float v) { v *= exposure; v = v / (1.0f + v); return uint8_t(std::pow(Saturate(v), 1.0f / 2.2f) * 255.0f + 0.5f); };
    for (size_t i = 0; i < c.size(); ++i) { (void)hit; rgb[3 * i] = enc(c[i].x); rgb[3 * i + 1] = enc(c[i].y); rgb[3 * i + 2] = enc(c[i].z); }
    return rgb;
}

const std::string kOut = LP_TEST_OUTPUT_DIR;

} // namespace

// ===========================================================================================================
// VSM
// ===========================================================================================================
static void TestVsmSynthetic() {
    using namespace Vsm;
    const uint32_t N = 128;
    std::vector<float> depth(size_t(N) * N, 0.8f);
    for (uint32_t y = 40; y < 88; ++y) for (uint32_t x = 40; x < 88; ++x) depth[size_t(y) * N + x] = 0.3f;

    auto edgeWidth = [&](uint32_t r) {
        VsmMap m = VsmFromDepth(depth.data(), N, false);
        VsmBlur(m, r);
        VsmConfig c; c.bleedReduction = 0.0f;
        // varre uma linha atravessando a borda esquerda do oclusor
        float prev = 2.0f; int width = 0; bool monotone = true;
        for (int x = 20; x <= 64; ++x) {
            const float v = VsmSample(m, float(x) + 0.5f, 64.5f, 0.8f, c);
            if (v > prev + 1e-5f) monotone = false;
            prev = v;
            if (v > 0.02f && v < 0.98f) ++width;
        }
        CHECK(monotone);
        CHECK(VsmSample(m, 64.5f, 64.5f, 0.8f, c) < 0.01f);     // umbra
        CHECK(VsmSample(m, 10.5f, 10.5f, 0.8f, c) > 0.999f);    // longe: totalmente iluminado
        CHECK(VsmSample(m, 64.5f, 64.5f, 0.25f, c) == 1.0f);      // receptor na frente do oclusor
        return width;
    };
    const int w3 = edgeWidth(3), w8 = edgeWidth(8), w0 = edgeWidth(0);
    std::printf("  penumbra (texels com 2%%<vis<98%%): raio 0 -> %d, raio 3 -> %d, raio 8 -> %d\n", w0, w3, w8);
    CHECK(w0 <= 1);
    CHECK(w3 >= 2 && w3 <= 2 * 3 + 3);
    CHECK(w8 > w3);                // blur maior = penumbra mais larga
}

// bias de inclinação e piso de variância: sem eles, planos viram acne (autossombra)
static void TestVsmAcne() {
    using namespace Vsm;
    const uint32_t N = 32;
    std::vector<float> flat(size_t(N) * N, 0.5f), ramp(size_t(N) * N);
    for (uint32_t y = 0; y < N; ++y) for (uint32_t x = 0; x < N; ++x) ramp[size_t(y) * N + x] = 0.3f + 0.002f * float(x);
    VsmConfig c; c.bleedReduction = 0.0f;
    const VsmMap mf = VsmFromDepth(flat.data(), N, false);
    const float vFlat = VsmSample(mf, 16.5f, 16.5f, 0.5f + 1e-4f, c);
    VsmConfig noFloor = c; noFloor.minVariance = 0.0f;
    const float vNoFloor = VsmSample(mf, 16.5f, 16.5f, 0.5f + 1e-4f, noFloor);
    std::printf("  plano com receptor 1e-4 acima: vis %.2f com piso de variancia | %.2f sem (acne)\n", vFlat, vNoFloor);
    CHECK(vFlat > 0.8f && vNoFloor < 0.05f);
    // rampa: o bias de inclinação (0.25*|grad|²) dá variância onde só há inclinação
    const VsmMap withSlope = VsmFromDepth(ramp.data(), N, true), noSlope = VsmFromDepth(ramp.data(), N, false);
    const float vs = VsmSample(withSlope, 16.5f, 16.5f, 0.3f + 0.002f * 16.0f + 1e-3f, c), vn = VsmSample(noSlope, 16.5f, 16.5f, 0.3f + 0.002f * 16.0f + 1e-3f, c);
    std::printf("  rampa (0.002/texel) com receptor 1e-3 acima: vis %.2f com bias de inclinacao | %.2f sem\n", vs, vn);
    CHECK(vs > 2.0f * vn);
}

// Chebyshev é um LIMITE SUPERIOR da fração iluminada: vsm >= fração exata do footprint (caixa 5x5), sempre.
static void TestVsmChebyshevBound() {
    using namespace Vsm;
    const uint32_t N = 96;
    DemoRng rng;
    std::vector<float> depth(size_t(N) * N);
    for (uint32_t y = 0; y < N; ++y) for (uint32_t x = 0; x < N; ++x) {
        const float smooth = 0.5f + 0.3f * std::sin(0.11f * float(x)) * std::cos(0.07f * float(y));
        depth[size_t(y) * N + x] = Saturate(smooth + (rng.Next01() < 0.15f ? rng.Range(-0.35f, 0.35f) : 0.0f));
    }
    VsmMap m = VsmFromDepth(depth.data(), N, false);
    VsmBlur(m, 2);
    VsmConfig c; c.minVariance = 0.0f; c.bleedReduction = 0.0f;
    uint32_t viol = 0, n = 0; double slack = 0;
    for (int i = 0; i < 20000; ++i) {
        const int x = 3 + int(rng.Next01() * float(N - 6)), y = 3 + int(rng.Next01() * float(N - 6));
        const float t = rng.Range(0.0f, 1.0f);
        int lit = 0;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) if (depth[size_t(y + dy) * N + size_t(x + dx)] >= t) ++lit;
        const float exact = float(lit) / 25.0f;
        const float v = VsmSample(m, float(x) + 0.5f, float(y) + 0.5f, t, c);
        if (v < exact - 1e-4f) ++viol;
        slack += v - exact; ++n;
    }
    std::printf("  Chebyshev vs fracao exata (%u amostras): violacoes do limite superior %u, folga media %.3f\n", n, viol, slack / n);
    CHECK(viol == 0);
}

static void TestVsmBleeding() {
    using namespace Vsm;
    const uint32_t N = 128;
    std::vector<float> depth(size_t(N) * N, 0.9f);
    for (uint32_t y = 0; y < N; ++y) for (uint32_t x = 20; x < 100; ++x) depth[size_t(y) * N + x] = x < 60 ? 0.2f : 0.5f;  // dois oclusores sobrepostos
    VsmMap m = VsmFromDepth(depth.data(), N, false);
    VsmBlur(m, 4);
    auto leak = [&](float reduction) {
        VsmConfig c; c.bleedReduction = reduction;
        float mx = 0;
        for (int x = 40; x < 80; ++x) mx = std::max(mx, VsmSample(m, float(x) + 0.5f, 64.5f, 0.9f, c));  // receptor atrás dos dois: deveria ser 0
        return mx;
    };
    const float l0 = leak(0.0f), l3 = leak(0.3f);
    VsmConfig c3; c3.bleedReduction = 0.3f;
    std::printf("  vazamento de luz (receptor atras de 2 oclusores): Chebyshev puro %.3f -> reducao 0.3: %.3f\n", l0, l3);
    CHECK(l0 > 0.03f);
    CHECK(l3 < 0.005f);
    CHECK(VsmSample(m, 10.5f, 64.5f, 0.9f, c3) > 0.999f);   // a redução não escurece o que está iluminado
    CHECK(VsmSample(m, 78.5f, 64.5f, 0.9f, c3) < 0.01f);    // nem clareia a umbra
}

static void TestVsmEndToEnd() {
    using namespace Vsm;
    const uint32_t W = 480, H = 270;
    LightingSystem lights;
    PopulateDemoLights(lights, 0, 0, 1.5f);
    const CameraSetup cam = MakeDemoCamera(W, H);
    const Scene scene = MakeDemoScene();
    SoftRenderer renderer(scene, cam, 16, 24, 64);
    FrameParams fp; fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);

    CsmConfig cfg; cfg.cascadeCount = 3; cfg.shadowMapSize = 1024; cfg.shadowDistance = 60.0f; cfg.pcfTaps = 1; cfg.blendFraction = 0.0f;
    CsmCameraInput ci; ci.view = renderer.View(); ci.proj00 = renderer.Proj().m[0][0]; ci.proj11 = renderer.Proj().m[1][1];
    ci.zNear = cam.zNear; ci.zFar = cam.zFar;
    const CsmResult csm = ComputeCsm(cfg, ci, frame.sun.direction);
    const ShadowSetup setup = BuildShadowSetup(scene, csm.gpu, frame.sun.direction);
    const GBufferView gb = renderer.ExportGBuffer();
    const Vec3 toSun = -Normalize(frame.sun.direction);

    struct Variant { const char* name; uint32_t radius; float bleed; bool slope; std::vector<VsmMap> maps; std::vector<float> vis; double ms = 0; };
    Variant vars[7] = {{"VSM raio 1", 1, 0.3f, true, {}, {}}, {"VSM raio 3", 3, 0.3f, true, {}, {}}, {"VSM raio 8", 8, 0.3f, true, {}, {}},
                       {"r0 b0.3", 0, 0.3f, true, {}, {}}, {"r1 sem slope", 1, 0.3f, false, {}, {}}, {"r1 b0.6", 1, 0.6f, true, {}, {}}, {"r1 b0.9", 1, 0.9f, false, {}, {}}};
    std::vector<float> hard(W * H, 1.0f);
    std::vector<uint8_t> use(W * H, 0);
    auto cascadeOf = [&](float d) { for (uint32_t k = 0; k < csm.gpu.cascadeCount; ++k) if (d >= csm.gpu.cascades[k].splitNear && d < csm.gpu.cascades[k].splitFar) return int(k); return -1; };
    for (size_t i = 0; i < hard.size(); ++i) {
        if (!gb.hit[i]) continue;
        const int k = cascadeOf(gb.viewDepth[i]);
        if (k < 0 || Dot(gb.worldNormal[i], toSun) <= 0.15f) continue;
        use[i] = 1;
        hard[i] = Reference::CsmSampleCascade(csm.gpu, setup.View(), uint32_t(k), gb.worldPos[i], gb.worldNormal[i]);
    }
    for (int vi = 0; vi < 7; ++vi) {
        Variant& v = vars[vi];
        double t0 = NowMs();
        for (uint32_t k = 0; k < csm.gpu.cascadeCount; ++k) { v.maps.push_back(VsmFromDepth(setup.maps[k].data(), setup.size, v.slope)); VsmBlur(v.maps.back(), v.radius); }
        v.ms = NowMs() - t0;
        v.vis.assign(W * H, 1.0f);
        VsmConfig c; c.bleedReduction = v.bleed;
        for (size_t i = 0; i < v.vis.size(); ++i) if (use[i]) v.vis[i] = VsmSampleCascade(csm.gpu, v.maps[size_t(cascadeOf(gb.viewDepth[i]))], uint32_t(cascadeOf(gb.viewDepth[i])), gb.worldPos[i], gb.worldNormal[i], c);
        // "Robusto": o resultado duro é o mesmo em 4 pontos deslocados (r+2) texels no plano da luz (mesma profundidade);
        // longe de qualquer borda de sombra o VSM tem de acertar. Perto da borda, ser suave é o objetivo.
        const Vec3 tu = Normalize(Cross(toSun, Vec3{0, 1, 0})), tv = Cross(toSun, tu);
        double sum = 0; size_t n = 0, litN = 0, litBad = 0, umbraN = 0, umbraBad = 0, soft = 0;
        for (size_t i = 0; i < hard.size(); ++i) if (use[i]) {
            sum += std::fabs(v.vis[i] - hard[i]); ++n;
            if (v.vis[i] > 0.05f && v.vis[i] < 0.95f) ++soft;
            const int k = cascadeOf(gb.viewDepth[i]);
            const float off = float(v.radius + 2) * csm.gpu.cascades[k].texelWorldSize;
            float h4[4];
            const Vec3 d4[4] = {tu * off, tu * -off, tv * off, tv * -off};
            for (int j = 0; j < 4; ++j) h4[j] = Reference::CsmSampleCascade(csm.gpu, setup.View(), uint32_t(k), gb.worldPos[i] + d4[j], gb.worldNormal[i]);
            const bool allLit = hard[i] > 0.99f && h4[0] > 0.99f && h4[1] > 0.99f && h4[2] > 0.99f && h4[3] > 0.99f;
            const bool allDark = hard[i] < 0.01f && h4[0] < 0.01f && h4[1] < 0.01f && h4[2] < 0.01f && h4[3] < 0.01f;
            if (allLit) { ++litN; if (v.vis[i] < 0.9f) ++litBad; }
            if (allDark) { ++umbraN; if (v.vis[i] > 0.15f) ++umbraBad; }
        }
        std::printf("  %-13s: |vsm-duro| medio %.4f | umbra robusta (%zu px) clareada: %.2f%% | luz robusta (%zu px) escurecida: %.2f%% | %zu px de penumbra | momentos %.0f ms\n", v.name,
                    sum / double(n), umbraN, 100.0 * double(umbraBad) / double(std::max<size_t>(umbraN, 1)), litN, 100.0 * double(litBad) / double(std::max<size_t>(litN, 1)), soft, v.ms);
        if (vi < 3) {   // configuração recomendada: bias de inclinação + redução 0.3
            CHECK(sum / double(n) < 0.03);
            CHECK(umbraN > 100 && double(umbraBad) / double(umbraN) < 0.02);
            CHECK(litN > 1000 && double(litBad) / double(litN) < 0.005);
        }
    }
    // blur maior = mais penumbra
    auto softCount = [&](const Variant& v) { size_t s = 0; for (size_t i = 0; i < v.vis.size(); ++i) if (use[i] && v.vis[i] > 0.05f && v.vis[i] < 0.95f) ++s; return s; };
    CHECK(softCount(vars[0]) < softCount(vars[1]) && softCount(vars[1]) < softCount(vars[2]));
    std::printf("  memoria: VSM RG16F %llu KiB por cascata de %u^2 (mapa de profundidade D32 %llu KiB; VSM precisa dos dois se o passe de sombra nao escreve momentos direto)\n",
                (unsigned long long)(VsmBytes(setup.size) / 1024), setup.size, (unsigned long long)(uint64_t(setup.size) * setup.size * 4 / 1024));

    if (!kOut.empty()) {
        auto shade = [&](const std::vector<float>& vis) { std::vector<float> img(W * H, 0.0f); for (size_t i = 0; i < img.size(); ++i) if (gb.hit[i]) img[i] = 0.08f + 0.92f * std::max(0.0f, Dot(gb.worldNormal[i], toSun)) * vis[i]; return Gray(img, gb.hit); };
        WritePng((kOut + "/vsm_hard.png").c_str(), shade(hard), W, H);
        WritePng((kOut + "/vsm_r1.png").c_str(), shade(vars[0].vis), W, H);
        WritePng((kOut + "/vsm_r3.png").c_str(), shade(vars[1].vis), W, H);
        WritePng((kOut + "/vsm_r8.png").c_str(), shade(vars[2].vis), W, H);
    }
}

// ===========================================================================================================
// SDF: sombras suaves com um raio
// ===========================================================================================================
static Sdf::SdfScene ToSdf(const Scene& scene) {
    Sdf::SdfScene s;
    for (const Sphere& sp : scene.spheres) s.spheres.push_back({sp.center, sp.radius});
    for (const Plane& pl : scene.planes) { const Vec3 n = Normalize(pl.normal); s.planes.push_back({n, Dot(n, pl.point)}); }
    return s;
}

static void TestSdfUnits() {
    using namespace Sdf;
    CHECK(std::fabs(DiscVisibleFraction(0.0f) - 0.5f) < 1e-6f && DiscVisibleFraction(1.0f) == 1.0f && std::fabs(DiscVisibleFraction(-1.0f)) < 1e-6f);
    CHECK(std::fabs(DiscVisibleFraction(0.5f) - 0.8045f) < 1e-3f);          // área do segmento circular (o linear daria 0.75)
    for (float s : {0.1f, 0.4f, 0.8f}) CHECK(std::fabs(DiscVisibleFraction(s) + DiscVisibleFraction(-s) - 1.0f) < 1e-5f);   // simetria
    // volume int8 vs campo analítico: erro de quantização + trilinear < 3 cm onde |d| < maxDist
    SdfScene sc; sc.spheres.push_back({{0, 1, 0}, 0.8f}); sc.spheres.push_back({{3, 0.5f, 1}, 0.5f}); sc.planes.push_back({{0, 1, 0}, 0.0f});
    SdfVolume v; v.Bake(sc, {-6, -1, -6}, {6, 5, 6}, 0.25f, 2.0f);
    DemoRng rng; std::vector<float> errs;
    for (int i = 0; i < 4000; ++i) {
        const Vec3 p{rng.Range(-5, 5), rng.Range(-0.5f, 4), rng.Range(-5, 5)};
        const float ref = sc.Distance(p);
        if (std::fabs(ref) > 1.9f) continue;
        errs.push_back(std::fabs(v.Distance(p) - ref));
    }
    std::sort(errs.begin(), errs.end());
    const float p95 = errs[size_t(double(errs.size()) * 0.95)], mx = errs.back();
    std::printf("  volume SDF int8 vs analitico (%zu pontos): erro p95 %.4f m, maximo %.4f m (o maximo fica nas dobras chao/esfera, onde o trilinear de 25 cm nao representa o vinco)\n", errs.size(), p95, mx);
    CHECK(p95 < 0.03f && mx < 0.15f);
}

static void TestSdfShadows() {
    using namespace Sdf;
    const uint32_t W = 480, H = 270;
    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer renderer(scene, cam);
    const GBufferView gb = renderer.ExportGBuffer();
    const Vec3 toSun = Normalize(Vec3{0.85f, 0.30f, 0.15f});
    const SdfScene sdf = ToSdf(scene);
    SdfVolume vol;
    double t0 = NowMs();
    vol.Bake(sdf, {-14.0f, -1.0f, -38.0f}, {14.0f, 7.0f, 10.0f}, 0.25f, 2.0f);
    const double bakeMs = NowMs() - t0;
    std::printf("  volume SDF: %ux%ux%u voxels de 25 cm, %llu KiB (int8, distancia truncada em 2 m), bake %.0f ms\n", vol.Dim(0), vol.Dim(1), vol.Dim(2),
                (unsigned long long)(vol.Bytes() / 1024), bakeMs);

    Rt::Bvh bvh; bvh.Build(TessellateScene(scene), Rt::BvhBuild::BinnedSah);
    std::vector<uint8_t> use(W * H, 0);
    for (size_t i = 0; i < use.size(); ++i) use[i] = gb.hit[i] && Dot(gb.worldNormal[i], toSun) > 0.1f;

    auto run = [&](auto&& dist, const SdfShadowParams& p, uint64_t* steps, double* ms) {
        std::vector<float> out(W * H, 1.0f);
        const double a = NowMs(); uint32_t st = 0;
        for (size_t i = 0; i < out.size(); ++i) if (use[i]) out[i] = SoftShadow(dist, gb.worldPos[i] + gb.worldNormal[i] * 0.02f, toSun, p, &st);
        if (ms) *ms = NowMs() - a;
        if (steps) *steps = st;
        return out;
    };
    auto rt = [&](const Rt::RtShadowConfig& c, uint64_t* nodes, double* ms) {
        std::vector<float> out(W * H, 1.0f);
        const double a = NowMs(); Rt::TraversalStats st;
        for (size_t i = 0; i < out.size(); ++i) if (use[i]) out[i] = Rt::RtSunVisibility(bvh, gb.worldPos[i], gb.worldNormal[i], toSun, c, uint32_t(i), &st);
        if (ms) *ms = NowMs() - a;
        if (nodes) *nodes = st.nodesVisited;
        return out;
    };
    auto compare = [&](const std::vector<float>& a, const std::vector<float>& b, double& mean, double& mismatchHard) {
        double sum = 0; size_t n = 0, mm = 0;
        for (size_t i = 0; i < a.size(); ++i) if (use[i]) { sum += std::fabs(a[i] - b[i]); ++n; if (std::fabs(a[i] - b[i]) > 0.5f) ++mm; }
        mean = sum / double(n); mismatchHard = double(mm) / double(n);
    };
    const auto analytic = [&](const Vec3& p) { return sdf.Distance(p); };
    const auto baked = [&](const Vec3& p) { return vol.Distance(p); };

    // A) sombra dura: SDF vs RT
    SdfShadowParams hardP; hardP.coneHalfAngle = 0.0f; hardP.maxSteps = 128; hardP.tMax = 200.0f;
    Rt::RtShadowConfig rtHard; rtHard.maxDistance = 200.0f;
    double mean, mism;
    const auto sdfHard = run(analytic, hardP, nullptr, nullptr);
    const auto rtHardImg = rt(rtHard, nullptr, nullptr);
    compare(sdfHard, rtHardImg, mean, mism);
    std::printf("  A) sombra dura SDF vs RT: erro medio %.4f, pixels discordantes %.2f%%\n", mean, 100.0 * mism);
    CHECK(mism < 0.02);

    // B) sombra suave (cone 0.05 rad): 1 raio SDF vs 64 raios RT
    SdfShadowParams softP; softP.coneHalfAngle = 0.05f; softP.tMax = 200.0f;
    Rt::RtShadowConfig rtSoft = rtHard; rtSoft.coneHalfAngle = 0.05f; rtSoft.samples = 64;
    Rt::RtShadowConfig rt16 = rtSoft; rt16.samples = 16;
    uint64_t stA = 0, stB = 0, nodes64 = 0, nodes16 = 0; double msA = 0, msB = 0, ms64 = 0, ms16 = 0;
    softP.tMax = std::min(softP.tMax, SafeShadowRange(2.0f, softP.coneHalfAngle));   // 36 m: limite de distância imposto pela truncagem do volume
    const auto sdfSoft = run(analytic, softP, &stA, &msA);
    const auto sdfBaked = run(baked, softP, &stB, &msB);
    const auto rtSoftImg = rt(rtSoft, &nodes64, &ms64);
    rt(rt16, &nodes16, &ms16);
    size_t nUse = 0; for (uint8_t u : use) nUse += u;
    double meanB, mmB, meanC, mmC, meanBase, mmBase;
    compare(sdfSoft, rtSoftImg, meanB, mmB);
    compare(sdfBaked, sdfSoft, meanC, mmC);
    compare(sdfHard, rtSoftImg, meanBase, mmBase);   // base de comparação: sombra dura contra a suave verdadeira
    // só pixels de penumbra (verdade RT entre 2% e 98%): é onde o SDF precisa acertar
    double penSdf = 0, penHard = 0, penVol = 0; size_t penN = 0;
    for (size_t i = 0; i < use.size(); ++i) if (use[i] && rtSoftImg[i] > 0.02f && rtSoftImg[i] < 0.98f) {
        penSdf += std::fabs(sdfSoft[i] - rtSoftImg[i]); penHard += std::fabs(sdfHard[i] - rtSoftImg[i]); penVol += std::fabs(sdfBaked[i] - rtSoftImg[i]); ++penN;
    }
    std::printf("     so penumbra (%zu px): erro SDF %.3f | volume int8 %.3f | fingir sombra dura %.3f\n", penN, penSdf / double(penN), penVol / double(penN), penHard / double(penN));
    CHECK(penN > 100 && penSdf / double(penN) < 0.15 && penSdf < penHard * 0.6 && penVol / double(penN) < 0.2);
    std::printf("  B) sombra suave: SDF analitico vs RT 64 raios: erro medio %.4f (sombra dura dá %.4f) | volume int8 vs analitico: %.4f\n", meanB, meanBase, meanC);
    std::printf("     custo por pixel: SDF analitico %.1f passos, %.0f ns | SDF volume %.1f passos, %.0f ns | RT 16 raios %.0f nos, %.0f ns | RT 64 raios %.0f nos, %.0f ns\n",
                double(stA) / double(nUse), msA * 1e6 / double(nUse), double(stB) / double(nUse), msB * 1e6 / double(nUse), double(nodes16) / double(nUse),
                ms16 * 1e6 / double(nUse), double(nodes64) / double(nUse), ms64 * 1e6 / double(nUse));
    CHECK(meanB < meanBase);
    CHECK(meanB < 0.03);
    CHECK(meanC < 0.03);

    // C) limite de distância no SDF
    SdfShadowParams limP = softP; limP.tMax = 1.5f;
    uint64_t stL = 0; const auto sdfLim = run(baked, limP, &stL, nullptr);
    size_t dark = 0, darkLim = 0; for (size_t i = 0; i < sdfBaked.size(); ++i) if (use[i]) { if (sdfBaked[i] < 0.3f) ++dark; if (sdfLim[i] < 0.3f) ++darkLim; }
    std::printf("  C) limite 1.5 m: passos/pixel %.1f -> %.1f, pixels em sombra %zu -> %zu\n", double(stB) / double(nUse), double(stL) / double(nUse), dark, darkLim);
    CHECK(stL < stB && darkLim < dark && darkLim > dark / 10);

    // D) rebake local == bake completo (objeto movido)
    {
        Scene moved = scene;
        const Sphere& old = moved.spheres[10];
        Vec3 lo = old.center - Vec3{old.radius, old.radius, old.radius}, hi = old.center + Vec3{old.radius, old.radius, old.radius};
        moved.spheres[10].center = moved.spheres[10].center + Vec3{1.4f, 0.0f, -0.8f};
        const Sphere& nw = moved.spheres[10];
        const Vec3 lo2 = nw.center - Vec3{nw.radius, nw.radius, nw.radius}, hi2 = nw.center + Vec3{nw.radius, nw.radius, nw.radius};
        const SdfScene sdf2 = ToSdf(moved);
        SdfVolume inc = vol;
        const uint32_t n1 = inc.Rebake(sdf2, lo, hi), n2 = inc.Rebake(sdf2, lo2, hi2);
        SdfVolume full; full.Bake(sdf2, {-14.0f, -1.0f, -38.0f}, {14.0f, 7.0f, 10.0f}, 0.25f, 2.0f);
        size_t diff = 0; for (size_t i = 0; i < full.Data().size(); ++i) if (full.Data()[i] != inc.Data()[i]) ++diff;
        std::printf("  D) objeto movido: rebake local %u+%u voxels de %zu (%.1f%%), diferencas vs bake completo: %zu\n", n1, n2, full.Data().size(),
                    100.0 * double(n1 + n2) / double(full.Data().size()), diff);
        CHECK(diff == 0);
        CHECK(n1 + n2 < full.Data().size() / 4);
    }

    if (!kOut.empty()) {
        auto shade = [&](const std::vector<float>& vis) { std::vector<float> img(W * H, 0.0f); for (size_t i = 0; i < img.size(); ++i) if (gb.hit[i]) img[i] = 0.08f + 0.92f * std::max(0.0f, Dot(gb.worldNormal[i], toSun)) * vis[i]; return Gray(img, gb.hit); };
        WritePng((kOut + "/sdf_rt64.png").c_str(), shade(rtSoftImg), W, H);
        WritePng((kOut + "/sdf_analytic.png").c_str(), shade(sdfSoft), W, H);
        WritePng((kOut + "/sdf_volume.png").c_str(), shade(sdfBaked), W, H);
        WritePng((kOut + "/sdf_hard.png").c_str(), shade(sdfHard), W, H);
    }
}

// ===========================================================================================================
// Neblina volumétrica
// ===========================================================================================================
static double Lum(const Vec3& c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }

static void TestFogMath() {
    using namespace Fog;
    // Henyey–Greenstein integra 1 na esfera
    for (float g : {0.0f, 0.3f, 0.7f, -0.5f}) {
        double sum = 0; const int n = 20000;
        for (int i = 0; i < n; ++i) { const double th = (double(i) + 0.5) / n * double(kPi); sum += HenyeyGreenstein(float(std::cos(th)), g) * std::sin(th); }
        sum *= 2.0 * double(kPi) * double(kPi) / n;
        CHECK(std::fabs(sum - 1.0) < 2e-3);
    }
    // Meio homogêneo com luz ambiente uniforme: solução analítica L(t) = (σs·A/σt)(1 - e^{-σt t}), T = e^{-σt t}
    FogConfig c; c.sigmaT = 0.05f; c.scatterAlbedo = {0.8f, 0.8f, 0.8f}; c.ambient = {2.0f, 1.0f, 0.5f}; c.maxDistance = 80.0f; c.gridX = 8; c.gridY = 6; c.gridZ = 12;
    FogCamera cam; cam.invView = Mat4::Identity(); cam.proj00 = 1.2f; cam.proj11 = 1.6f;
    FogVolume vol; BuildFog(c, cam, nullptr, nullptr, {}, vol);
    double worst = 0;
    for (uint32_t k = 1; k <= c.gridZ; ++k) {
        const float d = SliceEdge(c, float(k));
        const float u = (3 + 0.5f) / 8.0f, v = (2 + 0.5f) / 6.0f;                       // centro de uma coluna
        const float ndcX = u * 2 - 1, ndcY = 1 - v * 2;
        const float t = (d - c.nearDistance) * Length(FogViewDir(cam, ndcX, ndcY));   // o volume começa em nearDistance
        const FogSample s = SampleFog(vol, u, v, d);
        const double T = std::exp(-double(c.sigmaT) * t), f = (1.0 - T);
        const Vec3 Lx{float(0.8 * c.ambient.x * f), float(0.8 * c.ambient.y * f), float(0.8 * c.ambient.z * f)};
        worst = std::max({worst, std::fabs(double(s.T) - T), std::fabs(double(s.L.x) - double(Lx.x)) / double(Lx.x), std::fabs(double(s.L.z) - double(Lx.z)) / double(Lx.z)});
    }
    std::printf("  meio homogeneo vs solucao analitica (12 slices): erro max %.2e\n", worst);
    CHECK(worst < 1e-4);
    // além do limite de distância: constante
    const FogSample a = SampleFog(vol, 0.4f, 0.4f, c.maxDistance), b = SampleFog(vol, 0.4f, 0.4f, c.maxDistance * 3.0f);
    CHECK(a.T == b.T && a.L.x == b.L.x);
    // monotonia: T não cresce, L não decresce
    bool mono = true; float pt = 1.0f, pl = 0.0f;
    for (float d = 0.2f; d < c.maxDistance; d *= 1.15f) { const FogSample s = SampleFog(vol, 0.4f, 0.4f, d); if (s.T > pt + 1e-6f || float(Lum(s.L)) < pl - 1e-6f) mono = false; pt = s.T; pl = float(Lum(s.L)); }
    CHECK(mono);
}

static void TestFogScene() {
    using namespace Fog;
    const uint32_t W = 480, H = 270;
    Scene scene = MakeDemoScene();
    scene.planes.pop_back();                            // sem a parede: o sol vem do horizonte, atrás das esferas
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer renderer(scene, cam);
    const GBufferView gb = renderer.ExportGBuffer();
    Rt::Bvh bvh; bvh.Build(TessellateScene(scene), Rt::BvhBuild::BinnedSah);
    const Vec3 toSun = Normalize(Vec3{0.25f, 0.28f, -1.0f});
    FogSun sun; sun.toLight = toSun; sun.radiance = {7.0f, 6.0f, 4.6f};
    FogCamera fc; fc.invView = InverseAffine(renderer.View()); fc.proj00 = renderer.Proj().m[0][0]; fc.proj11 = renderer.Proj().m[1][1];
    FogConfig cfg; cfg.sigmaT = 0.022f; cfg.maxDistance = 60.0f; cfg.anisotropy = 0.65f; cfg.ambient = {0.008f, 0.012f, 0.02f};
    uint64_t visCalls = 0;
    const VisibilityFn vis = [&](const Vec3& p) { ++visCalls; Rt::Ray r; r.origin = p; r.dir = toSun; r.tMax = 200.0f; return bvh.Occluded(r) ? 0.0f : 1.0f; };

    FogVolume vol; FogStats st;
    double t0 = NowMs(); BuildFog(cfg, fc, &sun, vis, {}, vol, &st); const double buildMs = NowMs() - t0;
    std::printf("  froxels %ux%ux%u: %llu KiB (RGBA16F), %llu raios de visibilidade, %.0f ms (CPU, 1 thread)\n", cfg.gridX, cfg.gridY, cfg.gridZ,
                (unsigned long long)(vol.Bytes() / 1024), (unsigned long long)st.sunVisibilityEvals, buildMs);

    // verdade: marcha fina por pixel (mesma visibilidade), 256 passos exponenciais até a profundidade do pixel
    auto truthMarch = [&](uint32_t px, uint32_t py, float depth, uint64_t& evals) {
        const float ndcX = (float(px) + 0.5f) / float(W) * 2 - 1, ndcY = 1 - (float(py) + 0.5f) / float(H) * 2;
        const Vec3 dv = FogViewDir(fc, ndcX, ndcY), wd = Normalize(fc.invView.TransformDirection(dv));
        const float rayLen = Length(dv);
        const int N = 256; Vec3 L{}; float T = 1.0f;
        for (int i = 0; i < N; ++i) {
            const float d0 = cfg.nearDistance * std::pow(depth / cfg.nearDistance, float(i) / N), d1 = cfg.nearDistance * std::pow(depth / cfg.nearDistance, float(i + 1) / N), dm = std::sqrt(d0 * d1);
            const Vec3 p = fc.invView.TransformPoint(dv * dm);
            const float sT = DensityAt(cfg, p), dt = (d1 - d0) * rayLen;
            ++evals;
            const Vec3 rad = cfg.ambient + sun.radiance * (HenyeyGreenstein(Dot(sun.toLight, wd), cfg.anisotropy) * vis(p));
            const Vec3 S = cfg.scatterAlbedo * sT * rad;
            const float e = std::exp(-sT * dt);
            L = L + S * (T * (1.0f - e) / sT); T *= e;
        }
        return FogSample{L, T};
    };

    // erro do froxel (24/32/64 slices) vs verdade, em pixels com geometria (a cada 5 px)
    for (uint32_t gz : {16u, 32u, 64u}) {
        FogConfig c2 = cfg; c2.gridZ = gz;
        FogVolume v2; BuildFog(c2, fc, &sun, vis, {}, v2);
        double errL = 0, errT = 0, sumL = 0; size_t n = 0; uint64_t evals = 0;
        for (uint32_t y = 2; y < H; y += 5) for (uint32_t x = 2; x < W; x += 5) {
            const size_t i = size_t(y) * W + x; if (!gb.hit[i]) continue;
            const float d = std::min(gb.viewDepth[i], cfg.maxDistance);
            const FogSample tr = truthMarch(x, y, d, evals), fx = SampleFog(v2, (float(x) + 0.5f) / W, (float(y) + 0.5f) / H, d);
            errL += std::fabs(Lum(fx.L) - Lum(tr.L)); sumL += Lum(tr.L); errT += std::fabs(fx.T - tr.T); ++n;
        }
        std::printf("  %2u slices: erro medio de L %.4f (%.1f%% da luz espalhada media %.3f), erro de T %.5f | %llu froxels vs %.0f avaliacoes/pixel da marcha fina\n", gz,
                    errL / double(n), 100.0 * errL / sumL, sumL / double(n), errT / double(n), (unsigned long long)(uint64_t(c2.gridX) * c2.gridY * gz),
                    double(evals) / double(n));
        if (gz == 32) { CHECK(errL / sumL < 0.15); CHECK(errT / double(n) < 0.01); }
    }
    // as sombras das esferas aparecem na neblina: com visibilidade real, L médio é menor que sem oclusão
    FogVolume open; BuildFog(cfg, fc, &sun, [](const Vec3&) { return 1.0f; }, {}, open);
    double lOcc = 0, lOpen = 0;
    for (size_t i = 0; i < vol.inscatter.size(); ++i) { lOcc += Lum(vol.inscatter[i]); lOpen += Lum(open.inscatter[i]); }
    std::printf("  luz espalhada total: com sombras %.1f | sem oclusao %.1f (%.0f%%)\n", lOcc, lOpen, 100.0 * lOcc / lOpen);
    CHECK(lOcc < 0.97 * lOpen && lOcc > 0.3 * lOpen);

    // ---- imagens ----
    auto compose = [&](const FogVolume* v, bool useFog) {
        std::vector<Vec3> img(size_t(W) * H);
        const Vec3 sunTint = sun.radiance * 0.07f;
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            Vec3 col; float depth = cfg.maxDistance;
            if (gb.hit[i]) {
                Rt::Ray r; r.origin = gb.worldPos[i] + gb.worldNormal[i] * 2e-3f; r.dir = toSun; r.tMax = 200.0f;
                const float ndl = std::max(0.0f, Dot(gb.worldNormal[i], toSun)), lit = bvh.Occluded(r) ? 0.0f : 1.0f;
                col = Vec3{0.55f, 0.55f, 0.58f} * (Vec3{0.10f, 0.12f, 0.16f} + sunTint * (ndl * lit));
                depth = std::min(gb.viewDepth[i], cfg.maxDistance);
            } else {
                const float t = float(y) / float(H);
                col = Vec3{0.25f, 0.32f, 0.5f} * (1.0f - t) + Vec3{0.8f, 0.7f, 0.55f} * t;      // céu
            }
            if (useFog) { const FogSample f = SampleFog(*v, (float(x) + 0.5f) / W, (float(y) + 0.5f) / H, depth); col = col * f.T + f.L; }
            img[i] = col;
        }
        return Rgb8(img, gb.hit, 0.9f);
    };
    if (!kOut.empty()) {
        WritePng((kOut + "/fog_none.png").c_str(), compose(nullptr, false), W, H);
        WritePng((kOut + "/fog_open.png").c_str(), compose(&open, true), W, H);
        WritePng((kOut + "/fog_shadowed.png").c_str(), compose(&vol, true), W, H);
    }
}

// ===========================================================================================================
// Probes de GI
// ===========================================================================================================
namespace {
// Verdade: E/pi em (p,n) integrando L_in com 4096 raios, mesmo modelo de sombreamento dos probes (sem rebatimentos extras).
Vec3 TruthIrradiance(const Rt::Bvh& bvh, const Rt::ProbeEnvironment& env, const Vec3& p, const Vec3& n, float maxDist) {
    static const std::vector<Vec3> dirs = Rt::FibonacciSphere(4096);
    Vec3 e{};
    for (const Vec3& d : dirs) {
        const float c = Dot(n, d); if (c <= 0.0f) continue;
        Rt::Ray r; r.origin = p + n * 0.02f; r.dir = d; r.tMax = maxDist;
        Rt::Hit h; Vec3 L;
        if (bvh.Intersect(r, h)) {
            const Rt::Triangle& t = (*env.triangles)[h.tri];
            Vec3 N = Normalize(Cross(t.v1 - t.v0, t.v2 - t.v0)); if (Dot(N, d) > 0) N = -N;
            const Vec3 hp = r.origin + d * h.t; Vec3 E{};
            const float ndl = Dot(N, env.sunToLight);
            if (ndl > 0) { Rt::Ray s; s.origin = hp + N * 0.02f; s.dir = env.sunToLight; s.tMax = maxDist; if (!bvh.Occluded(s)) E = env.sunIrradiance * (ndl / kPi); }
            L = env.albedo(h.tri) * E;
        } else L = env.sky(d);
        e = e + L * c;
    }
    return e * (4.0f * kPi / float(dirs.size()) / kPi);
}
double RelLum(const Vec3& a, const Vec3& b) { return std::fabs(Lum(a) - Lum(b)) / (Lum(b) + 0.02); }
}

static void TestProbeGrid() {
    using namespace Rt;
    Scene scene = MakeDemoScene();
    scene.planes.pop_back();                                      // ao ar livre: só chão e esferas
    const std::vector<Triangle> tris = TessellateScene(scene);
    Bvh bvh; bvh.Build(tris, BvhBuild::BinnedSah);
    const size_t sphereTris = scene.spheres.size() * 960;         // 32 segmentos: 32*(2*16-2) triângulos por esfera
    CHECK(tris.size() == sphereTris + 2);

    Ibl::ProceduralSky sky; sky.sunIntensity = 0.0f;              // o sol é tratado à parte (luz direta vem do CSM)
    ProbeEnvironment env;
    env.triangles = &tris;
    env.albedo = [&](uint32_t t) { return t < sphereTris ? Vec3{0.85f, 0.30f, 0.20f} : Vec3{0.55f, 0.55f, 0.55f}; };
    env.sky = [&](const Vec3& d) { return sky.Radiance(d); };
    env.sunToLight = Normalize(Vec3{0.45f, 0.75f, 0.35f});
    env.sunIrradiance = {6.0f, 5.5f, 4.5f};
    env.multiBounce = false;                                       // a verdade acima também é de 1 rebatimento

    ProbeGridDesc gd; gd.origin = {-12.0f, 0.5f, -26.0f}; gd.spacing = 2.0f; gd.nx = 13; gd.ny = 4; gd.nz = 14; gd.raysPerProbe = 64; gd.maxDistance = 80.0f; gd.invalidateRadius = 10.0f;
    ProbeGrid grid; grid.Init(gd);
    CHECK(grid.Count() == 13u * 4 * 14);

    // (A) convergência com histerese: muitos quadros de 64 raios aproximam a verdade (nos próprios probes, espaço livre)
    auto freeProbe = [&](uint32_t i) { const Vec3 p = grid.Position(i); for (const Sphere& sp : scene.spheres) if (Length(p - sp.center) < sp.radius + 0.4f) return false; return true; };
    std::vector<uint32_t> test; for (uint32_t i = 0; i < grid.Count(); i += 5) if (freeProbe(i)) test.push_back(i);
    DemoRng rng;
    std::vector<Vec3> normals; for (size_t k = 0; k < test.size(); ++k) normals.push_back(Normalize(Vec3{rng.Range(-1, 1), rng.Range(0.1f, 1), rng.Range(-1, 1)}));
    std::vector<Vec3> truth; for (size_t k = 0; k < test.size(); ++k) truth.push_back(TruthIrradiance(bvh, env, grid.Position(test[k]), normals[k], gd.maxDistance));
    auto errAtProbes = [&]() { double s = 0; for (size_t k = 0; k < test.size(); ++k) s += RelLum(grid.Irradiance(grid.Position(test[k]), normals[k], false), truth[k]); return s / double(test.size()); };
    double e1 = 0, e8 = 0, e64 = 0;
    double t0 = NowMs();
    for (int f = 1; f <= 64; ++f) { grid.Update(bvh, env); if (f == 1) e1 = errAtProbes(); if (f == 8) e8 = errAtProbes(); }
    e64 = errAtProbes();
    const double updMs = (NowMs() - t0) / 64.0;
    std::printf("  A) %zu probes em espaco livre vs verdade (4096 raios): erro relativo medio apos 1 / 8 / 64 quadros de 64 raios: %.3f / %.3f / %.3f\n", test.size(), e1, e8, e64);
    std::printf("     %u probes, %llu KiB (FP16), %.1f ms por atualizacao completa (%u raios, CPU 1 thread)\n", grid.Count(), (unsigned long long)(grid.Bytes() / 1024), updMs, grid.Count() * gd.raysPerProbe);
    CHECK(e8 < e1 && e64 < e8 * 1.05 && e64 < 0.08);

    // (B) entre probes: interpolação no espaço (pontos aleatórios acima do chão, longe das esferas)
    {
        double sum = 0, sumNoVis = 0; int n = 0;
        for (int k = 0; k < 150; ++k) {
            const Vec3 p{rng.Range(-10, 10), rng.Range(0.6f, 5.5f), rng.Range(-24, -2)};
            bool bad = false; for (const Sphere& sp : scene.spheres) if (Length(p - sp.center) < sp.radius + 0.5f) bad = true;
            if (bad) continue;
            const Vec3 nrm = Normalize(Vec3{rng.Range(-1, 1), rng.Range(0.1f, 1), rng.Range(-1, 1)});
            const Vec3 tr = TruthIrradiance(bvh, env, p, nrm, gd.maxDistance);
            sum += RelLum(grid.Irradiance(p, nrm, true), tr); sumNoVis += RelLum(grid.Irradiance(p, nrm, false), tr); ++n;
        }
        std::printf("  B) %d pontos entre probes (espacamento 2 m): erro relativo medio %.3f com visibilidade | %.3f so trilinear\n", n, sum / n, sumNoVis / n);
        CHECK(sum / n < 0.12);
    }

    // (C) orçamento: 4 fatias de 1/4 == 1 ciclo completo; custo por quadro = 1/4
    {
        ProbeGrid g2; g2.Init(gd);
        const uint64_t r0 = g2.Stats().raysCast;
        uint32_t total = 0; for (int i = 0; i < 4; ++i) total += g2.Update(bvh, env, (g2.Count() + 3) / 4);
        CHECK(total >= g2.Count() && g2.InvalidCount() == 0);
        std::printf("  C) orcamento: 4 quadros de %u probes cobrem os %u (%llu raios no total, %u por quadro)\n", (g2.Count() + 3) / 4, g2.Count(),
                    (unsigned long long)(g2.Stats().raysCast - r0), (g2.Count() + 3) / 4 * gd.raysPerProbe);
    }

    // (D) vazamento de luz através de uma parede fina: visibilidade por Chebyshev nos octantes
    {
        std::vector<Triangle> wall = {{{0, -2, -30}, {0, 30, -30}, {0, 30, 30}}, {{0, -2, -30}, {0, 30, 30}, {0, -2, 30}}};
        Bvh wb; wb.Build(wall, BvhBuild::BinnedSah);
        ProbeEnvironment we = env; we.triangles = &wall; we.albedo = [](uint32_t) { return Vec3{0.8f, 0.8f, 0.8f}; };
        we.sky = [](const Vec3&) { return Vec3{0, 0, 0}; };         // sem céu: só o sol iluminando a face +x
        we.sunToLight = Normalize(Vec3{1.0f, 0.3f, 0.0f}); we.sunIrradiance = {10, 10, 10};
        ProbeGridDesc wd; wd.origin = {-0.5f, 2.0f, -2.0f}; wd.spacing = 1.0f; wd.nx = 2; wd.ny = 3; wd.nz = 5; wd.raysPerProbe = 128; wd.maxDistance = 60.0f;
        ProbeGrid wg; wg.Init(wd);
        for (int f = 0; f < 30; ++f) wg.Update(wb, we);
        const Vec3 p{-0.15f, 3.0f, 0.0f}, up{0, 1, 0};              // lado escuro, perto da parede
        const Vec3 tr = TruthIrradiance(wb, we, p, up, 60.0f);
        const double withVis = Lum(wg.Irradiance(p, up, true)), noVis = Lum(wg.Irradiance(p, up, false));
        std::printf("  D) vazamento atraves da parede: verdade %.4f | com visibilidade %.4f | so trilinear %.4f\n", Lum(tr), withVis, noVis);
        CHECK(noVis > 4.0 * (withVis + 0.01));                      // sem visibilidade vaza muito mais luz
        CHECK(withVis < 0.35 * noVis);
    }

    // (E) objeto movido: invalidação regional re-converge mais rápido que esperar a histerese
    {
        Scene moved = scene; const size_t idx = 10;
        const Sphere old = moved.spheres[idx];
        Aabb box; box.Grow(old.center - Vec3{old.radius, old.radius, old.radius}); box.Grow(old.center + Vec3{old.radius, old.radius, old.radius});
        moved.spheres[idx].center = moved.spheres[idx].center + Vec3{2.0f, 0.0f, -1.0f};
        const Sphere nw = moved.spheres[idx];
        box.Grow(nw.center - Vec3{nw.radius, nw.radius, nw.radius}); box.Grow(nw.center + Vec3{nw.radius, nw.radius, nw.radius});
        const std::vector<Triangle> tris2 = TessellateScene(moved);
        Bvh b2; b2.Build(tris2, BvhBuild::BinnedSah);
        ProbeEnvironment e2 = env; e2.triangles = &tris2;
        ProbeGrid slow = grid, fast = grid; (void)0;                          // grid está convergida para a cena antiga
        // probes de teste perto do objeto (dentro de 4 m da nova posição)
        std::vector<uint32_t> near; for (uint32_t i = 0; i < grid.Count(); ++i) { const Vec3 p = grid.Position(i); if (freeProbe(i) && Length(p - nw.center) < 4.0f) near.push_back(i); }
        std::vector<Vec3> nrm2, tr2; DemoRng r2;
        for (size_t k = 0; k < near.size(); ++k) { nrm2.push_back(Normalize(Vec3{r2.Range(-1, 1), r2.Range(0.1f, 1), r2.Range(-1, 1)})); tr2.push_back(TruthIrradiance(b2, e2, grid.Position(near[k]), nrm2[k], gd.maxDistance)); }
        auto err = [&](const ProbeGrid& g) { double s = 0; for (size_t k = 0; k < near.size(); ++k) s += RelLum(g.Irradiance(g.Position(near[k]), nrm2[k], false), tr2[k]); return s / double(std::max<size_t>(near.size(), 1)); };
        const double before = err(grid);
        const uint32_t inv = fast.InvalidateRegion(box);   // gd.invalidateRadius = 10 m
        for (int f = 0; f < 3; ++f) { slow.Update(b2, e2); fast.Update(b2, e2); }
        const double eSlow = err(slow), eFast = err(fast);
        std::printf("  E) objeto movido: %u de %u probes invalidados (%.0f%%), %zu probes de teste | erro antes %.3f -> apos 3 quadros: so histerese %.3f vs invalidacao %.3f\n", inv,
                    grid.Count(), 100.0 * double(inv) / double(grid.Count()), near.size(), before, eSlow, eFast);
        uint32_t expected = 0;
        for (uint32_t i = 0; i < grid.Count(); ++i) { const Vec3 q = grid.Position(i); const Aabb r = box.Expanded(gd.invalidateRadius); if (q.x >= r.lo.x && q.x <= r.hi.x && q.y >= r.lo.y && q.y <= r.hi.y && q.z >= r.lo.z && q.z <= r.hi.z) ++expected; }
        CHECK(near.size() > 5 && inv > 0 && inv == expected && inv < grid.Count());
        CHECK(eFast <= eSlow);
    }

    // (F) imagem: GI dos probes (com multi-bounce convergido) vs verdade 1 rebatimento, em 160x90
    {
        const uint32_t W = 160, H = 90;
        const CameraSetup cam = MakeDemoCamera(W, H);
        SoftRenderer renderer(scene, cam);
        const GBufferView gb = renderer.ExportGBuffer();
        ProbeEnvironment mb = env; mb.multiBounce = true;
        ProbeGrid g3; g3.Init(gd);
        for (int f = 0; f < 40; ++f) g3.Update(bvh, mb);
        std::vector<Vec3> imgP(W * H), imgT(W * H), imgD(W * H);
        double e = 0; size_t n = 0;
        t0 = NowMs();
        for (size_t i = 0; i < imgP.size(); ++i) {
            if (!gb.hit[i]) { imgP[i] = imgT[i] = imgD[i] = Vec3{0.15f, 0.2f, 0.35f}; continue; }
            const Vec3 pos = gb.worldPos[i] + gb.worldNormal[i] * 0.05f, nrm = gb.worldNormal[i];
            const Vec3 alb = gb.worldPos[i].y < 0.01f ? Vec3{0.55f, 0.55f, 0.55f} : Vec3{0.85f, 0.30f, 0.20f};
            imgP[i] = alb * g3.Irradiance(pos, nrm, true);
            imgD[i] = alb * 0.0f;
        }
        const double probeMs = NowMs() - t0;
        t0 = NowMs();
        for (size_t i = 0; i < imgT.size(); ++i) if (gb.hit[i]) {
            const Vec3 alb = gb.worldPos[i].y < 0.01f ? Vec3{0.55f, 0.55f, 0.55f} : Vec3{0.85f, 0.30f, 0.20f};
            imgT[i] = alb * TruthIrradiance(bvh, env, gb.worldPos[i] + gb.worldNormal[i] * 0.05f, gb.worldNormal[i], gd.maxDistance);
            e += std::fabs(Lum(imgP[i]) - Lum(imgT[i])); n += 1;
        }
        const double truthMs = NowMs() - t0;
        std::printf("  F) imagem %ux%u: %.2f us/pixel com probes vs %.0f us/pixel com 4096 raios (%.0fx); diferenca media de luminancia %.4f (probes incluem multi-bounce, a verdade nao)\n", W, H,
                    probeMs * 1e3 / double(W * H), truthMs * 1e3 / double(W * H), truthMs / probeMs, e / double(n));
        if (!kOut.empty()) {
            std::vector<uint8_t> hit(W * H, 1);
            WritePng((kOut + "/gi_probes.png").c_str(), Rgb8(imgP, hit, 1.4f), W, H);
            WritePng((kOut + "/gi_truth.png").c_str(), Rgb8(imgT, hit, 1.4f), W, H);
        }
    }
}

int main() {
    std::printf("[VSM: penumbra e bordas]\n");           TestVsmSynthetic();
    std::printf("[VSM: acne]\n");                       TestVsmAcne();
    std::printf("[VSM: limite superior de Chebyshev]\n"); TestVsmChebyshevBound();
    std::printf("[VSM: vazamento de luz]\n");            TestVsmBleeding();
    std::printf("[VSM: ponta a ponta (CSM)]\n");         TestVsmEndToEnd();
    std::printf("[SDF: unidades]\n");                   TestSdfUnits();
    std::printf("[SDF: sombras suaves]\n");             TestSdfShadows();
    std::printf("[Neblina: matematica]\n");            TestFogMath();
    std::printf("[Neblina: cena com sombras]\n");        TestFogScene();
    std::printf("[Probes de GI]\n");                    TestProbeGrid();
    if (g_failures) { std::printf("%d FALHA(S)\n", g_failures); return 1; }
    std::printf("OK\n");
    return 0;
}
