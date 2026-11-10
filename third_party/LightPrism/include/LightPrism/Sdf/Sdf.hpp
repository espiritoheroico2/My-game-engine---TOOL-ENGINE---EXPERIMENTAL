#pragma once
// Fase 6b — Campos de distância com sinal (SDF): sombras suaves com UM raio, e volume baked em int8.
//
// Sphere tracing: o campo diz "nada está mais perto que d", então o raio avança d com segurança. Quanto o raio passa perto de um
// oclusor (h) em relação à distância já percorrida (t), maior a fração do disco da luz coberta: a penumbra sai de graça do mesmo
// laço, sem raios extras (um raio RT por cone custaria 16–64).
//   vis = suavização( 0.5 + 0.5 * h / (t * tan(coneHalfAngle)) ),  mínimo ao longo do raio.
// `SdfVolume` guarda o campo truncado em int8 (1 B/voxel, distância máx. configurável = o LIMITE DE DISTÂNCIA do SDF): custo por
// passo O(1), independente do número de objetos; com atualização local quando um objeto se move (como o PRT).
#include <cstdint>
#include <vector>

#include "LightPrism/Math.hpp"

namespace LightPrism::Sdf {

struct SdfSphere { Vec3 center; float radius = 1.0f; };
struct SdfPlane { Vec3 normal{0, 1, 0}; float d = 0.0f; };   // dot(n, p) = d; distância positiva do lado da normal

class SdfScene {
public:
    std::vector<SdfSphere> spheres;
    std::vector<SdfPlane> planes;
    float Distance(const Vec3& p) const {
        float best = 1e30f;
        for (const SdfSphere& s : spheres) { const float d = Length(p - s.center) - s.radius; best = d < best ? d : best; }
        for (const SdfPlane& pl : planes) { const float d = Dot(pl.normal, p) - pl.d; best = d < best ? d : best; }
        return best;
    }
};

// Volume uniforme, distância truncada em [-maxDist, maxDist] codificada em int8. Fora do volume: +maxDist (nada perto).
class SdfVolume {
public:
    void Bake(const SdfScene& scene, const Vec3& lo, const Vec3& hi, float voxel, float maxDist) {
        m_origin = lo; m_voxel = voxel; m_maxDist = maxDist;
        m_n[0] = uint32_t(std::ceil((hi.x - lo.x) / voxel)) + 1; m_n[1] = uint32_t(std::ceil((hi.y - lo.y) / voxel)) + 1; m_n[2] = uint32_t(std::ceil((hi.z - lo.z) / voxel)) + 1;
        m_data.assign(size_t(m_n[0]) * m_n[1] * m_n[2], 127);
        for (uint32_t z = 0; z < m_n[2]; ++z)
            for (uint32_t y = 0; y < m_n[1]; ++y)
                for (uint32_t x = 0; x < m_n[0]; ++x) m_data[Index(x, y, z)] = Encode(scene.Distance(Pos(x, y, z)));
    }
    // Rebake só dos voxels dentro da caixa (objeto que se moveu): devolve quantos foram refeitos.
    uint32_t Rebake(const SdfScene& scene, const Vec3& lo, const Vec3& hi) {
        const Vec3 a = (lo - Vec3{m_maxDist, m_maxDist, m_maxDist} - m_origin) * (1.0f / m_voxel), b = (hi + Vec3{m_maxDist, m_maxDist, m_maxDist} - m_origin) * (1.0f / m_voxel);
        const int x0 = std::max(0, int(std::floor(a.x))), y0 = std::max(0, int(std::floor(a.y))), z0 = std::max(0, int(std::floor(a.z)));
        const int x1 = std::min(int(m_n[0]) - 1, int(std::ceil(b.x))), y1 = std::min(int(m_n[1]) - 1, int(std::ceil(b.y))), z1 = std::min(int(m_n[2]) - 1, int(std::ceil(b.z)));
        uint32_t n = 0;
        for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) { m_data[Index(uint32_t(x), uint32_t(y), uint32_t(z))] = Encode(scene.Distance(Pos(uint32_t(x), uint32_t(y), uint32_t(z)))); ++n; }
        return n;
    }
    // Trilinear. Fora do volume: maxDist.
    float Distance(const Vec3& p) const {
        const Vec3 q = (p - m_origin) * (1.0f / m_voxel);
        if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= float(m_n[0] - 1) || q.y >= float(m_n[1] - 1) || q.z >= float(m_n[2] - 1)) return m_maxDist;
        const uint32_t x = uint32_t(q.x), y = uint32_t(q.y), z = uint32_t(q.z);
        const float fx = q.x - float(x), fy = q.y - float(y), fz = q.z - float(z);
        auto v = [&](uint32_t dx, uint32_t dy, uint32_t dz) { return float(m_data[Index(x + dx, y + dy, z + dz)]); };
        const float c00 = v(0, 0, 0) * (1 - fx) + v(1, 0, 0) * fx, c10 = v(0, 1, 0) * (1 - fx) + v(1, 1, 0) * fx;
        const float c01 = v(0, 0, 1) * (1 - fx) + v(1, 0, 1) * fx, c11 = v(0, 1, 1) * (1 - fx) + v(1, 1, 1) * fx;
        const float c0 = c00 * (1 - fy) + c10 * fy, c1 = c01 * (1 - fy) + c11 * fy;
        return (c0 * (1 - fz) + c1 * fz) * (m_maxDist / 127.0f);
    }
    uint64_t Bytes() const { return m_data.size(); }
    uint32_t Dim(int a) const { return m_n[a]; }
    float Voxel() const { return m_voxel; }
    const std::vector<int8_t>& Data() const { return m_data; }
