// Testes das otimizações de memória/desempenho (fase 5a): raio automático, clusters ativos, orçamento adaptativo,
// cache de cascatas, sombra planar.
#include <chrono>
#include <cmath>
#include <cstdio>

#include "LightPrism/Cluster/ClusterSystem.hpp"
#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/MemoryBudget.hpp"
#include "LightPrism/Post/Ssao.hpp"
#include "LightPrism/Shadow/CascadeCache.hpp"
#include "LightPrism/Shadow/PlanarShadow.hpp"
#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Util.hpp"

using namespace LightPrism;
using namespace LightPrism::Soft;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)

namespace {
} // namespace

// 1) Raio automático
static void TestAutoRadius() {
    CHECK(AutoLightRadius(10.0f, 4.0f, 0.0f) == 10.0f);                 // desligado
    CHECK(std::fabs(AutoLightRadius(100.0f, 4.0f, 0.01f) - 20.0f) < 1e-4f); // sqrt(4/0.01)
    CHECK(AutoLightRadius(5.0f, 4.0f, 0.01f) == 5.0f);                  // nunca aumenta

    // Luzes com raio exagerado (30 m) para intensidades comuns de cena: o caso típico de "raio preguiçoso" do artista.
    const uint32_t W = 320, H = 180;
    LightingConfig base, tuned;
    base.maxPointLights = tuned.maxPointLights = 4096;
    tuned.autoRadiusCutoff = 0.05f;
    LightingSystem a(base), b(tuned);
    DemoRng rng;
    for (int i = 0; i < 6; ++i) {
        PointLightDesc d;
        d.position = {rng.Range(-10.0f, 10.0f), rng.Range(1.0f, 3.0f), rng.Range(-14.0f, -2.0f)};
        d.radius = 30.0f;
        d.intensity = rng.Range(14.0f, 40.0f);
        d.color = {rng.Range(0.5f, 1.0f), rng.Range(0.5f, 1.0f), rng.Range(0.5f, 1.0f)};
        a.AddPointLight(d);
        b.AddPointLight(d);
    }
    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer r(scene, cam, 16, 24, 128);
    FrameParams fp; fp.cameraPosition = cam.eye;
    FrameBuffer fa, fb; RenderStats sa, sb;
    r.Render(a.BuildFrame(fp), ShadeMode::Clustered, fa, sa);
    r.Render(b.BuildFrame(fp), ShadeMode::Clustered, fb, sb);
    double maxd = 0, sum = 0; size_t n = 0;
    for (size_t i = 0; i < fa.color.size(); ++i) if (fa.hit[i]) {
        const double d = std::fabs(fa.color[i].x - fb.color[i].x) + std::fabs(fa.color[i].y - fb.color[i].y) + std::fabs(fa.color[i].z - fb.color[i].z);
        maxd = std::max(maxd, d); sum += d; ++n;
    }
    std::printf("  raio automatico (corte 0.05, raio pedido 30 m): avaliacoes de luz %llu -> %llu (%.1f%%), erro HDR max %.4f medio %.5f\n",
                (unsigned long long)sa.lightEvaluations, (unsigned long long)sb.lightEvaluations,
                100.0 * double(sb.lightEvaluations) / double(sa.lightEvaluations), maxd, sum / double(n));
    CHECK(!sa.overflow && !sb.overflow);
    CHECK(sb.lightEvaluations < sa.lightEvaluations);
    CHECK(sb.lightEvaluations * 10 < sa.lightEvaluations * 9);   // pelo menos 10% menos avaliações
    CHECK(maxd < 0.1);
}

