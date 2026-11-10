// Testes do CSM: splits, cobertura do frustum, estabilidade (texel snapping), blend, sombra vs oclusão analítica
// (ponta a ponta no renderizador por software) e CsmSystem/RHI.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "LightPrism/Shadow/Csm.hpp"
#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Soft/Png.hpp"

using namespace LightPrism;
using namespace LightPrism::Soft;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= double(tol))

#ifndef LP_TEST_OUTPUT_DIR
#define LP_TEST_OUTPUT_DIR ""
#endif

namespace {

constexpr float kP00 = 0.9f, kP11 = 1.6f;

CsmCameraInput RandomCamera(DemoRng& rng, float zNear = 0.1f, float zFar = 120.0f) {
    const Vec3 eye{rng.Range(-40, 40), rng.Range(1, 20), rng.Range(-40, 40)};
    Vec3 dir{rng.Range(-1, 1), rng.Range(-0.6f, 0.3f), rng.Range(-1, 1)};
    dir = Normalize(dir);
    CsmCameraInput c;
    c.view = LookAtRH(eye, eye + dir);
    c.proj00 = kP00; c.proj11 = kP11; c.zNear = zNear; c.zFar = zFar;
    return c;
}

const Vec3 kSun = Normalize(Vec3{0.4f, -1.0f, -0.3f});

} // namespace

static void TestSplits() {
    CsmCameraInput cam;
    cam.view = LookAtRH({0, 2, 0}, {0, 2, -1});
    cam.proj00 = kP00; cam.proj11 = kP11; cam.zNear = 0.5f; cam.zFar = 200.0f;

    CsmConfig cfg;
    cfg.cascadeCount = 4;
    for (float lambda : {0.0f, 0.5f, 1.0f}) {
        cfg.splitLambda = lambda;
        const CsmResult r = ComputeCsm(cfg, cam, kSun);
        CHECK(r.gpu.cascadeCount == 4);
        CHECK_NEAR(r.gpu.cascades[0].splitNear, 0.5, 1e-6);
        CHECK_NEAR(r.gpu.cascades[3].splitFar, 200.0, 1e-3);
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK(r.gpu.cascades[i].splitFar > r.gpu.cascades[i].splitNear);
            if (i > 0) CHECK_NEAR(r.gpu.cascades[i].splitNear, r.gpu.cascades[i - 1].splitFar, 1e-4); // contíguas
        }
        if (lambda == 0.0f) {  // uniforme: passos iguais
            const float step = (200.0f - 0.5f) / 4.0f;
            for (uint32_t i = 0; i < 4; ++i) CHECK_NEAR(r.gpu.cascades[i].splitFar - r.gpu.cascades[i].splitNear, step, 1e-2);
        }
        if (lambda == 1.0f) {  // logarítmico: razão constante
            const float ratio = r.gpu.cascades[0].splitFar / r.gpu.cascades[0].splitNear;
            for (uint32_t i = 1; i < 4; ++i)
                CHECK_NEAR(r.gpu.cascades[i].splitFar / r.gpu.cascades[i].splitNear, ratio, 1e-2 * ratio);
        }
    }

    cfg.splitLambda = 0.7f;
    cfg.shadowDistance = 60.0f;
    CHECK_NEAR(ComputeCsm(cfg, cam, kSun).gpu.cascades[3].splitFar, 60.0, 1e-3);
    cfg.shadowDistance = 1000.0f;  // maior que o far: limitado ao far da câmera
    CHECK_NEAR(ComputeCsm(cfg, cam, kSun).gpu.cascades[3].splitFar, 200.0, 1e-3);

    cfg.shadowDistance = 0.0f;
    cfg.cascadeCount = 0;
    CHECK(ComputeCsm(cfg, cam, kSun).gpu.cascadeCount == 1);   // mínimo 1
    cfg.cascadeCount = 9;
    CHECK(ComputeCsm(cfg, cam, kSun).gpu.cascadeCount == 4);   // máximo 4

    // Entradas inválidas desligam as sombras (sem NaN/crash).
    cfg.cascadeCount = 3;
    CsmCameraInput bad = cam;
    bad.zFar = bad.zNear;
    CHECK(ComputeCsm(cfg, bad, kSun).gpu.cascadeCount == 0);
    bad = cam; bad.proj00 = 0.0f;
    CHECK(ComputeCsm(cfg, bad, kSun).gpu.cascadeCount == 0);
    bad = cam; bad.zNear = 0.0f;
    CHECK(ComputeCsm(cfg, bad, kSun).gpu.cascadeCount == 0);
    cfg.shadowMapSize = 1;
    CHECK(ComputeCsm(cfg, cam, kSun).gpu.cascadeCount == 0);
}

