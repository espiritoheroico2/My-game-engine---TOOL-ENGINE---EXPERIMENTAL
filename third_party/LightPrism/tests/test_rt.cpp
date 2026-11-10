// Testes do ray tracing por software (Fase 5b): BVH (SAH binned e LBVH), limite de distância, Refit, efeitos RT.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "LightPrism/Rt/Bvh.hpp"
#include "LightPrism/Rt/RtEffects.hpp"
#include "LightPrism/Rt/Transfer.hpp"
#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Soft/Png.hpp"
#include "LightPrism/Soft/SceneMesh.hpp"

using namespace LightPrism;
using namespace LightPrism::Rt;
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

std::vector<Triangle> RandomSoup(uint32_t n, DemoRng& rng, float world = 20.0f, float size = 1.2f) {
    std::vector<Triangle> t(n);
    for (auto& tr : t) {
        const Vec3 c{rng.Range(-world, world), rng.Range(-world, world), rng.Range(-world, world)};
        auto off = [&] { return Vec3{rng.Range(-size, size), rng.Range(-size, size), rng.Range(-size, size)}; };
        tr = {c + off(), c + off(), c + off()};
    }
    return t;
}

Ray RandomRay(DemoRng& rng, float tMax) {
    Ray r;
    r.origin = {rng.Range(-24, 24), rng.Range(-24, 24), rng.Range(-24, 24)};
    r.dir = Normalize({rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1)});
    r.tMax = tMax;
    return r;
}

struct Agree { uint32_t rays = 0, hits = 0, mismatchT = 0, mismatchOcc = 0; };

Agree CompareWithBrute(const Bvh& bvh, const std::vector<Triangle>& tris, DemoRng& rng, uint32_t nRays, float tMaxLo, float tMaxHi) {
    Agree a;
    for (uint32_t i = 0; i < nRays; ++i) {
        const Ray r = RandomRay(rng, rng.Range(tMaxLo, tMaxHi));
        Hit h1, h2;
        const bool b1 = bvh.Intersect(r, h1), b2 = IntersectBruteForce(tris, r, h2);
        ++a.rays;
        if (b1 != b2 || (b1 && std::fabs(h1.t - h2.t) > 1e-4f * (1.0f + h2.t))) ++a.mismatchT;
        if (b1 && h1.t > r.tMax) ++a.mismatchT;                      // nunca devolve além do limite
        if (b1) ++a.hits;
        if (bvh.Occluded(r) != b2) ++a.mismatchOcc;
    }
    return a;
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

// Mapa de calor (preto -> laranja -> branco) normalizado por `maxv`.
std::vector<uint8_t> Heat(const std::vector<float>& v, const std::vector<uint8_t>& hit, float maxv) {
    std::vector<uint8_t> rgb(v.size() * 3, 0);
    for (size_t i = 0; i < v.size(); ++i) {
        if (!hit[i]) continue;
        const float t = Saturate(v[i] / maxv);
        rgb[3 * i] = uint8_t(255.0f * Saturate(t * 2.0f));
        rgb[3 * i + 1] = uint8_t(255.0f * Saturate(t * 2.0f - 0.7f) );
        rgb[3 * i + 2] = uint8_t(255.0f * Saturate(t * 4.0f - 3.0f));
    }
    return rgb;
}

} // namespace