// 2) Clusters ativos
static void TestActiveClusters() {
    {   // fundo (profundidade 0) nunca marca cluster
        ClusterGridDesc gd; gd.screenWidth = 64; gd.screenHeight = 64; gd.zFar = 100.0f;
        ClusterGridGPU g = MakeClusterGrid(gd);
        std::vector<float> depth(64 * 64, 0.0f);
        std::vector<uint8_t> mask;
        CHECK(MarkActiveClusters(g, depth.data(), 64, 64, mask) == 0);
        depth[10 * 64 + 10] = 5.0f;
        CHECK(MarkActiveClusters(g, depth.data(), 64, 64, mask) == 1);
    }
    const uint32_t W = 480, H = 270;
    LightingConfig cfg; cfg.maxPointLights = 4096;
    LightingSystem lights(cfg);
    PopulateDemoLights(lights, 120, 12, 0.25f);
    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(W, H);
    SoftRenderer r(scene, cam, 16, 24, 128);
    FrameParams fp; fp.cameraPosition = cam.eye;
    const FrameLightView& v = lights.BuildFrame(fp);
    FrameBuffer f0, f1; RenderStats s0, s1;
    r.activeClusters = false;
    r.Render(v, ShadeMode::Clustered, f0, s0);
    r.activeClusters = true;
    r.Render(v, ShadeMode::Clustered, f1, s1);
    double maxd = 0;
    for (size_t i = 0; i < f0.color.size(); ++i) if (f0.hit[i]) {
        maxd = std::max<double>(maxd, std::fabs(f0.color[i].x - f1.color[i].x) + std::fabs(f0.color[i].y - f1.color[i].y) + std::fabs(f0.color[i].z - f1.color[i].z));
    }
    std::printf("  clusters ativos: %u de %u (%.1f%%), cull %.1f ms -> %.1f ms, diferenca max %.3g\n", s1.activeClusters, s1.clusterCount,
                100.0 * s1.activeClusters / s1.clusterCount, s0.cullMs, s1.cullMs, maxd);
    CHECK(maxd == 0.0);                            // idêntico nos pixels desenhados
    CHECK(!s0.overflow && !s1.overflow);
    CHECK(s1.activeClusters > 0 && s1.activeClusters < s1.clusterCount / 2);
    CHECK(s1.cullMs < s0.cullMs);                  // só o culling; a marcação é um passe de GPU barato (markMs)
}

// 2b) Com a máscara, o mesmo orçamento de índices aguenta mais luzes (menos overflow).
static void TestActiveClustersOverflow() {
    LightingConfig cfg; cfg.maxPointLights = 4096;
    LightingSystem lights(cfg);
    PopulateDemoLights(lights, 300, 20, 0.25f);
    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(480, 270);
    SoftRenderer r(scene, cam, 16, 24, 128);
    FrameParams fp; fp.cameraPosition = cam.eye;
    const FrameLightView& v = lights.BuildFrame(fp);
    FrameBuffer f0, f1; RenderStats s0, s1;
    r.activeClusters = false; r.Render(v, ShadeMode::Clustered, f0, s0);
    r.activeClusters = true;  r.Render(v, ShadeMode::Clustered, f1, s1);
    std::printf("  320 luzes, 8 indices/cluster: overflow sem mascara = %s, com mascara = %s\n", s0.overflow ? "SIM" : "nao", s1.overflow ? "SIM" : "nao");
    CHECK(s0.overflow && !s1.overflow);
}

// 3) Orçamento adaptativo
namespace {
struct Rig {
    std::unique_ptr<IGraphicsDevice> dev;
    LightingSystem lights;
    ClusterCamera cam;
    ClusterSystem cs;
    explicit Rig(const ClusterSystemDesc& d) : cs(d) {
        dev = CreateNullDevice();
        const CameraSetup c = MakeDemoCamera(640, 360);
        const Mat4 proj = PerspectiveRH_ReverseZ(c.fovYRad, 640.0f / 360.0f, c.zNear, c.zFar);
        cam = MakeClusterCamera(LookAtRH(c.eye, c.target), proj, 640, 360, c.zNear, c.zFar);
    }
    bool Frame(ClusterStats* st = nullptr) {
        FrameParams fp; fp.cameraPosition = {0, 6.5f, 7};
        const FrameLightView& v = lights.BuildFrame(fp);
        lights.UploadToGPU(*dev);
        const bool ok = cs.Execute(*dev, lights, v, cam);
        if (st) cs.ReadbackStats(*st);
        return ok;
    }
};
} // namespace