static void TestInverseAffine() {
    DemoRng rng;
    for (int i = 0; i < 50; ++i) {
        const CsmCameraInput cam = RandomCamera(rng);
        const Mat4 inv = InverseAffine(cam.view);
        const Mat4 id = cam.view * inv;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) CHECK_NEAR(id.m[r][c], r == c ? 1.0 : 0.0, 2e-4);
    }
    // Afim com escala não uniforme (ortográfica * rotação), como as matrizes das cascatas.
    const Mat4 m = OrthographicRH(-3, 5, -2, 7, 1.0f, 40.0f) * LookAtRH({0, 0, 0}, kSun);
    const Mat4 id = m * InverseAffine(m);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK_NEAR(id.m[r][c], r == c ? 1.0 : 0.0, 2e-4);
}

static void TestCoverage() {
    DemoRng rng;
    CsmConfig cfg;
    cfg.cascadeCount = 4;
    cfg.shadowMapSize = 1024;
    cfg.blendFraction = 0.15f;
    for (int pose = 0; pose < 60; ++pose) {
        const CsmCameraInput cam = RandomCamera(rng);
        const CsmResult r = ComputeCsm(cfg, cam, kSun);
        const Mat4 toWorld = InverseAffine(cam.view);
        for (uint32_t k = 0; k < r.gpu.cascadeCount; ++k) {
            const CsmCascadeGPU& c = r.gpu.cascades[k];
            const float prevLen = k > 0 ? c.splitNear - r.gpu.cascades[k - 1].splitNear : 0.0f;
            const float nearExt = c.splitNear - r.gpu.blendFraction * prevLen;
            for (float d : {nearExt, 0.5f * (nearExt + c.splitFar), c.splitFar})
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        const Vec3 w = toWorld.TransformPoint({float(sx) * d / cam.proj00, float(sy) * d / cam.proj11, -d});
                        const Vec3 clip = c.viewProj.TransformPoint(w);
                        CHECK(std::fabs(clip.x) <= 1.0005f && std::fabs(clip.y) <= 1.0005f);
                        CHECK(clip.z >= -1e-3f && clip.z <= 1.001f);
                    }
        }
    }
}

// Invariante exata do snapping: depois de deslocar o centro para a grade de texels, a esfera ORIGINAL da fatia
// (centro sem snapping, raio justo) continua inteira dentro da caixa ortográfica. Sem a folga de 1 texel no raio isto
// falha quase sempre; os cantos do frustum (TestCoverage) só estouram em casos raros por causa do arredondamento a 1/16 m.
static void TestSnapKeepsSliceInsideBox() {
    DemoRng rng;
    for (uint32_t size : {512u, 2048u}) {
        CsmConfig cfg;
        cfg.cascadeCount = 4;
        cfg.shadowMapSize = size;
        for (int pose = 0; pose < 100; ++pose) {
            const CsmResult r = ComputeCsm(cfg, RandomCamera(rng), kSun);
            for (uint32_t k = 0; k < r.gpu.cascadeCount; ++k) {
                const Vec3 clip = r.gpu.cascades[k].viewProj.TransformPoint(r.sphereCenter[k]);
                const float tightInBox = r.sphereRadiusTight[k] / r.sphereRadius[k];  // raio justo em unidades de clip
                CHECK(r.sphereRadius[k] > r.sphereRadiusTight[k]);
                CHECK(std::fabs(clip.x) + tightInBox <= 1.0f + 1e-4f);
                CHECK(std::fabs(clip.y) + tightInBox <= 1.0f + 1e-4f);
            }
        }
    }
}

static void TestRadiusRotationInvariant() {
    DemoRng rng;
    CsmConfig cfg;
    cfg.cascadeCount = 3;
    float radii[3] = {};
    for (int i = 0; i < 80; ++i) {
        const CsmResult r = ComputeCsm(cfg, RandomCamera(rng), kSun);  // pose e orientação aleatórias
        for (uint32_t k = 0; k < 3; ++k) {
            if (i == 0) radii[k] = r.sphereRadius[k];
            CHECK(r.sphereRadius[k] == radii[k]);  // exatamente igual
        }
    }
}