static void TestSoupCorrectness() {
    DemoRng rng;
    const auto tris = RandomSoup(2000, rng);
    for (BvhBuild mode : {BvhBuild::BinnedSah, BvhBuild::Lbvh}) {
        Bvh bvh; bvh.Build(tris, mode);
        const char* nm = mode == BvhBuild::BinnedSah ? "SAH" : "LBVH";
        const Agree inf = CompareWithBrute(bvh, tris, rng, 4000, 1e30f, 1e30f);
        const Agree lim = CompareWithBrute(bvh, tris, rng, 4000, 0.5f, 12.0f);
        std::printf("  %-4s: %zu nos, prof %u, SAH %.1f, %llu KiB | raios infinitos: %u hits, %u erros | com limite: %u hits, %u erros\n", nm,
                    bvh.Nodes().size(), bvh.MaxDepth(), bvh.SahCost(), (unsigned long long)(bvh.MemoryBytes() / 1024), inf.hits,
                    inf.mismatchT + inf.mismatchOcc, lim.hits, lim.mismatchT + lim.mismatchOcc);
        CHECK(inf.hits > 400 && lim.hits > 100);
        CHECK(inf.mismatchT == 0 && inf.mismatchOcc == 0);
        CHECK(lim.mismatchT == 0 && lim.mismatchOcc == 0);
        // indices originais consistentes
        std::vector<uint8_t> seen(tris.size(), 0);
        for (uint32_t id : bvh.LeafOrderIndices()) { CHECK(id < tris.size()); if (id < tris.size()) ++seen[id]; }
        bool all = true; for (uint8_t s : seen) all = all && s == 1;
        CHECK(all);
    }
}

static void TestDegenerate() {
    Bvh e; e.Build({});
    Hit h; Ray r; r.origin = {0, 0, 0}; r.dir = {0, 0, 1};
    CHECK(e.Empty() && !e.Intersect(r, h) && !e.Occluded(r));

    std::vector<Triangle> one{{{-1, -1, 5}, {1, -1, 5}, {0, 1, 5}}};
    for (BvhBuild m : {BvhBuild::BinnedSah, BvhBuild::Lbvh}) {
        Bvh b; b.Build(one, m);
        CHECK(b.Intersect(r, h) && std::fabs(h.t - 5.0f) < 1e-4f && h.tri == 0);
        r.tMax = 4.9f; CHECK(!b.Intersect(r, h) && !b.Occluded(r));   // limite de distância corta o triângulo
        r.tMax = 1e30f;
        r.dir = {0, 0, -1}; CHECK(b.Intersect(r, h) == false);        // atrás
        r.dir = {0, 0, 1};
    }
    // 500 triângulos IDÊNTICOS (códigos de Morton iguais) e eixos alinhados (dir com zeros): não pode travar nem perder hit.
    std::vector<Triangle> same(500, one[0]);
    for (BvhBuild m : {BvhBuild::BinnedSah, BvhBuild::Lbvh}) {
        Bvh b; b.Build(same, m);
        CHECK(b.Intersect(r, h) && std::fabs(h.t - 5.0f) < 1e-4f);
        CHECK(b.MaxDepth() < 60);
    }
    // triângulo degenerado (área zero) não gera hit
    std::vector<Triangle> deg{{{0, 0, 3}, {1, 1, 3}, {2, 2, 3}}};
    Bvh bd; bd.Build(deg);
    CHECK(!bd.Intersect(r, h));
}

