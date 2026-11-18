#pragma once
// Culling por frustum (esfera contra 6 planos), puro: sem GPU, sem RHI.
// Base para desenhar so o que esta na tela (ver docs/PESQUISA_MILHARES_OBJETOS.md, prioridade P2).
// A matriz vem em colunas (m[coluna][linha]), a mesma convencao de tool::math::Mat4.
// depthZeroToOne: true para D3D/Vulkan (profundidade em [0,1]); false para GL (profundidade em [-1,1]).
// Ainda NAO esta ligada ao renderizador: ligar exige teste visual, senao objetos podem sumir.
#include <cmath>

namespace tool::render {

struct FrustumPlane { float n[3]; float d; };  // n . p + d >= 0 do lado de dentro, n normalizado
struct Frustum { FrustumPlane p[6]; };

inline Frustum frustumFromViewProj(const float m[4][4], bool depthZeroToOne) {
  float r[4][4];  // r[i][c] = linha i, coluna c
  for (int i = 0; i < 4; ++i) for (int c = 0; c < 4; ++c) r[i][c] = m[c][i];
  float raw[6][4];
  for (int k = 0; k < 4; ++k) {
    raw[0][k] = r[3][k] + r[0][k];   // esquerda
    raw[1][k] = r[3][k] - r[0][k];   // direita
    raw[2][k] = r[3][k] + r[1][k];   // baixo
    raw[3][k] = r[3][k] - r[1][k];   // cima
    raw[4][k] = depthZeroToOne ? r[2][k] : r[3][k] + r[2][k];  // proximo
    raw[5][k] = r[3][k] - r[2][k];   // distante
  }
  Frustum f{};
  for (int i = 0; i < 6; ++i) {
    const float len = std::sqrt(raw[i][0] * raw[i][0] + raw[i][1] * raw[i][1] + raw[i][2] * raw[i][2]);
    const float inv = len > 0.f ? 1.f / len : 0.f;
    for (int k = 0; k < 3; ++k) f.p[i].n[k] = raw[i][k] * inv;
    f.p[i].d = raw[i][3] * inv;
  }
  return f;
}

// Verdadeiro se a esfera (centro e raio no mundo) toca o frustum. Conservador: pode aceitar uma esfera que
// so encosta de fora, nunca descarta uma que esta visivel.
inline bool sphereInFrustum(const Frustum& f, float x, float y, float z, float radius) {
  for (const FrustumPlane& pl : f.p) {
    const float dist = pl.n[0] * x + pl.n[1] * y + pl.n[2] * z + pl.d;
    if (dist < -radius) return false;
  }
  return true;
}

}  // namespace tool::render