// Propriedade que elimina o tremor: um ponto fixo do mundo cai sempre na mesma posição FRACIONÁRIA do texel.
static double MaxFracDeviation(bool snap, uint32_t cascade) {
    DemoRng rng;
    CsmConfig cfg;
    cfg.cascadeCount = 3;
    cfg.shadowMapSize = 2048;
    cfg.snapToTexels = snap;
    const Vec3 P0{3.3f, 1.7f, -8.1f};
    double refX = 0, refY = 0, worst = 0;
    for (int i = 0; i < 200; ++i) {
        const CsmResult r = ComputeCsm(cfg, RandomCamera(rng), kSun);
        const Vec3 clip = r.gpu.cascades[cascade].viewProj.TransformPoint(P0);
        const double size = double(cfg.shadowMapSize);
        const double tx = (double(clip.x) * 0.5 + 0.5) * size, ty = (0.5 - double(clip.y) * 0.5) * size;
        const double fx = tx - std::floor(tx), fy = ty - std::floor(ty);
        if (i == 0) { refX = fx; refY = fy; continue; }
        auto circ = [](double a, double b) { const double d = std::fabs(a - b); return std::min(d, 1.0 - d); };
        worst = std::max({worst, circ(fx, refX), circ(fy, refY)});
    }
    return worst;
}

static void TestTexelSnapping() {
    for (uint32_t k = 0; k < 3; ++k) {
        CHECK(MaxFracDeviation(true, k) < 0.02);   // estável (tolerância = precisão de float)
        CHECK(MaxFracDeviation(false, k) > 0.1);   // sem snapping a imagem do mundo desliza: o teste detecta
    }
}

static void TestBlend() {
    CsmCameraInput cam;
    cam.view = LookAtRH({0, 2, 0}, {0, 2, -1});
    cam.proj00 = kP00; cam.proj11 = kP11; cam.zNear = 0.1f; cam.zFar = 100.0f;
    CsmConfig cfg;
    cfg.cascadeCount = 3;
    cfg.blendFraction = 0.2f;
    cfg.shadowMapSize = 64;
    cfg.pcfTaps = 1;
    const CsmResult r = ComputeCsm(cfg, cam, kSun);

    // Cascata 0 toda sombreada, cascata 1 toda iluminada, cascata 2 toda iluminada.
    const uint32_t N = 64;
    std::vector<float> dark(N * N, 0.0f), lit(N * N, 1.0f);
    CsmDepthMaps maps;
    maps.size = N;
    maps.data[0] = dark.data(); maps.data[1] = lit.data(); maps.data[2] = lit.data();

    const CsmCascadeGPU& c0 = r.gpu.cascades[0];
    const float width = r.gpu.blendFraction * (c0.splitFar - c0.splitNear);
    const Vec3 pos = Vec3{0, 2, 0} + Vec3{0, 0, -1} * (c0.splitFar - 0.5f * width);  // dentro das duas cascatas
    const Vec3 N0{0, 1, 0};
    uint32_t used = 99;
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, c0.splitNear + 1.0f, &used), 0.0, 1e-6);
    CHECK(used == 0);
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, c0.splitFar - width, &used), 0.0, 1e-6);          // início do blend
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, c0.splitFar - 0.5f * width), 0.5, 1e-4);          // meio
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, c0.splitFar), 1.0, 1e-4);                        // fim = cascata 1
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, c0.splitFar + 0.01f, &used), 1.0, 1e-6);
    CHECK(used == 1);
    // Além da última cascata: sem sombra.
    CHECK_NEAR(Reference::CsmVisibility(r.gpu, maps, pos, N0, r.gpu.cascades[2].splitFar + 1.0f, &used), 1.0, 1e-6);
    CHECK(used == 255);
}