static void TestAdaptiveBudget() {
    ClusterSystemDesc fixed; fixed.mode = ClusterMode::Cpu; fixed.indicesPerCluster = 8;
    ClusterSystemDesc ad; ad.mode = ClusterMode::Cpu; ad.indicesPerCluster = 1; ad.adaptiveIndexBudget = true; ad.shrinkAfterFrames = 5;

    Rig rf(fixed), ra(ad);
    PopulateDemoLights(rf.lights, 200, 20, 0.25f);
    PopulateDemoLights(ra.lights, 200, 20, 0.25f);
    ClusterStats s;
    rf.Frame(&s);
    const uint64_t fixedBytes = rf.cs.AllocatedBytes();
    const bool fixedOverflow = s.overflow;

    ra.Frame(&s);
    CHECK(s.overflow);                              // começa pequeno demais: overflow detectado
    const uint64_t smallBytes = ra.cs.AllocatedBytes();
    ra.Frame(&s);                                    // cresce
    ra.Frame(&s);
    CHECK(!s.overflow);
    CHECK(ra.cs.AllocatedBytes() > smallBytes);
    std::printf("  orcamento adaptativo: fixo(8/cluster) %llu KiB%s | adaptativo 1/cluster %llu KiB -> %llu KiB sem overflow\n",
                (unsigned long long)(fixedBytes / 1024), fixedOverflow ? " (com overflow)" : "", (unsigned long long)(smallBytes / 1024),
                (unsigned long long)(ra.cs.AllocatedBytes() / 1024));
    CHECK(ra.cs.AllocatedBytes() < fixedBytes);     // economiza memória e não perde luzes

    // Cena esvazia: o orçamento encolhe.
    LightingSystem empty;
    Rig re(ad);
    PopulateDemoLights(re.lights, 200, 20, 0.25f);
    for (int i = 0; i < 4; ++i) re.Frame(&s);
    const uint64_t busy = re.cs.AllocatedBytes();
    LightingSystem fewer;
    re.lights = std::move(fewer);
    PopulateDemoLights(re.lights, 4, 0, 0.25f);
    for (int i = 0; i < 20; ++i) re.Frame(&s);
    std::printf("  ... com poucas luzes: %llu KiB -> %llu KiB\n", (unsigned long long)(busy / 1024), (unsigned long long)(re.cs.AllocatedBytes() / 1024));
    CHECK(re.cs.AllocatedBytes() < busy);
    CHECK(!s.overflow);
}

// 4) Cache de cascatas
static void TestCascadeCache() {
    const uint32_t W = 960, H = 540;
    const CameraSetup base = MakeDemoCamera(W, H);
    const Mat4 proj = PerspectiveRH_ReverseZ(base.fovYRad, float(W) / float(H), base.zNear, base.zFar);
    CsmConfig cfg; cfg.cascadeCount = 3; cfg.shadowMapSize = 2048; cfg.shadowDistance = 80.0f;
    const Vec3 sun = Normalize(Vec3{0.4f, -1.0f, -0.3f});
    auto csmAt = [&](Vec3 eye) {
        CsmCameraInput ci; ci.view = LookAtRH(eye, eye + Vec3{0, -0.15f, -1}); ci.proj00 = proj.m[0][0]; ci.proj11 = proj.m[1][1];
        ci.zNear = base.zNear; ci.zFar = base.zFar;
        return ComputeCsm(cfg, ci, sun).gpu;
    };
    CascadeCache cache;
    const CsmGPU g0 = csmAt({0, 2, 5});
    CHECK(cache.Update(g0, false) == 0x7);                   // primeiro frame: tudo
    CHECK(cache.Update(g0, false) == 0);                     // parado, estático: nada
    CHECK(cache.Update(csmAt({0, 2, 5}), false) == 0);
    // Câmera anda 20 cm: só as cascatas cujo texel é menor que o deslocamento mudam.
    for (float step : {0.003f, 0.025f}) {          // 3 mm/frame (mira/parado) e 2,5 cm/frame (andando a 1,5 m/s a 60 fps)
        CascadeCache c2;
        Vec3 eye{0, 2, 5};
        c2.Update(csmAt(eye), false);
        uint32_t sum = 0, frames = 0, farChanges = 0;
        for (int i = 0; i < 300; ++i) {
            eye.z -= step;
            const uint32_t m = c2.Update(csmAt(eye), false);
            sum += uint32_t(PopCount32(m)); ++frames; farChanges += (m >> 2) & 1u;
        }
        std::printf("  cache de cascatas, camera a %.1f mm/frame: %.2f cascatas redesenhadas por frame (de 3); cascata distante em %u%% dos frames\n",
                    step * 1000.0f, double(sum) / frames, 100u * farChanges / frames);
        if (step < 0.01f) { CHECK(double(sum) / frames < 1.6); CHECK(farChanges < frames / 5); }
    }
    // Dinâmico: cascata i a cada 2^i frames, mesmo parado.
    CascadeCache dyn;
    const CsmGPU g = csmAt({0, 2, 5});
    dyn.Update(g, true);
    uint32_t counts[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) { const uint32_t m = dyn.Update(g, true); for (int c = 0; c < 3; ++c) counts[c] += (m >> c) & 1u; }
    CHECK(counts[0] == 16 && counts[1] == 8 && counts[2] == 4);
    dyn.Invalidate();
    CHECK(dyn.Update(g, false) == 0x7);
    // salto grande da camera: todas as cascatas mudam (pega cache que nunca compara a matriz)
    CascadeCache jump; jump.Update(csmAt({0, 2, 5}), false);
    CHECK(jump.Update(csmAt({40, 2, -30}), false) == 0x7);
    // maxStride limita o intervalo: cascata 2 (stride 4) com maxStride 2 atualiza a cada 2 frames
    CascadeCache lim; lim.Update(g, true, 2);
    uint32_t c2 = 0;
    for (int i = 0; i < 16; ++i) c2 += (lim.Update(g, true, 2) >> 2) & 1u;
    CHECK(c2 == 8);
}

