#pragma once
// Converte a cena analítica do renderizador de software em triângulos (para o BVH). Esferas viram UV-spheres
// inscritas; planos viram quadrados finitos (`planeHalfExtent`).
#include <vector>

#include "LightPrism/Rt/Bvh.hpp"
#include "LightPrism/Soft/SoftRenderer.hpp"

namespace LightPrism::Soft {

inline std::vector<Rt::Triangle> TessellateScene(const Scene& s, uint32_t segments = 32, float planeHalfExtent = 60.0f) {
    std::vector<Rt::Triangle> out;
    const uint32_t nu = segments, nv = segments / 2;
    for (const Sphere& sp : s.spheres) {
        auto P = [&](uint32_t i, uint32_t j) {
            const float th = kPi * float(j) / float(nv), ph = 2.0f * kPi * float(i % nu) / float(nu);
            return sp.center + Vec3{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)} * sp.radius;
        };
        for (uint32_t j = 0; j < nv; ++j)
            for (uint32_t i = 0; i < nu; ++i) {
                const Vec3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
                if (j != 0) out.push_back({a, b, c});
                if (j != nv - 1) out.push_back({a, c, d});
            }
    }
    for (const Plane& p : s.planes) {
        const Vec3 n = Normalize(p.normal);
        const Vec3 up = std::fabs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 t = Normalize(Cross(up, n)) * planeHalfExtent, b = Cross(n, Normalize(t)) * planeHalfExtent;
        const Vec3 c0 = p.point - t - b, c1 = p.point + t - b, c2 = p.point + t + b, c3 = p.point - t + b;
        out.push_back({c0, c1, c2});
        out.push_back({c0, c2, c3});
    }
    return out;
}

} // namespace LightPrism::Soft
