// Golden tests CPU (sem framework externo): layouts, BRDF, atenuação, orçamento, upload.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Mat4.hpp"
#include "LightPrism/Reference/Brdf.hpp"
#include "LightPrism/Reference/Tonemap.hpp"

using namespace LightPrism;

static int g_failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= double(tol))

// ∫ D(h) (n·h) dω sobre o hemisfério deve valer 1 (normalização da NDF).
static void TestGGXNormalization() {
    for (float roughness : {0.1f, 0.3f, 0.6f, 1.0f}) {
        const int steps = 400000;
        const double half = 1.5707963267948966;
        double sum = 0.0;
        for (int i = 0; i < steps; ++i) {
            const double th = (i + 0.5) / steps * half;
            const double c = std::cos(th), s = std::sin(th);
            sum += Reference::D_GGX(float(c), roughness) * c * s * (half / steps);
        }
        sum *= 2.0 * 3.14159265358979323846;
        CHECK_NEAR(sum, 1.0, 0.02);
    }
}

static void TestFresnel() {
    const Vec3 F0{0.04f, 0.04f, 0.04f};
    const Vec3 n = Reference::F_Schlick(1.0f, F0);
    const Vec3 g = Reference::F_Schlick(0.0f, F0);
    CHECK_NEAR(n.x, 0.04, 1e-6);
    CHECK_NEAR(g.x, 1.0, 1e-6);
}

static void TestVisibilityFinite() {
    const float v = Reference::V_SmithGGXCorrelated(1.0f, 0.0f, 0.5f);
    CHECK(std::isfinite(v));
    CHECK(v >= 0.0f);
}

static void TestAttenuation() {
    CHECK_NEAR(Reference::DistanceAttenuation(10.0f, 10.0f), 0.0, 1e-9);
    CHECK_NEAR(Reference::DistanceAttenuation(25.0f, 10.0f), 0.0, 1e-9);
    CHECK(std::isfinite(Reference::DistanceAttenuation(0.0f, 10.0f)));
    float prev = Reference::DistanceAttenuation(0.5f, 10.0f);
    for (float d = 1.0f; d < 10.0f; d += 0.5f) {
        const float a = Reference::DistanceAttenuation(d, 10.0f);
        CHECK(a <= prev);
        prev = a;
    }
}

static void TestSpotCone() {
    SpotLightDesc d;
    d.innerAngleRad = 0.3f;
    d.outerAngleRad = 0.6f;
    const float cosI = std::cos(0.3f), cosO = std::cos(0.6f);
    const float scale = 1.0f / (cosI - cosO), offset = -cosO * scale;
    CHECK_NEAR(Reference::SpotAttenuation(cosI, scale, offset), 1.0, 1e-5);
    CHECK_NEAR(Reference::SpotAttenuation(cosO, scale, offset), 0.0, 1e-5);
    CHECK_NEAR(Reference::SpotAttenuation(0.0f, scale, offset), 0.0, 1e-9);
}

static void TestHandles() {
    LightingSystem sys;
    PointLightDesc d;
    LightHandle h = sys.AddPointLight(d);
    CHECK(h.IsValid());
    CHECK(sys.UpdatePointLight(h, d));
    CHECK(sys.RemoveLight(h));
    CHECK(!sys.RemoveLight(h));            // handle velho
    CHECK(!sys.UpdatePointLight(h, d));
    LightHandle h2 = sys.AddPointLight(d); // reaproveita slot, nova geração
    CHECK(h2.index == h.index && h2.generation != h.generation);
    CHECK(!sys.UpdatePointLight(h, d));
}

static void TestBudgetKeepsNearest() {
    LightingConfig cfg;
    cfg.maxPointLights = 32;
    LightingSystem sys(cfg);
    for (int i = 0; i < 100; ++i) {
        PointLightDesc d;
        d.position = {float(i), 0, 0};
        d.radius = 5.0f;
        d.intensity = 100.0f;
        sys.AddPointLight(d);
    }
    FrameParams p;
    const FrameLightView& v = sys.BuildFrame(p);
    CHECK(v.pointCount == 32);
    CHECK(v.droppedLights == 68);
    for (uint32_t i = 0; i < v.pointCount; ++i) CHECK(v.points[i].position.x < 32.0f);
}

static void TestLayerMaskAndExposure() {
    LightingSystem sys;
    PointLightDesc a; a.layerMask = 0x1; a.intensity = 10.0f;
    PointLightDesc b; b.layerMask = 0x2;
    sys.AddPointLight(a);
    sys.AddPointLight(b);
    FrameParams p; p.layerMask = 0x1; p.preExposure = 0.5f;
    const FrameLightView& v = sys.BuildFrame(p);
    CHECK(v.pointCount == 1);
    CHECK_NEAR(v.points[0].intensity, 5.0, 1e-6);
}