// 5) Sombra planar de Blinn
static void TestPlanarShadow() {
    DemoRng rng;
    const Vec3 n = Normalize(Vec3{0.1f, 1.0f, 0.2f});
    const float d = -0.5f;                           // plano: n.x + d = 0
    for (int i = 0; i < 100; ++i) {
        const Vec3 light{rng.Range(-5, 5), rng.Range(6, 12), rng.Range(-5, 5)};
        const Vec3 p{rng.Range(-3, 3), rng.Range(1.5f, 4), rng.Range(-3, 3)};
        const Mat4 T = MakePlanarShadowMatrix(n, d, light, 1.0f);
        const Vec3 q = ApplyPlanarShadow(T, p);
        CHECK(std::fabs(Dot(n, q) + d) < 1e-3f);     // cai no plano
        const Vec3 u = Normalize(p - light), w = Normalize(q - light);   // colinear com a luz
        CHECK(Length(Cross(u, w)) < 1e-3f);
        // contra interseção raio-plano analítica
        const float t = -(Dot(n, light) + d) / Dot(n, p - light);
        const Vec3 ref = light + (p - light) * t;
        CHECK(Length(ref - q) < 1e-3f);
        // direcional (w = 0): l aponta PARA a luz
        const Vec3 toLight = Normalize(Vec3{0.3f, 1.0f, 0.1f});
        const Vec3 q2 = ApplyPlanarShadow(MakePlanarShadowMatrix(n, d, toLight, 0.0f), p);
        CHECK(std::fabs(Dot(n, q2) + d) < 1e-3f);
        CHECK(Length(Cross(Normalize(p - q2), toLight)) < 1e-3f);
    }
}