static void TestQualityAndSpeed() {
    DemoRng rng;
    const auto tris = RandomSoup(20000, rng, 30.0f, 0.8f);
    Bvh sah, lbvh;
    double t0 = NowMs(); sah.Build(tris, BvhBuild::BinnedSah); const double tSah = NowMs() - t0;
    t0 = NowMs(); lbvh.Build(tris, BvhBuild::Lbvh); const double tLbvh = NowMs() - t0;
    std::printf("  20k triangulos: build SAH %.1f ms (custo %.1f) | LBVH %.1f ms (custo %.1f)\n", tSah, sah.SahCost(), tLbvh, lbvh.SahCost());
    CHECK(sah.SahCost() <= lbvh.SahCost() * 1.02);   // SAH deve ser pelo menos tão bom quanto o LBVH
    CHECK(tLbvh < tSah * 1.5);                       // LBVH existe para construir rápido

    // Limite de distância: nós visitados e tempo por raio. Raios saindo de pontos aleatórios, curtos vs infinitos.
    for (BvhBuild mode : {BvhBuild::BinnedSah, BvhBuild::Lbvh}) {
        const Bvh& b = mode == BvhBuild::BinnedSah ? sah : lbvh;
        TraversalStats sInf, sShort;
        double msInf = 0, msShort = 0;
        for (int pass = 0; pass < 2; ++pass) {
            DemoRng rr; TraversalStats& st = pass ? sShort : sInf;
            const double a = NowMs();
            for (int i = 0; i < 30000; ++i) {
                Ray r = RandomRay(rr, pass ? 1.5f : 1e30f);
                Hit h; b.Intersect(r, h, &st);
            }
            (pass ? msShort : msInf) = NowMs() - a;
        }
        std::printf("  %s: nos/raio infinito %.1f, limite 1.5 m %.1f (-%.0f%%) | %.2f vs %.2f Mraios/s\n", mode == BvhBuild::BinnedSah ? "SAH " : "LBVH",
                    double(sInf.nodesVisited) / 30000.0, double(sShort.nodesVisited) / 30000.0,
                    100.0 * (1.0 - double(sShort.nodesVisited) / double(sInf.nodesVisited)), 0.03 / (msInf / 1000.0), 0.03 / (msShort / 1000.0));
        CHECK(sShort.nodesVisited * 2 < sInf.nodesVisited);   // o limite tem de cortar pelo menos metade do trabalho
    }
}

static void TestRefit() {
    DemoRng rng;
    auto tris = RandomSoup(3000, rng);
    for (BvhBuild mode : {BvhBuild::BinnedSah, BvhBuild::Lbvh}) {
        Bvh bvh; bvh.Build(tris, mode);
        const float costBefore = bvh.SahCost();
        auto moved = tris;
        for (size_t i = 0; i < moved.size(); i += 3) {                 // 1/3 dos triângulos se movem 2 m
            const Vec3 d{2.0f, -1.0f, 0.5f};
            moved[i] = {moved[i].v0 + d, moved[i].v1 + d, moved[i].v2 + d};
        }
        bvh.Refit(moved);
        const Agree a = CompareWithBrute(bvh, moved, rng, 3000, 1e30f, 1e30f);
        std::printf("  Refit %s: %u hits, %u erros, custo SAH %.1f -> %.1f\n", mode == BvhBuild::BinnedSah ? "SAH " : "LBVH", a.hits,
                    a.mismatchT + a.mismatchOcc, costBefore, bvh.SahCost());
        CHECK(a.mismatchT == 0 && a.mismatchOcc == 0 && a.hits > 300);
        // Refit é exato: mesma resposta que reconstruir do zero (mesmos hits t)
        Bvh fresh; fresh.Build(moved, mode);
        uint32_t diff = 0;
        for (int i = 0; i < 1500; ++i) {
            const Ray r = RandomRay(rng, 1e30f); Hit h1, h2;
            const bool x = bvh.Intersect(r, h1), y = fresh.Intersect(r, h2);
            if (x != y || (x && std::fabs(h1.t - h2.t) > 1e-4f)) ++diff;
        }
        CHECK(diff == 0);
    }
}

