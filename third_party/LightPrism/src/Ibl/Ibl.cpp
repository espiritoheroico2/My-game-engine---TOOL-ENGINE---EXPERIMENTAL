#include "LightPrism/Ibl/Ibl.hpp"

#include <algorithm>
#include <cmath>

#include "LightPrism/Sampling.hpp"

namespace LightPrism::Ibl {

namespace {

constexpr float kShK[9] = {0.282095f, 0.488603f, 0.488603f, 0.488603f, 1.092548f, 1.092548f, 0.315392f, 1.092548f, 0.546274f};

// Polinômios da base (sem as constantes), mesma ordem do HLSL.
void ShPoly(const Vec3& d, float p[9]) {
    p[0] = 1.0f; p[1] = d.y; p[2] = d.z; p[3] = d.x;
    p[4] = d.x * d.y; p[5] = d.y * d.z; p[6] = 3.0f * d.z * d.z - 1.0f; p[7] = d.x * d.z; p[8] = d.x * d.x - d.y * d.y;
}

// H em espaço tangente (z = normal), distribuído por D(h)*NoH.
Vec3 ImportanceSampleGGX(float u1, float u2, float roughness) {
    const float r = std::max(roughness, Reference::kMinRoughness);
    const float a2 = r * r * r * r;
    const float phi = 2.0f * kPi * u1;
    const float cosT = std::sqrt((1.0f - u2) / (1.0f + (a2 - 1.0f) * u2));
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    return {sinT * std::cos(phi), sinT * std::sin(phi), cosT};
}

void MakeBasis(const Vec3& n, Vec3& t, Vec3& b) { MakeTangentBasis(n, t, b); }

} // namespace

// ---------------------------------------------------------------- SH
void ShBasis(const Vec3& d, float out[9]) {
    float p[9];
    ShPoly(d, p);
    for (int i = 0; i < 9; ++i) out[i] = kShK[i] * p[i];
}

Sh9 ProjectToSh(const EnvFn& env, uint32_t res) {
    double acc[9][3] = {};
    const uint32_t nz = res, nphi = res * 2;
    const double dw = 4.0 * double(kPi) / (double(nz) * double(nphi));
    for (uint32_t i = 0; i < nz; ++i) {
        const float z = 1.0f - 2.0f * (float(i) + 0.5f) / float(nz);
        const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
        for (uint32_t j = 0; j < nphi; ++j) {
            const float phi = 2.0f * kPi * (float(j) + 0.5f) / float(nphi);
            const Vec3 d{s * std::cos(phi), s * std::sin(phi), z};
            const Vec3 L = env(d);
            float b[9];
            ShBasis(d, b);
            for (int k = 0; k < 9; ++k) {
                acc[k][0] += double(L.x) * b[k] * dw;
                acc[k][1] += double(L.y) * b[k] * dw;
                acc[k][2] += double(L.z) * b[k] * dw;
            }
        }
    }
    Sh9 sh;
    for (int k = 0; k < 9; ++k) sh.c[k] = {float(acc[k][0]), float(acc[k][1]), float(acc[k][2])};
    return sh;
}

Sh9 ConvolveCosine(const Sh9& r) {
    // A_l / pi: 1, 2/3, 1/4  (A0 = pi, A1 = 2pi/3, A2 = pi/4)
    const float band[3] = {1.0f, 2.0f / 3.0f, 0.25f};
    Sh9 o;
    for (int k = 0; k < 9; ++k) o.c[k] = r.c[k] * band[k == 0 ? 0 : (k < 4 ? 1 : 2)];
    return o;
}

Vec3 EvalSh(const Sh9& sh, const Vec3& n, int bands) {
    float b[9];
    ShBasis(n, b);
    Vec3 r{};
    const int count = bands >= 3 ? 9 : (bands == 2 ? 4 : 1);
    for (int k = 0; k < count; ++k) r = r + sh.c[k] * b[k];
    return {std::max(r.x, 0.0f), std::max(r.y, 0.0f), std::max(r.z, 0.0f)};
}

IblGPU MakeIblGPU(const Sh9& c, float intensity, uint32_t mipCount, uint32_t flags) {
    IblGPU g{};
    for (int k = 0; k < 9; ++k) {
        g.sh[k][0] = c.c[k].x * kShK[k];
        g.sh[k][1] = c.c[k].y * kShK[k];
        g.sh[k][2] = c.c[k].z * kShK[k];
    }
    g.intensity = intensity;
    g.specularMaxMip = mipCount > 0 ? float(mipCount - 1) : 0.0f;
    g.flags = flags;
    return g;
}

Vec3 EvaluateIrradiancePacked(const IblGPU& g, const Vec3& n) {
    float p[9];
    ShPoly(n, p);
    Vec3 r{};
    const int count = (g.flags & IblFlag_ShL1) ? 4 : 9;
    for (int k = 0; k < count; ++k) r = r + Vec3{g.sh[k][0], g.sh[k][1], g.sh[k][2]} * p[k];
    return {std::max(r.x, 0.0f), std::max(r.y, 0.0f), std::max(r.z, 0.0f)};
}

// ---------------------------------------------------------------- Cubemap
Vec3 CubeFaceUVToDir(uint32_t face, float a, float b) {
    switch (face) {
        case 0: return Normalize({1, b, -a});
        case 1: return Normalize({-1, b, a});
        case 2: return Normalize({a, 1, -b});
        case 3: return Normalize({a, -1, b});
        case 4: return Normalize({a, b, 1});
        default: return Normalize({-a, b, -1});
    }
}

void CubeDirToFaceUV(const Vec3& d, uint32_t& face, float& a, float& b) {
    const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    if (ax >= ay && ax >= az) {
        if (d.x > 0) { face = 0; a = -d.z / ax; b = d.y / ax; }
        else         { face = 1; a = d.z / ax;  b = d.y / ax; }
    } else if (ay >= az) {
        if (d.y > 0) { face = 2; a = d.x / ay; b = -d.z / ay; }
        else         { face = 3; a = d.x / ay; b = d.z / ay; }
    } else {
        if (d.z > 0) { face = 4; a = d.x / az;  b = d.y / az; }
        else         { face = 5; a = -d.x / az; b = d.y / az; }
    }
}

Vec3 SampleCubemap(const Cubemap& cm, const Vec3& dir) {
    uint32_t f;
    float a, b;
    CubeDirToFaceUV(dir, f, a, b);
    const float s = float(cm.size);
    const float fx = std::min(std::max((a * 0.5f + 0.5f) * s - 0.5f, 0.0f), s - 1.0f);
    const float fy = std::min(std::max((0.5f - b * 0.5f) * s - 0.5f, 0.0f), s - 1.0f);
    const uint32_t x0 = uint32_t(fx), y0 = uint32_t(fy);
    const uint32_t x1 = std::min(x0 + 1, cm.size - 1), y1 = std::min(y0 + 1, cm.size - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    return Lerp(Lerp(cm.Texel(f, x0, y0), cm.Texel(f, x1, y0), tx), Lerp(cm.Texel(f, x0, y1), cm.Texel(f, x1, y1), tx), ty);
}

PrefilteredEnv BuildPrefilteredEnv(const EnvFn& env, uint32_t baseSize, uint32_t mipCount, uint32_t samples) {
    PrefilteredEnv pe;
    pe.mipCount = std::max(mipCount, 1u);
    pe.mips.resize(pe.mipCount);
    for (uint32_t m = 0; m < pe.mipCount; ++m) {
        Cubemap& cm = pe.mips[m];
        cm.size = std::max(8u, baseSize >> m);
        const float rough = pe.mipCount > 1 ? float(m) / float(pe.mipCount - 1) : 0.0f;
        for (uint32_t f = 0; f < 6; ++f) {
            cm.face[f].resize(size_t(cm.size) * cm.size);
            for (uint32_t y = 0; y < cm.size; ++y)
                for (uint32_t x = 0; x < cm.size; ++x) {
                    const float a = (float(x) + 0.5f) / float(cm.size) * 2.0f - 1.0f;
                    const float b = 1.0f - (float(y) + 0.5f) / float(cm.size) * 2.0f;
                    const Vec3 N = CubeFaceUVToDir(f, a, b);
                    Vec3 result;
                    if (m == 0) {
                        result = env(N);
                    } else {
                        Vec3 T, B;
                        MakeBasis(N, T, B);
                        Vec3 sum{};
                        float wsum = 0.0f;
                        for (uint32_t i = 0; i < samples; ++i) {
                            const Vec3 h = ImportanceSampleGGX((float(i) + 0.5f) / float(samples), RadicalInverse(i), rough);
                            const Vec3 H = T * h.x + B * h.y + N * h.z;
                            const Vec3 L = H * (2.0f * Dot(N, H)) - N;  // V = N
                            const float NoL = Dot(N, L);
                            if (NoL > 0.0f) { sum = sum + env(L) * NoL; wsum += NoL; }
                        }
                        result = wsum > 0.0f ? sum * (1.0f / wsum) : env(N);
                    }
                    cm.face[f][size_t(y) * cm.size + x] = result;
                }
        }
    }
    return pe;
}

Vec3 SamplePrefiltered(const PrefilteredEnv& pe, const Vec3& dir, float roughness) {
    const float lod = Saturate(roughness) * float(pe.mipCount - 1);
    const uint32_t m0 = uint32_t(lod);
    const uint32_t m1 = std::min(m0 + 1, pe.mipCount - 1);
    const float t = lod - float(m0);
    const Vec3 a = SampleCubemap(pe.mips[m0], dir);
    return m1 == m0 ? a : Lerp(a, SampleCubemap(pe.mips[m1], dir), t);
}

// ---------------------------------------------------------------- LUT
void IntegrateBrdf(float NoV, float roughness, uint32_t samples, float& A, float& B) {
    NoV = std::min(std::max(NoV, 1e-3f), 1.0f);
    const Vec3 V{std::sqrt(1.0f - NoV * NoV), 0.0f, NoV};
    double a = 0.0, b = 0.0;
    for (uint32_t i = 0; i < samples; ++i) {
        const Vec3 H = ImportanceSampleGGX((float(i) + 0.5f) / float(samples), RadicalInverse(i), roughness);
        const float VoH = Dot(V, H);
        const Vec3 L = H * (2.0f * VoH) - V;
        const float NoL = L.z, NoH = H.z;
        if (NoL <= 0.0f || VoH <= 0.0f) continue;
        const float vis = Reference::V_SmithGGXCorrelated(NoV, NoL, roughness);
        const float gv = 4.0f * vis * NoL * VoH / NoH;
        const float m = 1.0f - VoH, m2 = m * m;
        const float Fc = m2 * m2 * m;
        a += double((1.0f - Fc) * gv);
        b += double(Fc * gv);
    }
    A = float(a / samples);
    B = float(b / samples);
}

BrdfLut BuildBrdfLut(uint32_t size, uint32_t samples) {
    BrdfLut lut;
    lut.size = size;
    lut.ab.resize(size_t(size) * size * 2);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            float A, B;
            IntegrateBrdf((float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), samples, A, B);
            lut.ab[(size_t(y) * size + x) * 2 + 0] = A;
            lut.ab[(size_t(y) * size + x) * 2 + 1] = B;
        }
    return lut;
}

void SampleBrdfLut(const BrdfLut& lut, float NoV, float roughness, float& A, float& B) {
    const float s = float(lut.size);
    const float fx = std::min(std::max(Saturate(NoV) * s - 0.5f, 0.0f), s - 1.0f);
    const float fy = std::min(std::max(Saturate(roughness) * s - 0.5f, 0.0f), s - 1.0f);
    const uint32_t x0 = uint32_t(fx), y0 = uint32_t(fy);
    const uint32_t x1 = std::min(x0 + 1, lut.size - 1), y1 = std::min(y0 + 1, lut.size - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    auto at = [&](uint32_t x, uint32_t y, int c) { return lut.ab[(size_t(y) * lut.size + x) * 2 + c]; };
    A = Lerp(Lerp(at(x0, y0, 0), at(x1, y0, 0), tx), Lerp(at(x0, y1, 0), at(x1, y1, 0), tx), ty);
    B = Lerp(Lerp(at(x0, y0, 1), at(x1, y0, 1), tx), Lerp(at(x0, y1, 1), at(x1, y1, 1), tx), ty);
}

// ---------------------------------------------------------------- Formatos compactos
namespace {

// float sem sinal com 5 bits de expoente (bias 15) e `mant` bits de mantissa (6 para 11 bits, 5 para 10 bits).
uint32_t FloatToUF(float v, int mant) {
    const float maxv = (2.0f - std::ldexp(1.0f, -mant)) * 32768.0f;
    if (!(v > 0.0f)) return 0;                 // negativo/NaN -> 0
    if (v >= maxv) return (30u << mant) | ((1u << mant) - 1u);
    int e;
    const float f = std::frexp(v, &e);         // v = f * 2^e, f em [0.5,1)
    e -= 1;                                    // v = (2f) * 2^e, 2f em [1,2)
    if (e < -14) {                             // denormal
        const uint32_t m = uint32_t(std::lround(std::ldexp(v, 14 + mant)));
        return m >= (1u << mant) ? (1u << mant) : m;
    }
    uint32_t m = uint32_t(std::lround((2.0f * f - 1.0f) * float(1u << mant)));
    if (m == (1u << mant)) { m = 0; ++e; }
    if (e > 15) return (30u << mant) | ((1u << mant) - 1u);
    return (uint32_t(e + 15) << mant) | m;
}

float UFToFloat(uint32_t bits, int mant) {
    const uint32_t e = bits >> mant, m = bits & ((1u << mant) - 1u);
    if (e == 0) return std::ldexp(float(m), -14 - mant);
    return std::ldexp(1.0f + float(m) / float(1u << mant), int(e) - 15);
}

} // namespace

uint32_t PackR11G11B10F(const Vec3& c) {
    return FloatToUF(c.x, 6) | (FloatToUF(c.y, 6) << 11) | (FloatToUF(c.z, 5) << 22);
}

Vec3 UnpackR11G11B10F(uint32_t v) {
    return {UFToFloat(v & 0x7FFu, 6), UFToFloat((v >> 11) & 0x7FFu, 6), UFToFloat((v >> 22) & 0x3FFu, 5)};
}

uint32_t PackRGB9E5(const Vec3& c) {
    const float kMax = 65408.0f;  // (511/512) * 2^16
    const float r = std::min(std::max(c.x, 0.0f), kMax), g = std::min(std::max(c.y, 0.0f), kMax), b = std::min(std::max(c.z, 0.0f), kMax);
    const float maxc = std::max(r, std::max(g, b));
    int es = maxc > 0.0f ? std::max(-16, int(std::floor(std::log2(maxc)))) + 16 : 0;  // expoente + bias 15 + 1
    float denom = std::ldexp(1.0f, es - 24);
    if (uint32_t(std::floor(maxc / denom + 0.5f)) == 512u) { ++es; denom *= 2.0f; }
    const uint32_t mr = uint32_t(std::floor(r / denom + 0.5f)), mg = uint32_t(std::floor(g / denom + 0.5f)), mb = uint32_t(std::floor(b / denom + 0.5f));
    return mr | (mg << 9) | (mb << 18) | (uint32_t(es) << 27);
}

Vec3 UnpackRGB9E5(uint32_t v) {
    const float scale = std::ldexp(1.0f, int(v >> 27) - 24);
    return {float(v & 511u) * scale, float((v >> 9) & 511u) * scale, float((v >> 18) & 511u) * scale};
}

uint64_t PackedPrefiltered::Bytes() const {
    uint64_t b = 0;
    for (const Mip& m : mips) b += uint64_t(m.size) * m.size * 6 * 4;
    return b;
}

uint64_t PrefilteredBytes(const PrefilteredEnv& pe) {
    uint64_t b = 0;
    for (const Cubemap& m : pe.mips) b += uint64_t(m.size) * m.size * 6 * 12;
    return b;
}

PackedPrefiltered PackPrefiltered(const PrefilteredEnv& pe, HdrFormat fmt) {
    PackedPrefiltered p;
    p.format = fmt;
    p.mips.resize(pe.mips.size());
    for (size_t m = 0; m < pe.mips.size(); ++m) {
        p.mips[m].size = pe.mips[m].size;
        for (int f = 0; f < 6; ++f) {
            p.mips[m].face[f].resize(pe.mips[m].face[f].size());
            for (size_t i = 0; i < pe.mips[m].face[f].size(); ++i)
                p.mips[m].face[f][i] = fmt == HdrFormat::RGB9E5 ? PackRGB9E5(pe.mips[m].face[f][i]) : PackR11G11B10F(pe.mips[m].face[f][i]);
        }
    }
    return p;
}

PrefilteredEnv UnpackPrefiltered(const PackedPrefiltered& p) {
    PrefilteredEnv pe;
    pe.mipCount = uint32_t(p.mips.size());
    pe.mips.resize(p.mips.size());
    for (size_t m = 0; m < p.mips.size(); ++m) {
        pe.mips[m].size = p.mips[m].size;
        for (int f = 0; f < 6; ++f) {
            pe.mips[m].face[f].resize(p.mips[m].face[f].size());
            for (size_t i = 0; i < p.mips[m].face[f].size(); ++i)
                pe.mips[m].face[f][i] = p.format == HdrFormat::RGB9E5 ? UnpackRGB9E5(p.mips[m].face[f][i]) : UnpackR11G11B10F(p.mips[m].face[f][i]);
        }
    }
    return pe;
}

PackedBrdfLut PackBrdfLut(const BrdfLut& lut) {
    PackedBrdfLut p;
    p.size = lut.size;
    p.rg.resize(lut.ab.size());
    for (size_t i = 0; i < lut.ab.size(); ++i) p.rg[i] = uint8_t(std::lround(Saturate(lut.ab[i]) * 255.0f));
    return p;
}

BrdfLut UnpackBrdfLut(const PackedBrdfLut& p) {
    BrdfLut lut;
    lut.size = p.size;
    lut.ab.resize(p.rg.size());
    for (size_t i = 0; i < p.rg.size(); ++i) lut.ab[i] = float(p.rg[i]) / 255.0f;
    return lut;
}

// ---------------------------------------------------------------- Avaliação
IblData BuildIblData(const EnvFn& env, uint32_t shRes, uint32_t cubeSize, uint32_t mipCount, uint32_t lutSize) {
    IblData d;
    d.irradiance = ConvolveCosine(ProjectToSh(env, shRes));
    d.prefiltered = BuildPrefilteredEnv(env, cubeSize, mipCount);
    d.lut = BuildBrdfLut(lutSize);
    return d;
}

float DominantDirFactor(float roughness) {
    // Lagarde: o lobo especular rugoso se afasta do vetor de reflexão em direção à normal.
    const float a = roughness * roughness;
    const float smooth = Saturate(1.0f - a);
    return smooth * (std::sqrt(smooth) + a);
}

float SpecularOcclusion(float NoV, float ao, float roughness) {
    return Saturate(std::pow(NoV + ao, std::exp2(-16.0f * roughness - 1.0f)) - 1.0f + ao);
}

IndirectResult EvaluateIndirect(const IblData& ibl, const Reference::SurfaceData& s, float ao) {
    const float rawNoV = Dot(s.N, s.V);
    const float NoV = std::max(rawNoV, 1e-4f);
    const Vec3 F0 = Lerp(Vec3{0.04f, 0.04f, 0.04f}, s.albedo, s.metallic);
    float A, B;
    SampleBrdfLut(ibl.lut, NoV, s.roughness, A, B);
    const Vec3 Fss = F0 * A + Vec3{B, B, B};
    const Vec3 kD = (Vec3{1, 1, 1} - Vec3{std::min(Fss.x, 1.0f), std::min(Fss.y, 1.0f), std::min(Fss.z, 1.0f)}) * (1.0f - s.metallic);

    IndirectResult r;
    r.diffuse = kD * s.albedo * EvalSh(ibl.irradiance, s.N) * (ibl.intensity * ao);
    Vec3 R = s.N * (2.0f * rawNoV) - s.V;
    R = Normalize(Lerp(s.N, R, DominantDirFactor(s.roughness)));
    r.specular = SamplePrefiltered(ibl.prefiltered, R, s.roughness) * Fss * (ibl.intensity * SpecularOcclusion(NoV, ao, s.roughness));
    return r;
}

Vec3 IntegrateIndirectReference(const EnvFn& env, const Reference::SurfaceData& s, uint32_t res) {
    const uint32_t nz = res, nphi = res * 2;
    const double dw = 4.0 * double(kPi) / (double(nz) * double(nphi));
    double acc[3] = {};
    for (uint32_t i = 0; i < nz; ++i) {
        const float z = 1.0f - 2.0f * (float(i) + 0.5f) / float(nz);
        const float sn = std::sqrt(std::max(0.0f, 1.0f - z * z));
        for (uint32_t j = 0; j < nphi; ++j) {
            const float phi = 2.0f * kPi * (float(j) + 0.5f) / float(nphi);
            const Vec3 L{sn * std::cos(phi), sn * std::sin(phi), z};
            if (Dot(s.N, L) <= 0.0f) continue;
            const Vec3 c = Reference::EvaluateBRDF(s, L, env(L));
            acc[0] += double(c.x) * dw; acc[1] += double(c.y) * dw; acc[2] += double(c.z) * dw;
        }
    }
    return {float(acc[0]), float(acc[1]), float(acc[2])};
}

Vec3 ProceduralSky::Radiance(const Vec3& d) const {
    Vec3 c;
    if (d.y >= 0.0f) c = Lerp(horizon, zenith, std::sqrt(d.y));
    else c = Lerp(horizon * 0.5f, ground, std::sqrt(-d.y));
    const float sd = std::max(0.0f, Dot(d, Normalize(sunDir)));
    return c + sunColor * (sunIntensity * std::pow(sd, sunPower));
}

} // namespace LightPrism::Ibl