static void TestUploadRoundTrip() {
    LightingSystem sys;
    for (int i = 0; i < 10; ++i) {
        PointLightDesc d; d.position = {float(i), 1, 2}; d.intensity = float(i + 1);
        sys.AddPointLight(d);
    }
    SpotLightDesc s; s.position = {3, 4, 5};
    sys.AddSpotLight(s);
    sys.SetDirectionalLight({});
    auto dev = CreateNullDevice();
    const FrameLightView& v = sys.BuildFrame({});
    CHECK(sys.UploadToGPU(*dev));
    CHECK(sys.PointLightBuffer() != nullptr);
    CHECK(std::memcmp(sys.PointLightBuffer()->NativeHandle(), v.points, v.pointCount * sizeof(PointLightGPU)) == 0);
    CHECK(std::memcmp(sys.SpotLightBuffer()->NativeHandle(), v.spots, sizeof(SpotLightGPU)) == 0);
    CHECK(v.globals.hasSun == 1u && v.globals.numPointLights == 10u);
    // Segundo frame com mais luzes força crescimento do buffer.
    for (int i = 0; i < 100; ++i) sys.AddPointLight({});
    sys.BuildFrame({});
    CHECK(sys.UploadToGPU(*dev));
}

static void TestQualityTiers() {
    DeviceCaps mobile; mobile.mobileClass = true;
    CHECK(SuggestTier(mobile) == QualityTier::Mobile);
    DeviceCaps big; big.vramBytes = 12ull << 30;
    CHECK(SuggestTier(big) == QualityTier::High);
    DeviceCaps small; small.vramBytes = 2ull << 30;
    CHECK(SuggestTier(small) == QualityTier::Low);
    CHECK(MakeQualitySettings(QualityTier::Low).csmCascades < MakeQualitySettings(QualityTier::High).csmCascades);
}

static void TestMat4() {
    // LookAt: origem vista de (0,0,5) fica a 5 unidades à frente (-Z).
    const Mat4 view = LookAtRH({0, 0, 5}, {0, 0, 0});
    const Vec3 o = view.TransformPoint({0, 0, 0});
    CHECK_NEAR(o.x, 0.0, 1e-6); CHECK_NEAR(o.y, 0.0, 1e-6); CHECK_NEAR(o.z, -5.0, 1e-5);

    // Up paralelo à direção não gera NaN.
    const Mat4 down = LookAtRH({0, 5, 0}, {0, 0, 0}, {0, 1, 0});
    CHECK(std::isfinite(down.m[0][0]) && std::isfinite(down.m[2][3]));

    // Perspectiva Z reverso: near -> 1, far -> 0.
    const Mat4 proj = PerspectiveRH_ReverseZ(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    CHECK_NEAR(proj.TransformPoint({0, 0, -0.1f}).z, 1.0, 1e-4);
    CHECK_NEAR(proj.TransformPoint({0, 0, -100.0f}).z, 0.0, 1e-4);

    // Ortográfica: near -> 0, far -> 1.
    const Mat4 ortho = OrthographicRH(-10, 10, -10, 10, 1.0f, 50.0f);
    CHECK_NEAR(ortho.TransformPoint({0, 0, -1.0f}).z, 0.0, 1e-5);
    CHECK_NEAR(ortho.TransformPoint({0, 0, -50.0f}).z, 1.0, 1e-5);
    CHECK_NEAR(ortho.TransformPoint({10, -10, -1.0f}).x, 1.0, 1e-5);

    // Associatividade: (A*B)p == A(Bp); transposta dupla é identidade.
    const Vec3 p{1, 2, -3};
    const Vec3 a = (proj * view).TransformPoint(p);
    const Vec3 b = proj.TransformPoint(view.TransformPoint(p));
    CHECK_NEAR(a.x, b.x, 1e-5); CHECK_NEAR(a.y, b.y, 1e-5); CHECK_NEAR(a.z, b.z, 1e-5);
    CHECK_NEAR(view.Transposed().Transposed().m[1][2], view.m[1][2], 0.0);
}

static void TestTonemap() {
    using namespace Reference;
    CHECK_NEAR(ACESFilm(0.0f), 0.0, 1e-6);
    CHECK(ACESFilm(100.0f) <= 1.0f);
    float prev = 0.0f;
    for (float x = 0.0f; x < 20.0f; x += 0.1f) {
        const float y = ACESFilm(x);
        CHECK(y >= prev - 1e-6f);
        prev = y;
    }
    CHECK_NEAR(LinearToSRGB(0.0f), 0.0, 1e-7);
    CHECK_NEAR(LinearToSRGB(1.0f), 1.0, 1e-6);
    CHECK_NEAR(LinearToSRGB(0.5f), 0.735357, 1e-4);
    // Continuidade no ponto de junção da curva sRGB.
    CHECK_NEAR(LinearToSRGB(0.0031308f), LinearToSRGB(0.0031309f), 1e-4);
}

int main() {
    TestGGXNormalization();
    TestFresnel();
    TestVisibilityFinite();
    TestAttenuation();
    TestSpotCone();
    TestHandles();
    TestBudgetKeepsNearest();
    TestLayerMaskAndExposure();
    TestUploadRoundTrip();
    TestQualityTiers();
    TestMat4();
    TestTonemap();
    if (g_failures == 0) std::printf("Todos os testes passaram.\n");
    else std::printf("%d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