// Cena do demo: BVH tesselado reproduz o AO analítico (esferas e planos exatos) dentro de um erro pequeno.
static void TestEffectsAndImages() {
    const uint32_t W = 480, H = 270;
    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer r(scene, cam);
    const GBufferView gb = r.ExportGBuffer();
    const auto tris = TessellateScene(scene);
    Bvh bvh; bvh.Build(tris, BvhBuild::BinnedSah);
    std::printf("  cena demo: %zu triangulos, BVH %llu KiB, SAH %.1f\n", tris.size(), (unsigned long long)(bvh.MemoryBytes() / 1024), bvh.SahCost());

    // --- AO: RT vs verdade analítica (mesmo estimador) ---
    RtAoConfig ao; ao.radius = 0.8f; ao.rays = 32;
    std::vector<float> aoRt(W * H, 1.0f), truth, work(W * H, 0.0f), workInf(W * H, 0.0f);
    r.ComputeRayTracedAO(ao.radius, 256, truth);
    TraversalStats st; const double t0 = NowMs();
    for (size_t i = 0; i < aoRt.size(); ++i) if (gb.hit[i]) {
        TraversalStats one;
        aoRt[i] = RtAmbientOcclusion(bvh, gb.worldPos[i], gb.worldNormal[i], ao, uint32_t(i), &one);
        work[i] = float(one.nodesVisited) / float(ao.rays);
        st.nodesVisited += one.nodesVisited; st.rays += one.rays; st.triTests += one.triTests;
    }
    const double aoMs = NowMs() - t0;
    double err = 0, occErr = 0; size_t n = 0, occN = 0;
    for (size_t i = 0; i < aoRt.size(); ++i) if (gb.hit[i]) {
        const double d = std::fabs(aoRt[i] - truth[i]); err += d; ++n;
        if (truth[i] < 0.9f) { occErr += d; ++occN; }
    }
    std::printf("  AO RT (%u raios, raio %.1f m): erro medio %.4f, ocluidos %zu px erro %.4f | %.0f ms (%.1f nos/raio)\n", ao.rays, ao.radius,
                err / double(n), occN, occErr / double(std::max<size_t>(occN, 1)), aoMs, double(st.nodesVisited) / double(st.rays));
    CHECK(err / double(n) < 0.02);
    CHECK(occN > 500 && occErr / double(occN) < 0.06);

    // Nos visitados por raio com AO de raio infinito (controle): mostra o que o limite economiza.
    RtAoConfig aoInf = ao; aoInf.radius = 1e4f;   // t/radius ~ 0: só interessa o custo
    TraversalStats stInf;
    for (size_t i = 0; i < aoRt.size(); ++i) if (gb.hit[i]) {
        TraversalStats one;
        RtAmbientOcclusion(bvh, gb.worldPos[i], gb.worldNormal[i], aoInf, uint32_t(i), &one);
        workInf[i] = float(one.nodesVisited) / float(aoInf.rays);
        stInf.nodesVisited += one.nodesVisited; stInf.rays += one.rays;
    }
    std::printf("  custo do raio: limite %.1f m = %.1f nos/raio | sem limite = %.1f nos/raio (%.1fx mais)\n", ao.radius,
                double(st.nodesVisited) / double(st.rays), double(stInf.nodesVisited) / double(stInf.rays),
                double(stInf.nodesVisited) / double(st.nodesVisited));
    CHECK(stInf.nodesVisited > st.nodesVisited * 1.1);   // na cena demo (esparsa) o ganho e menor que na sopa de triangulos

    // --- Sombra do sol: dura, suave, e limitada a 1.5 m (sol baixo = sombras longas) ---
    const Vec3 toSun = Normalize({0.85f, 0.30f, 0.15f});
    auto shade = [&](const RtShadowConfig& c, uint64_t* nodes) {
        std::vector<float> img(W * H, 0.0f);
        for (size_t i = 0; i < img.size(); ++i) if (gb.hit[i]) {
            TraversalStats one;
            const float vis = RtSunVisibility(bvh, gb.worldPos[i], gb.worldNormal[i], toSun, c, uint32_t(i), &one);
            const float ndl = std::max(0.0f, Dot(gb.worldNormal[i], toSun));
            img[i] = 0.08f + 0.92f * ndl * vis;
            if (nodes) *nodes += one.nodesVisited;
        }
        return img;
    };
    RtShadowConfig hard; hard.maxDistance = 200.0f;
    RtShadowConfig soft = hard; soft.coneHalfAngle = 0.05f; soft.samples = 16;
    RtShadowConfig lim = hard; lim.maxDistance = 1.5f;
    uint64_t nHard = 0, nLim = 0;
    const auto imgHard = shade(hard, &nHard), imgSoft = shade(soft, nullptr), imgLim = shade(lim, &nLim);
    size_t shadowed = 0, shadowedLim = 0, total = 0, softPx = 0;
    for (size_t i = 0; i < imgHard.size(); ++i) if (gb.hit[i] && Dot(gb.worldNormal[i], toSun) > 0.05f) {
        ++total; if (imgHard[i] < 0.09f + 0.0f) ++shadowed; if (imgLim[i] < 0.09f) ++shadowedLim;
        if (imgSoft[i] > 0.12f && imgSoft[i] < 0.9f * std::max(0.0f, Dot(gb.worldNormal[i], toSun)) + 0.05f) ++softPx;
    }
    std::printf("  sol: %.1f%% sombreado (duro, 200 m) | %.1f%% com limite 1.5 m | nos %llu -> %llu (-%.0f%%) | %zu px com penumbra\n",
                100.0 * double(shadowed) / double(total), 100.0 * double(shadowedLim) / double(total), (unsigned long long)nHard,
                (unsigned long long)nLim, 100.0 * (1.0 - double(nLim) / double(nHard)), softPx);
    CHECK(shadowed > total / 20);                    // há sombra de verdade
    CHECK(shadowedLim < shadowed);                   // o limite remove sombras longas
    CHECK(shadowedLim > shadowed / 8);               // ...mas mantém as de contato
    CHECK(nLim <= nHard);
    CHECK(softPx > 200);
    {   // a sombra suave concorda em média com a dura (pega visibilidade invertida)
        double dsum = 0; size_t dn = 0;
        for (size_t i = 0; i < imgHard.size(); ++i) if (gb.hit[i]) { dsum += std::fabs(imgSoft[i] - imgHard[i]); ++dn; }
        CHECK(dsum / double(dn) < 0.02);
    }

    const std::string dir = LP_TEST_OUTPUT_DIR;
    if (!dir.empty()) {
        WritePng((dir + "/rt_ao.png").c_str(), Gray(aoRt, gb.hit), W, H);
        WritePng((dir + "/rt_ao_truth.png").c_str(), Gray(truth, gb.hit), W, H);
        WritePng((dir + "/rt_sun_hard.png").c_str(), Gray(imgHard, gb.hit), W, H);
        WritePng((dir + "/rt_sun_soft.png").c_str(), Gray(imgSoft, gb.hit), W, H);
        WritePng((dir + "/rt_sun_limit1m5.png").c_str(), Gray(imgLim, gb.hit), W, H);
        float mx = 1; for (float v : workInf) mx = std::max(mx, v);
        WritePng((dir + "/rt_cost_limited.png").c_str(), Heat(work, gb.hit, mx), W, H);
        WritePng((dir + "/rt_cost_unlimited.png").c_str(), Heat(workInf, gb.hit, mx), W, H);
    }
}