private:
    size_t Index(uint32_t x, uint32_t y, uint32_t z) const { return (size_t(z) * m_n[1] + y) * m_n[0] + x; }
    Vec3 Pos(uint32_t x, uint32_t y, uint32_t z) const { return m_origin + Vec3{float(x), float(y), float(z)} * m_voxel; }
    int8_t Encode(float d) const { const float c = d < -m_maxDist ? -m_maxDist : (d > m_maxDist ? m_maxDist : d); return int8_t(std::lround(c / m_maxDist * 127.0f)); }
    Vec3 m_origin; float m_voxel = 1.0f, m_maxDist = 1.0f; uint32_t m_n[3] = {0, 0, 0}; std::vector<int8_t> m_data;
};

struct SdfShadowParams {
    float tMin = 0.1f;               // metros: começa afastado da própria superfície
    float tMax = 30.0f;              // LIMITE DE DISTÂNCIA
    float coneHalfAngle = 0.05f;     // rad (0 -> sombra dura)
    uint32_t maxSteps = 64;
    float minStep = 0.02f, maxStep = 4.0f;
};

// Alcance máximo seguro para um volume com distância truncada em `truncDist`: além de truncDist/tan(cone) a distância truncada
// subestima h/(t*tan) e a sombra escureceria sem motivo. Use  params.tMax = min(params.tMax, SafeShadowRange(...)).
inline float SafeShadowRange(float truncDist, float coneHalfAngle) {
    return 0.9f * truncDist / std::tan(coneHalfAngle > 1e-4f ? coneHalfAngle : 1e-4f);
}

// Fração visível de um disco de luz de raio 1 cortado por uma reta a distância `s` do centro (s em [-1,1]; s<0 = centro coberto).
inline float DiscVisibleFraction(float s) {
    s = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
    return 0.5f + (s * std::sqrt(1.0f - s * s) + std::asin(s)) / kPi;
}

// Visibilidade [0,1] ao longo de `dir` (unitária, PARA a luz). `dist` = callable float(Vec3). `steps` opcional (contagem).
// s = h / (t*tan(cone)) é a distância (em raios do disco) do raio central ao oclusor; mínimo ao longo do caminho. O raio continua
// por DENTRO do oclusor (h < 0) para dar a metade escura da penumbra; com s <= -1 o disco inteiro está coberto.
template <class DistFn>
inline float SoftShadow(const DistFn& dist, const Vec3& origin, const Vec3& dir, const SdfShadowParams& p, uint32_t* steps = nullptr) {
    const float tanA = std::tan(p.coneHalfAngle > 1e-4f ? p.coneHalfAngle : 1e-4f);
    float sMin = 1.0f, t = p.tMin;
    for (uint32_t i = 0; i < p.maxSteps && t < p.tMax; ++i) {
        const float h = dist(origin + dir * t);
        if (steps) ++*steps;
        const float s = h / (t * tanA);
        sMin = s < sMin ? s : sMin;
        if (sMin <= -1.0f) return 0.0f;
        const float a = h < 0.0f ? -h : h;
        t += a < p.minStep ? p.minStep : (a > p.maxStep ? p.maxStep : a);
    }
    return DiscVisibleFraction(sMin);
}

} // namespace LightPrism::Sdf