// 5) SSAO em meia resolução
static void TestSsaoHalf() {
    Scene scene;
    Plane floor; floor.point = {0, 0, 0}; floor.normal = {0, 1, 0}; scene.planes.push_back(floor);
    Plane wall; wall.point = {0, 0, -6}; wall.normal = {0, 0, 1}; scene.planes.push_back(wall);
    Sphere a; a.center = {-1.6f, 0.8f, -4.2f}; a.radius = 0.8f; scene.spheres.push_back(a);
    Sphere b; b.center = {1.4f, 0.8f, -3.0f}; b.radius = 0.8f; scene.spheres.push_back(b);
    CameraSetup cam; cam.eye = {0.0f, 2.4f, 0.5f}; cam.target = {0.0f, 0.5f, -5.0f}; cam.fovYRad = 1.0f; cam.zNear = 0.1f; cam.zFar = 80.0f;
    cam.width = 480; cam.height = 270;
    SoftRenderer r(scene, cam);
    const GBufferView gb = r.ExportGBuffer();
    Ssao::SsaoInput in; in.width = cam.width; in.height = cam.height; in.viewDepth = gb.viewDepth.data(); in.viewNormal = gb.viewNormal.data();
    in.proj00 = r.Proj().m[0][0]; in.proj11 = r.Proj().m[1][1];
    Ssao::SsaoConfig cfg; cfg.radius = 0.6f;
    std::vector<float> full, half, truth;
    const auto t0 = std::chrono::steady_clock::now();
    Ssao::ComputeSsao(cfg, in, full);
    const double msFull = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto t1 = std::chrono::steady_clock::now();
    Ssao::ComputeSsaoHalf(cfg, in, half);
    const double msHalf = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    r.ComputeRayTracedAO(cfg.radius, 256, truth);
    double eFull = 0, eHalf = 0, oFull = 0, oHalf = 0; size_t n = 0, no = 0;
    for (size_t i = 0; i < truth.size(); ++i) if (gb.hit[i]) {
        eFull += std::fabs(full[i] - truth[i]); eHalf += std::fabs(half[i] - truth[i]); ++n;
        if (truth[i] < 0.9f) { oFull += std::fabs(full[i] - truth[i]); oHalf += std::fabs(half[i] - truth[i]); ++no; }
    }
    std::printf("  SSAO meia-resolucao: erro medio vs verdade %.4f (cheia %.4f); nos ocluidos %.4f (cheia %.4f); tempo %.0f ms vs %.0f ms (%.1fx)\n",
                eHalf / double(n), eFull / double(n), oHalf / double(no), oFull / double(no), msHalf, msFull, msFull / msHalf);
    CHECK(oHalf / double(no) < oFull / double(no) + 0.05);
    CHECK(eHalf / double(n) < eFull / double(n) + 0.01);
    CHECK(msHalf < msFull * 0.6);
}

// 6) Formatos HDR compactos
static void TestPacking() {
    DemoRng rng;
    double worstR = 0, worstB = 0, worstShared = 0;
    for (int i = 0; i < 4000; ++i) {
        const float scale = std::pow(10.0f, rng.Range(-3.0f, 3.0f));
        const Vec3 c{scale * rng.Range(0.05f, 1.0f), scale * rng.Range(0.05f, 1.0f), scale * rng.Range(0.05f, 1.0f)};
        const Vec3 u = Ibl::UnpackR11G11B10F(Ibl::PackR11G11B10F(c));
        worstR = std::max(worstR, double(std::fabs(u.x - c.x) / c.x));
        worstB = std::max(worstB, double(std::fabs(u.z - c.z) / c.z));
        const Vec3 s = Ibl::UnpackRGB9E5(Ibl::PackRGB9E5(c));
        const float mx = std::max(c.x, std::max(c.y, c.z));
        worstShared = std::max(worstShared, double(std::max(std::fabs(s.x - c.x), std::max(std::fabs(s.y - c.y), std::fabs(s.z - c.z))) / mx));
    }
    std::printf("  packing: R11G11B10F erro relativo max R %.3f%% B %.3f%% | RGB9E5 erro max %.3f%% do maior canal\n", 100 * worstR, 100 * worstB, 100 * worstShared);
    CHECK(worstR < 0.0085);       // 2^-7 (mantissa de 6 bits)
    CHECK(worstB < 0.017);        // 2^-6 (mantissa de 5 bits)
    CHECK(worstShared < 0.0045);  // ~2^-8
    const Vec3 z = Ibl::UnpackRGB9E5(Ibl::PackRGB9E5({0, 0, 0}));
    CHECK(z.x == 0 && z.y == 0 && z.z == 0);
    const Vec3 neg = Ibl::UnpackR11G11B10F(Ibl::PackR11G11B10F({-1.0f, 0.5f, 0.0f}));
    CHECK(neg.x == 0 && std::fabs(neg.y - 0.5f) < 1e-6f);
    const Vec3 huge = Ibl::UnpackRGB9E5(Ibl::PackRGB9E5({1e9f, 1.0f, 1.0f}));
    CHECK(huge.x > 65000.0f && huge.x < 65500.0f);

    // IBL completo com tudo compactado vs float.
    Ibl::ProceduralSky sky;
    const Ibl::EnvFn env = [&sky](const Vec3& d) { return sky.Radiance(d); };
    Ibl::IblData f = Ibl::BuildIblData(env, 48, 64, 6, 32);
    const uint64_t floatBytes = Ibl::PrefilteredBytes(f.prefiltered) + f.lut.ab.size() * 4;
    for (Ibl::HdrFormat fmt : {Ibl::HdrFormat::RGB9E5, Ibl::HdrFormat::R11G11B10F}) {
        const Ibl::PackedPrefiltered pk = Ibl::PackPrefiltered(f.prefiltered, fmt);
        const Ibl::PackedBrdfLut pl = Ibl::PackBrdfLut(f.lut);
        Ibl::IblData p = f;
        p.prefiltered = Ibl::UnpackPrefiltered(pk);
        p.lut = Ibl::UnpackBrdfLut(pl);
        const uint64_t packedBytes = pk.Bytes() + pl.Bytes();
        double worst = 0;
        for (int i = 0; i < 60; ++i) {
            Reference::SurfaceData sd;
            const float z2 = rng.Range(-1.0f, 1.0f), ph = rng.Range(0.0f, 2.0f * kPi), s2 = std::sqrt(1 - z2 * z2);
            sd.N = {s2 * std::cos(ph), z2, s2 * std::sin(ph)};
            sd.V = Normalize(sd.N + Vec3{0.3f, 0.2f, 0.1f});
            sd.albedo = {0.8f, 0.5f, 0.3f}; sd.metallic = (i & 1) ? 1.0f : 0.0f; sd.roughness = rng.Range(0.15f, 1.0f);
            const Vec3 a2 = Ibl::EvaluateIndirect(f, sd, 1.0f).Total(), b2 = Ibl::EvaluateIndirect(p, sd, 1.0f).Total();
            worst = std::max(worst, double(std::fabs(a2.y - b2.y) / std::max(a2.y, 0.05f)));
        }
        std::printf("  IBL compactado (%s + LUT RG8): %llu KiB -> %llu KiB (%.0f%%), erro relativo max %.2f%%\n", fmt == Ibl::HdrFormat::RGB9E5 ? "RGB9E5" : "R11G11B10F",
                    (unsigned long long)(floatBytes / 1024), (unsigned long long)(packedBytes / 1024), 100.0 * double(packedBytes) / double(floatBytes), 100 * worst);
        CHECK(packedBytes * 3 < floatBytes * 1.2);
        CHECK(worst < 0.04);
    }
}