// Ponta a ponta: sombras do shadow map (raycast a partir das matrizes das cascatas) vs oclusão analítica.
static void TestShadowMatchesAnalytic() {
    const uint32_t W = 480, H = 270;
    LightingSystem lights;
    PopulateDemoLights(lights, 0, 0, 1.5f);
    const CameraSetup cam = MakeDemoCamera(W, H);
    const Scene scene = MakeDemoScene();
    SoftRenderer renderer(scene, cam, 16, 24, 64);

    FrameParams fp;
    fp.cameraPosition = cam.eye;
    const FrameLightView& frame = lights.BuildFrame(fp);

    CsmConfig cfg;
    cfg.cascadeCount = 3;
    cfg.shadowMapSize = 1024;
    cfg.shadowDistance = 60.0f;
    cfg.pcfTaps = 1;
    cfg.blendFraction = 0.0f;
    CsmCameraInput ci;
    ci.view = renderer.View();
    ci.proj00 = renderer.Proj().m[0][0]; ci.proj11 = renderer.Proj().m[1][1];
    ci.zNear = cam.zNear; ci.zFar = cam.zFar;
    const CsmResult csm = ComputeCsm(cfg, ci, frame.sun.direction);
    CHECK(csm.gpu.cascadeCount == 3);
    const ShadowSetup setup = BuildShadowSetup(scene, csm.gpu, frame.sun.direction);

    FrameBuffer withShadows, without;
    RenderStats st;
    renderer.Render(frame, ShadeMode::AllLights, withShadows, st, &setup);
    renderer.Render(frame, ShadeMode::AllLights, without, st, nullptr);

    // Para cada pixel voltado ao sol (N·L > 0,15), compara o shadow map com a oclusão analítica. Pixels de costas
    // (N·L <= 0) ficam de fora: o sol não os ilumina de qualquer forma e o normal bias os empurra para fora da
    // silhueta. "Robusto" = o resultado analítico é o mesmo em 4 pontos deslocados 5 texels no plano tangente, ou seja,
    // longe de qualquer borda de sombra; ali o shadow map tem de ser EXATO. Nas bordas tolera-se alguma divergência.
    const Mat4 inv = InverseAffine(renderer.View());
    const Vec3 toSun = -Normalize(frame.sun.direction);
    const Vec3 tu = Normalize(Cross(toSun, Vec3{0, 1, 0})), tv = Cross(toSun, tu);
    uint64_t compared = 0, robust = 0, robustMismatch = 0, edge = 0, edgeMismatch = 0, shadowed = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            if (!withShadows.hit[i] || withShadows.cascade[i] >= csm.gpu.cascadeCount) continue;
            const float d = withShadows.viewDepth[i];
            const float ndcX = (float(x) + 0.5f) / float(W) * 2.0f - 1.0f, ndcY = 1.0f - (float(y) + 0.5f) / float(H) * 2.0f;
            const Vec3 pos = inv.TransformPoint({ndcX / renderer.Proj().m[0][0] * d, ndcY / renderer.Proj().m[1][1] * d, -d});

            // Superfície do pixel: esfera (se o ponto está sobre uma), parede ou piso.
            const Sphere* hs = nullptr;
            Vec3 n{0, 1, 0};
            float bestErr = 1e9f;
            for (const Sphere& sp : scene.spheres) {
                const float e = std::fabs(Length(pos - sp.center) - sp.radius);
                if (e < bestErr) { bestErr = e; hs = &sp; n = Normalize(pos - sp.center); }
            }
            if (bestErr > 0.02f) { hs = nullptr; n = pos.z < -33.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}; }
            if (Dot(n, toSun) <= 0.15f) continue;
            ++compared;

            auto occluded = [&](Vec3 off) {  // oclusão analítica num ponto deslocado, mantido sobre a superfície
                off = off - n * Dot(n, off);
                Vec3 p = pos + off, nn = n;
                if (hs) { nn = Normalize(p - hs->center); p = hs->center + nn * hs->radius; }
                return SunOccludedAnalytic(scene, p + nn * 2e-3f, frame.sun.direction);
            };
            const bool analytic = occluded({0, 0, 0});
            const bool map = withShadows.sunVisibility[i] < 0.5f;
            const float s = 5.0f * csm.gpu.cascades[withShadows.cascade[i]].texelWorldSize;
            const bool sameAround = occluded(tu * s) == analytic && occluded(tu * -s) == analytic &&
                                    occluded(tv * s) == analytic && occluded(tv * -s) == analytic;
            shadowed += analytic ? 1 : 0;
            if (sameAround) { ++robust; robustMismatch += (analytic != map) ? 1 : 0; }
            else            { ++edge;   edgeMismatch   += (analytic != map) ? 1 : 0; }
        }
    std::printf("  sombra vs analitico: %llu px voltados ao sol, %.1f%% em sombra | longe de borda: %llu px, %llu divergentes | "
                "perto de borda: %llu px, %llu divergentes (%.1f%%)\n",
                (unsigned long long)compared, 100.0 * double(shadowed) / double(compared ? compared : 1),
                (unsigned long long)robust, (unsigned long long)robustMismatch, (unsigned long long)edge,
                (unsigned long long)edgeMismatch, 100.0 * double(edgeMismatch) / double(edge ? edge : 1));
    CHECK(compared > uint64_t(W) * H / 2);
    CHECK(double(robust) > 0.8 * double(compared));                   // quase tudo é comparável com exatidão
    CHECK(robustMismatch == 0);                                       // sem vazamento de luz nem sombra falsa
    CHECK(double(edgeMismatch) < 0.15 * double(edge));                // bordas: só divergência de resolução
    CHECK(double(shadowed) / double(compared) > 0.02);                // há sombra de fato
    CHECK(double(shadowed) / double(compared) < 0.70);                // e não está tudo no escuro
    // Sombras só podem escurecer.
    for (size_t i = 0; i < withShadows.color.size(); ++i)
        CHECK(withShadows.color[i].x <= without.color[i].x + 1e-5f);

    const std::string dir = LP_TEST_OUTPUT_DIR;
    if (!dir.empty()) {
        CHECK(WritePng((dir + "/test_shadow_lit.png").c_str(), ToneMapToRgb8(withShadows), W, H));
        CHECK(WritePng((dir + "/test_shadow_cascades.png").c_str(), CascadeRgb8(withShadows), W, H));
    }
}

