#pragma once
// Sombra planar de Blinn (barata, ideal para o tier Mobile): achata a geometria do caster sobre um plano receptor.
// Sem shadow map, sem memória extra, 1 draw por caster. Só serve para receptor plano.
//   plano: dot(n, x) + d = 0 ;  luz: L = (l.xyz, w)  (w = 1 pontual na posição l; w = 0 direcional, l aponta PARA a luz)
//   T = dot(P, L) * I - L * P^T        com P = (n, d).    p' = T * p  (depois da divisão por w)
// O desenho deve usar stencil (incrementa e rejeita se != 0) para não escurecer duas vezes, e subir o plano um
// epsilon (ou polygon offset) contra z-fighting.
#include "LightPrism/Mat4.hpp"

namespace LightPrism {

inline Mat4 MakePlanarShadowMatrix(const Vec3& planeNormal, float planeD, const Vec3& lightVec, float lightW) {
    const float P[4] = {planeNormal.x, planeNormal.y, planeNormal.z, planeD};
    const float L[4] = {lightVec.x, lightVec.y, lightVec.z, lightW};
    const float dotPL = P[0] * L[0] + P[1] * L[1] + P[2] * L[2] + P[3] * L[3];
    Mat4 m = Mat4::Identity();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m.m[r][c] = (r == c ? dotPL : 0.0f) - L[r] * P[c];
    return m;
}

// Aplica a matriz a um ponto e faz a divisão perspectiva.
inline Vec3 ApplyPlanarShadow(const Mat4& t, const Vec3& p) {
    const float x = t.m[0][0] * p.x + t.m[0][1] * p.y + t.m[0][2] * p.z + t.m[0][3];
    const float y = t.m[1][0] * p.x + t.m[1][1] * p.y + t.m[1][2] * p.z + t.m[1][3];
    const float z = t.m[2][0] * p.x + t.m[2][1] * p.y + t.m[2][2] * p.z + t.m[2][3];
    const float w = t.m[3][0] * p.x + t.m[3][1] * p.y + t.m[3][2] * p.z + t.m[3][3];
    return {x / w, y / w, z / w};
}

} // namespace LightPrism