// 7) SH L1 (mobile)
static void TestShL1() {
    Ibl::ProceduralSky sky;
    const Ibl::EnvFn env = [&sky](const Vec3& d) { return sky.Radiance(d); };
    const Ibl::Sh9 irr = Ibl::ConvolveCosine(Ibl::ProjectToSh(env, 64));
    const Ibl::IblGPU l2 = Ibl::MakeIblGPU(irr, 1.0f, 6, 0), l1 = Ibl::MakeIblGPU(irr, 1.0f, 6, Ibl::IblFlag_ShL1);
    DemoRng rng;
    double meanErr = 0, worst = 0; int n = 0;
    for (int i = 0; i < 200; ++i) {
        const float z = rng.Range(-1.0f, 1.0f), ph = rng.Range(0.0f, 2.0f * kPi), s = std::sqrt(1 - z * z);
        const Vec3 nrm{s * std::cos(ph), z, s * std::sin(ph)};
        const Vec3 a = Ibl::EvalSh(irr, nrm, 3), b = Ibl::EvalSh(irr, nrm, 2);
        const Vec3 pa = Ibl::EvaluateIrradiancePacked(l2, nrm), pb = Ibl::EvaluateIrradiancePacked(l1, nrm);
        CHECK(std::fabs(pa.y - a.y) < 1e-4f && std::fabs(pb.y - b.y) < 1e-4f);   // GPU == CPU nos dois modos
        const double e = std::fabs(b.y - a.y) / std::max(a.y, 0.05f);
        meanErr += e; worst = std::max(worst, e); ++n;
    }
    std::printf("  SH L1 (4 coeficientes) vs L2 (9): erro relativo medio %.2f%%, max %.2f%% (ALU: 4 em vez de 9 MADs por canal)\n", 100 * meanErr / n, 100 * worst);
    CHECK(meanErr / n < 0.10);   // L1 perde detalhe direcional: aceitável só no tier Mobile
    CHECK(worst < 0.30);
}