static void TestCsmSystem() {
    auto dev = CreateNullDevice();
    LightingSystem lights;
    PopulateDemoLights(lights, 0, 0, 1.5f);
    FrameParams fp;
    const FrameLightView* frame = &lights.BuildFrame(fp);

    CsmCameraInput cam;
    cam.view = LookAtRH({0, 3, 5}, {0, 1, -10});
    cam.proj00 = kP00; cam.proj11 = kP11; cam.zNear = 0.1f; cam.zFar = 80.0f;

    CsmSystem sys;
    CHECK(sys.Buffer() == nullptr);
    CHECK(sys.Update(*dev, *frame, cam));
    CHECK(sys.Buffer() != nullptr && sys.Buffer()->Desc().sizeBytes == sizeof(CsmGPU));
    CHECK(sys.Result().gpu.cascadeCount == 3);
    CHECK(std::memcmp(sys.Buffer()->NativeHandle(), &sys.Result().gpu, sizeof(CsmGPU)) == 0);

    // Sem sol: desliga.
    lights.ClearDirectionalLight();
    frame = &lights.BuildFrame(fp);
    CHECK(sys.Update(*dev, *frame, cam));
    CHECK(sys.Result().gpu.cascadeCount == 0);
    CHECK(static_cast<const CsmGPU*>(sys.Buffer()->NativeHandle())->cascadeCount == 0);

    // Sol sem CastShadows: desliga.
    DirectionalLightDesc sun;
    sun.flags = LightFlag_None;
    lights.SetDirectionalLight(sun);
    frame = &lights.BuildFrame(fp);
    CHECK(sys.Update(*dev, *frame, cam));
    CHECK(sys.Result().gpu.cascadeCount == 0);

    // Reativa e muda a config pela qualidade.
    sun.flags = LightFlag_CastShadows;
    lights.SetDirectionalLight(sun);
    frame = &lights.BuildFrame(fp);
    sys.SetConfig(MakeCsmConfig(MakeQualitySettings(QualityTier::High)));
    CHECK(sys.Update(*dev, *frame, cam));
    CHECK(sys.Result().gpu.cascadeCount == 4 && sys.Result().gpu.shadowMapSize == 4096.0f && sys.Result().gpu.pcfTaps == 9);
    sys.SetConfig(MakeCsmConfig(MakeQualitySettings(QualityTier::Mobile)));
    CHECK(sys.Update(*dev, *frame, cam));
    CHECK(sys.Result().gpu.cascadeCount == 1 && sys.Result().gpu.shadowMapSize == 1024.0f && sys.Result().gpu.pcfTaps == 1);

    // Câmera inválida: desliga sem falhar.
    CsmCameraInput bad = cam;
    bad.zFar = 0.0f;
    CHECK(sys.Update(*dev, *frame, bad));
    CHECK(sys.Result().gpu.cascadeCount == 0);

    // Troca de dispositivo recria o buffer.
    auto dev2 = CreateNullDevice();
    CHECK(sys.Update(*dev2, *frame, cam));
    CHECK(sys.Buffer() != nullptr && sys.Result().gpu.cascadeCount == 1);
}

static void TestLayout() {
    CHECK(offsetof(CsmGPU, cascadeCount) == 320);
    CHECK(offsetof(CsmGPU, pcfTaps) == 340);
    CHECK(offsetof(CsmCascadeGPU, splitNear) == 64);
    CHECK(offsetof(CsmCascadeGPU, depthRange) == 76);
}

int main() {
    TestSplits();
    TestInverseAffine();
    TestCoverage();
    TestSnapKeepsSliceInsideBox();
    TestRadiusRotationInvariant();
    TestTexelSnapping();
    TestBlend();
    TestShadowMatchesAnalytic();
    TestCsmSystem();
    TestLayout();
    if (g_failures == 0) std::printf("Testes de sombra (CSM): todos passaram.\n");
    else std::printf("Testes de sombra (CSM): %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
