#pragma once
// Culling por frustum (esfera contra 6 planos), puro: sem GPU, sem RHI.
// Base para desenhar so o que esta na tela (ver docs/PESQUISA_MILHARES_OBJETOS.md, prioridade P2).
// A matriz vem em colunas (m[coluna][linha]), a mesma convencao de tool::math::Mat4.
// depthZeroToOne: true para D3D/Vulkan (profundidade em [0,1]); false para GL (profundidade em [-1,1]).
// Usado pela GpuRenderer no culling de casters de sombra; bounds invalidos sao mantidos por seguranca.
#include <algorithm>
#include <cmath>

namespace tool::render {

struct FrustumPlane { float n[3]; float d; };  // n . p + d >= 0 do lado de dentro, n normalizado
struct Frustum { FrustumPlane p[6]; };
struct SphereBounds { float center[3] = {}; float radius = 0.f; bool valid = false; };

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

inline SphereBounds transformSphere(const float world[4][4], const SphereBounds& local) {
  SphereBounds out{};
  if (!local.valid || !std::isfinite(local.radius) || local.radius < 0.f) return out;
  for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
    if (!std::isfinite(world[c][r])) return out;
  for (int c = 0; c < 3; ++c)
    if (std::abs(world[c][3]) > 1e-5f) return out;
  if (std::abs(world[3][3] - 1.f) > 1e-5f) return out;
  for (float v : local.center) if (!std::isfinite(v)) return out;

  const float x = local.center[0], y = local.center[1], z = local.center[2];
  const float w = world[0][3] * x + world[1][3] * y + world[2][3] * z + world[3][3];
  if (!std::isfinite(w) || std::abs(w) < 1e-8f) return out;
  out.center[0] = (world[0][0] * x + world[1][0] * y + world[2][0] * z + world[3][0]) / w;
  out.center[1] = (world[0][1] * x + world[1][1] * y + world[2][1] * z + world[3][1]) / w;
  out.center[2] = (world[0][2] * x + world[1][2] * y + world[2][2] * z + world[3][2]) / w;

  float maxRowSum = 0.f, maxColumnSum = 0.f;
  for (int r = 0; r < 3; ++r) {
    float sum = 0.f;
    for (int c = 0; c < 3; ++c) sum += std::abs(world[c][r]);
    maxRowSum = std::max(maxRowSum, sum);
  }
  for (int c = 0; c < 3; ++c) {
    float sum = 0.f;
    for (int r = 0; r < 3; ++r) sum += std::abs(world[c][r]);
    maxColumnSum = std::max(maxColumnSum, sum);
  }
  out.radius = local.radius * std::sqrt(maxRowSum * maxColumnSum);
  if (!std::isfinite(out.center[0]) || !std::isfinite(out.center[1]) || !std::isfinite(out.center[2]) ||
      !std::isfinite(out.radius)) return SphereBounds{};
  out.valid = true;
  return out;
}

// Verdadeiro se a esfera (centro e raio no mundo) toca o frustum. Conservador: pode aceitar uma esfera que
// so encosta de fora, nunca descarta uma que esta visivel.
inline bool sphereInFrustum(const Frustum& f, float x, float y, float z, float radius, int planeCount) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
      !std::isfinite(radius) || radius < 0.f) return true;
  planeCount = std::clamp(planeCount, 0, 6);
  for (int i = 0; i < planeCount; ++i) {
    const FrustumPlane& pl = f.p[i];
    if (!std::isfinite(pl.n[0]) || !std::isfinite(pl.n[1]) || !std::isfinite(pl.n[2]) || !std::isfinite(pl.d)) continue;
    const float dist = pl.n[0] * x + pl.n[1] * y + pl.n[2] * z + pl.d;
    if (!std::isfinite(dist)) continue;
    if (dist < -radius - std::max(1e-5f, radius * 1e-5f)) return false;
  }
  return true;
}

inline bool sphereInFrustum(const Frustum& f, float x, float y, float z, float radius) {
  return sphereInFrustum(f, x, y, z, radius, 6);
}

inline bool sphereInFrustum(const Frustum& f, const SphereBounds& sphere) {
  if (!sphere.valid) return true;
  return sphereInFrustum(f, sphere.center[0], sphere.center[1], sphere.center[2], sphere.radius);
}

inline bool sphereInFrustumSides(const Frustum& f, const SphereBounds& sphere) {
  if (!sphere.valid) return true;
  return sphereInFrustum(f, sphere.center[0], sphere.center[1], sphere.center[2], sphere.radius, 4);
}

}  // namespace tool::render