// 8) Estimador de memória
static void TestMemoryEstimate() {
    MemoryQuery q;
    // Cluster: bate com a alocação real do ClusterSystem.
    {
        q.quality = MakeQualitySettings(QualityTier::Medium);
        ClusterSystemDesc d; d.mode = ClusterMode::Cpu; d.indicesPerCluster = q.quality.indicesPerCluster;
        d.tileSizePx = q.tileSizePx; d.zSlices = q.zSlices;
        Rig rig(d);
        PopulateDemoLights(rig.lights, 10, 2, 0.25f);
        const CameraSetup c = MakeDemoCamera(q.width, q.height);
        const Mat4 proj = PerspectiveRH_ReverseZ(c.fovYRad, float(q.width) / float(q.height), c.zNear, c.zFar);
        rig.cam = MakeClusterCamera(LookAtRH(c.eye, c.target), proj, q.width, q.height, c.zNear, c.zFar);
        rig.Frame();
        const MemoryEstimate e = EstimateMemory(q);
        const uint64_t lightsPart = uint64_t(q.maxPointLights + q.maxSpotLights) * sizeof(ClusterLightGPU);
        CHECK(e.clusters - lightsPart == rig.cs.AllocatedBytes());
    }
    // IBL: bate com os vetores reais.
    {
        const Ibl::EnvFn flat = [](const Vec3&) { return Vec3{1, 1, 1}; };
        const Ibl::PrefilteredEnv pe = Ibl::BuildPrefilteredEnv(flat, 64, 6, 4);
        CHECK(Ibl::PrefilteredBytes(pe) == EstimateCubemapChainBytes(64, 6, 12));
        CHECK(Ibl::PackPrefiltered(pe, Ibl::HdrFormat::RGB9E5).Bytes() == EstimateCubemapChainBytes(64, 6, 4));
    }
    // Tabela por tier e economia das otimizações (1080p).
    std::printf("  memoria da Light Prism em 1080p (MiB):  tier      luzes  clusters  sombras    IBL   SSAO  TOTAL\n");
    uint64_t prev = ~0ull;
    const char* names[] = {"Mobile", "Low", "Medium", "High"};
    for (int t = 3; t >= 0; --t) {
        MemoryQuery mq; mq.quality = MakeQualitySettings(QualityTier(t));
        const MemoryEstimate e = EstimateMemory(mq);
        std::printf("                                          %-8s %6s %8s %8s %6s %6s %6s\n", names[t], FormatMiB(e.lightBuffers).c_str(), FormatMiB(e.clusters).c_str(),
                    FormatMiB(e.shadowMaps).c_str(), FormatMiB(e.ibl).c_str(), FormatMiB(e.ssao).c_str(), FormatMiB(e.Total()).c_str());
        CHECK(e.Total() < prev);
        prev = e.Total();
    }
    MemoryQuery base; base.quality = MakeQualitySettings(QualityTier::Medium);
    base.quality.packedIbl = false; base.quality.ssaoHalfRes = false; base.quality.indicesPerCluster = 8; base.quality.shadowDepth16 = false;
    MemoryQuery opt; opt.quality = MakeQualitySettings(QualityTier::Medium); opt.quality.shadowDepth16 = true;
    const MemoryEstimate eb = EstimateMemory(base), eo = EstimateMemory(opt);
    std::printf("  Medium sem otimizacoes de memoria: %s MiB -> com: %s MiB (-%.0f%%)\n", FormatMiB(eb.Total()).c_str(), FormatMiB(eo.Total()).c_str(),
                100.0 * (1.0 - double(eo.Total()) / double(eb.Total())));
    CHECK(eo.Total() < eb.Total() * 7 / 10);
}

int main() {
    TestAutoRadius();
    TestActiveClusters();
    TestActiveClustersOverflow();
    TestAdaptiveBudget();
    TestCascadeCache();
    TestPlanarShadow();
    TestSsaoHalf();
    TestPacking();
    TestShL1();
    TestMemoryEstimate();
    if (g_failures == 0) std::printf("Testes de otimizacao: todos passaram.\n");
    else std::printf("Testes de otimizacao: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