// ---------------------------------------------------------------------------------------------
// PRT: ray tracing com cache
// ---------------------------------------------------------------------------------------------
namespace {

Vec3 EvalRadianceSh(const Ibl::Sh9& L, const Vec3& w) {   // L'(w) = sum L_k Y_k(w), sem clamp (a verdade é a mesma função linear)
    float y[9]; Ibl::ShBasis(w, y);
    Vec3 r{};
    for (int k = 0; k < 9; ++k) r = r + L.c[k] * y[k];
    return r;
}

// Verdade independente: E/pi = (1/pi) * integral V(w) L'(w) cos+ dw com um conjunto DIFERENTE de direções (4096).
Vec3 BruteIrradiance(const Bvh& bvh, const TransferSample& s, const TransferConfig& c, const Ibl::Sh9& L) {
    static const std::vector<Vec3> dirs = FibonacciSphere(4096);
    Vec3 e{};
    Ray r; r.origin = s.position + s.normal * c.bias; r.tMax = c.maxDistance;
    for (const Vec3& w : dirs) {
        const float cosT = Dot(s.normal, w);
        if (cosT <= 0.0f) continue;
        r.dir = w;
        if (!bvh.Empty() && bvh.Occluded(r)) continue;
        e = e + EvalRadianceSh(L, w) * cosT;
    }
    return e * (4.0f * kPi / float(dirs.size()) / kPi);
}

std::vector<TransferSample> MakeSamples(const Scene& scene, float spacing) {
    std::vector<TransferSample> out;
    for (float z = -26.0f; z <= 0.0f; z += spacing)
        for (float x = -12.0f; x <= 12.0f; x += spacing) out.push_back({{x, 0.0f, z}, {0, 1, 0}});
    const auto dirs = FibonacciSphere(48);
    for (const Sphere& sp : scene.spheres)
        for (const Vec3& d : dirs) out.push_back({sp.center + d * sp.radius, d});
    return out;
}

double RelErr(const Vec3& a, const Vec3& b) {
    return (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z)) / (std::fabs(b.x) + std::fabs(b.y) + std::fabs(b.z) + 0.05);
}

Aabb SphereBox(const Sphere& s) {
    Aabb b; b.Grow(s.center - Vec3{s.radius, s.radius, s.radius}); b.Grow(s.center + Vec3{s.radius, s.radius, s.radius});
    return b;
}

std::vector<uint8_t> Upscale(const std::vector<uint8_t>& rgb, uint32_t w, uint32_t h, uint32_t k) {
    std::vector<uint8_t> o(size_t(w) * h * k * k * 3);
    for (uint32_t y = 0; y < h * k; ++y) for (uint32_t x = 0; x < w * k; ++x)
        for (int c = 0; c < 3; ++c) o[(size_t(y) * w * k + x) * 3 + c] = rgb[(size_t(y / k) * w + x / k) * 3 + c];
    return o;
}

} // namespace

static void TestTransferCache() {
    Ibl::ProceduralSky noon; noon.sunDir = Normalize({0.2f, 0.9f, 0.1f}); noon.sunIntensity = 2.0f; noon.sunPower = 8.0f;
    Ibl::ProceduralSky dusk = noon; dusk.sunDir = Normalize({0.9f, 0.12f, 0.3f}); dusk.sunColor = {1.0f, 0.55f, 0.3f};
    dusk.zenith = {0.08f, 0.10f, 0.30f};
    auto proj = [](const Ibl::ProceduralSky& sky) { return Ibl::ProjectToSh([&](const Vec3& d) { return sky.Radiance(d); }, 64); };
    const Ibl::Sh9 Ln = proj(noon), Ld = proj(dusk);

    // (A) sem geometria: o vetor de transferência tem de reproduzir o SH já convoluído do IBL.
    {
        Bvh empty;
        TransferConfig cfg; cfg.rays = 512;
        std::vector<TransferSample> ss{{{0, 0, 0}, {0, 1, 0}}, {{0, 0, 0}, Normalize({1, 0.3f, -0.4f})}, {{0, 0, 0}, {0, -1, 0}}, {{0, 0, 0}, {0, 0, 1}}};
        TransferCache tc; tc.Init(ss, cfg); tc.Bake(empty);
        const Ibl::Sh9 conv = Ibl::ConvolveCosine(Ln);
        double worst = 0;
        for (uint32_t i = 0; i < tc.Count(); ++i) worst = std::max(worst, RelErr(tc.Irradiance(i, Ln), Ibl::EvalSh(conv, ss[i].normal)));
        std::printf("  A) sem oclusao: transferencia vs IBL convoluido, erro relativo max %.4f\n", worst);
        CHECK(worst < 0.02);
    }

    const Scene scene = MakeDemoScene();
    auto tris = TessellateScene(scene);
    Bvh bvh; bvh.Build(tris, BvhBuild::BinnedSah);
    TransferConfig cfg; cfg.maxDistance = 3.0f; cfg.rays = 256;
    const auto samples = MakeSamples(scene, 0.4f);
    TransferCache tc; tc.Init(samples, cfg);
    double t0 = NowMs();
    const uint32_t baked = tc.Bake(bvh);
    const double bakeMs = NowMs() - t0;
    const TransferStats fullStats = tc.Stats();
    std::printf("  bake completo: %u amostras, %llu raios, %.0f ms (%.2f Mraios/s), cache %llu KiB (%.0f B/amostra)\n", baked,
                (unsigned long long)fullStats.raysCast, bakeMs, double(fullStats.raysCast) / bakeMs / 1000.0,
                (unsigned long long)(fullStats.bytes / 1024), double(fullStats.bytes) / double(fullStats.samples));
    CHECK(baked == samples.size() && tc.Stats().dirty == 0);

    // (B) cena ocluída vs verdade independente (4096 raios), para dois céus
    {
        double worstN = 0, sumN = 0, worstD = 0; uint32_t n = 0;
        for (uint32_t i = 0; i < tc.Count(); i += 7) {
            const Vec3 en = tc.Irradiance(i, Ln), ed = tc.Irradiance(i, Ld);
            const double a = RelErr(en, BruteIrradiance(bvh, samples[i], cfg, Ln)), b = RelErr(ed, BruteIrradiance(bvh, samples[i], cfg, Ld));
            worstN = std::max(worstN, a); sumN += a; worstD = std::max(worstD, b); ++n;
        }
        std::printf("  B) %u amostras vs forca bruta: erro relativo medio %.4f, max %.4f (meio-dia) | max %.4f (entardecer)\n", n, sumN / double(n), worstN, worstD);
        CHECK(worstN < 0.12 && worstD < 0.12 && sumN / double(n) < 0.015);   // max = amostras em bordas de contato (256 raios binarios vs 4096)
    }

    // (C) trocar o céu NÃO custa raios
    {
        const uint64_t before = tc.Stats().raysCast;
        Vec3 acc{};
        t0 = NowMs();
        for (int pass = 0; pass < 20; ++pass) for (uint32_t i = 0; i < tc.Count(); ++i) acc = acc + tc.Irradiance(i, pass & 1 ? Ln : Ld);
        const double ms = NowMs() - t0;
        std::printf("  C) 20 trocas de ceu x %u amostras: %.2f ms (%.1f ns/amostra), raios novos: %llu (soma %.1f)\n", tc.Count(), ms,
                    ms * 1e6 / (20.0 * tc.Count()), (unsigned long long)(tc.Stats().raysCast - before), double(acc.x));
        CHECK(tc.Stats().raysCast == before);
    }

    // (D) mover um objeto: invalidação regional + re-bake incremental == bake do zero (bit a bit)
    {
        Scene moved = scene;
        const size_t idx = 10;
        const Aabb oldBox = SphereBox(moved.spheres[idx]);
        moved.spheres[idx].center = moved.spheres[idx].center + Vec3{1.4f, 0.0f, -0.8f};
        const Aabb newBox = SphereBox(moved.spheres[idx]);
        const auto movedTris = TessellateScene(moved);
        CHECK(movedTris.size() == tris.size());
        bvh.Refit(movedTris);

        const auto& samples2 = samples;   // amostras fixas: o teste compara apenas a geometria nova
        const uint64_t raysBefore = tc.Stats().raysCast;
        const uint32_t d1 = tc.InvalidateRegion(oldBox), d2 = tc.InvalidateRegion(newBox);
        t0 = NowMs();
        const uint32_t rebaked = tc.Bake(bvh);
        const double incMs = NowMs() - t0;
        const uint64_t incRays = tc.Stats().raysCast - raysBefore;

        TransferCache fresh; fresh.Init(samples2, cfg);
        t0 = NowMs(); fresh.Bake(bvh); const double freshMs = NowMs() - t0;
        uint32_t differ = 0;
        for (uint32_t i = 0; i < tc.Count(); ++i)
            for (int k = 0; k < 9; ++k) if (tc.Transfer(i)[k] != fresh.Transfer(i)[k]) { ++differ; break; }
        std::printf("  D) objeto movido: %u+%u amostras sujas de %u (%.1f%%), re-bake %u amostras %.0f ms (%llu raios) vs do zero %.0f ms -> %.1fx menos trabalho | amostras diferentes do bake do zero: %u\n",
                    d1, d2, tc.Count(), 100.0 * double(rebaked) / double(tc.Count()), rebaked, incMs, (unsigned long long)incRays, freshMs,
                    freshMs / std::max(incMs, 1e-3), differ);
        CHECK(differ == 0);                                       // exatidão do cache incremental
        CHECK(rebaked > 0 && rebaked < tc.Count() / 4);           // só uma fração pequena é refeita
        CHECK(tc.Stats().dirty == 0);

        // orçamento por quadro: marcar tudo e processar em fatias de 500 amostras termina em ceil(N/500) quadros
        tc.InvalidateAll();
        CHECK(tc.Bake(bvh, 500) == 500);   // exatamente o orçamento (nem 501)
        uint32_t frames = 1;
        while (tc.Stats().dirty > 0) { tc.Bake(bvh, 500); ++frames; }
        CHECK(frames == (tc.Count() + 499) / 500);
        std::printf("     orcamento 500 amostras/quadro: bake completo diluido em %u quadros\n", frames);
    }

    // (E) imagens: visão de cima do chão — irradiância do céu (meio-dia vs entardecer) e amostras sujas após mover um objeto
    {
        const std::string dir = LP_TEST_OUTPUT_DIR;
        if (!dir.empty()) {
            const uint32_t gw = uint32_t(24.0f / 0.4f) + 1, gh = uint32_t(26.0f / 0.4f) + 1;
            auto luma = [&](const Ibl::Sh9& L) {
                std::vector<float> v(size_t(gw) * gh); std::vector<uint8_t> hit(v.size(), 1);
                for (size_t i = 0; i < v.size(); ++i) { const Vec3 e = tc.Irradiance(uint32_t(i), L); v[i] = 0.3f * (e.x * 0.2126f + e.y * 0.7152f + e.z * 0.0722f) / 0.3f; }
                return Gray(v, hit);
            };
            WritePng((dir + "/prt_noon.png").c_str(), Upscale(luma(Ln), gw, gh, 8), gw * 8, gh * 8);
            WritePng((dir + "/prt_dusk.png").c_str(), Upscale(luma(Ld), gw, gh, 8), gw * 8, gh * 8);
            // sujas após mover a esfera 10
            TransferCache c2; c2.Init(samples, cfg); c2.Bake(bvh);
            const Aabb nb = SphereBox(scene.spheres[10]);
            c2.InvalidateRegion(nb);
            std::vector<float> v(size_t(gw) * gh); std::vector<uint8_t> hit(v.size(), 1);
            for (size_t i = 0; i < v.size(); ++i) v[i] = c2.IsDirty(uint32_t(i)) ? 1.0f : 0.15f;
            WritePng((dir + "/prt_dirty.png").c_str(), Upscale(Heat(v, hit, 1.0f), gw, gh, 8), gw * 8, gh * 8);
        }
    }
}

int main() {
    std::printf("[BVH: corretude vs forca bruta]\n");   TestSoupCorrectness();
    std::printf("[BVH: casos degenerados]\n");           TestDegenerate();
    std::printf("[BVH: qualidade, velocidade e limite de distancia]\n"); TestQualityAndSpeed();
    std::printf("[BVH: Refit]\n");                       TestRefit();
    std::printf("[Efeitos RT: AO, sombra, imagens]\n");  TestEffectsAndImages();
    std::printf("[PRT: ray tracing com cache]\n");       TestTransferCache();
    if (g_failures) { std::printf("%d FALHA(S)\n", g_failures); return 1; }
    std::printf("OK\n");
    return 0;
}
