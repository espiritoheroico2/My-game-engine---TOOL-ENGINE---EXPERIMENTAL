// ToolEngine - modelo de iluminacao (referencia em CPU do que os shaders HLSL fazem).
// Forward+ por clusters: luzes sao atribuídas a uma grade 3D de frustum; o pixel shader le
// so as luzes do seu cluster. Sombras em cascata para a luz direcional.
#pragma once
#include "tool/math/math.hpp"

#include <vector>

namespace tool {

enum class LightType : u32 { Directional = 0, Point = 1, Spot = 2 };

// Layout identico ao cbuffer/StructuredBuffer do HLSL (assets/shaders/lighting.hlsli): 64 bytes.
struct alignas(16) GpuLight {
  f32 position[3];  f32 range;       // 16
  f32 color[3];     f32 intensity;   // 32
  f32 direction[3]; f32 spotCos;     // 48 (cos do angulo externo)
  u32 type;         u32 shadowIndex; f32 spotInnerCos; u32 pad; // 64
};
static_assert(sizeof(GpuLight) == 64, "GpuLight deve ter 64 bytes (layout do HLSL)");

struct Light {
  LightType type = LightType::Point;
  Vec3 position{}, direction{0, -1, 0}, color{1, 1, 1};
  f32 intensity = 1.f, range = 10.f;
  f32 innerAngle = 0.4f, outerAngle = 0.6f; // radianos (spot)
  bool castShadow = false;
};

inline GpuLight toGpu(const Light& l, u32 shadowIndex = 0xFFFFFFFFu) {
  GpuLight g{};
  g.position[0] = l.position.x; g.position[1] = l.position.y; g.position[2] = l.position.z; g.range = l.range;
  g.color[0] = l.color.x; g.color[1] = l.color.y; g.color[2] = l.color.z; g.intensity = l.intensity;
  const Vec3 d = normalize(l.direction);
  g.direction[0] = d.x; g.direction[1] = d.y; g.direction[2] = d.z;
  g.spotCos = std::cos(l.outerAngle); g.spotInnerCos = std::cos(l.innerAngle);
  g.type = static_cast<u32>(l.type); g.shadowIndex = shadowIndex;
  return g;
}

// Atenuacao fisica suavizada com janela (igual a do HLSL): 1/d^2 * (1 - (d/r)^4)^2.
inline f32 attenuation(f32 dist, f32 range) {
  if (dist >= range) return 0.f;
  const f32 d2 = std::max(dist * dist, 1e-4f);
  const f32 w = std::clamp(1.f - std::pow(dist / range, 4.f), 0.f, 1.f);
  return w * w / d2;
}

inline f32 spotFactor(Vec3 lightDir, Vec3 toPoint, f32 innerCos, f32 outerCos) {
  const f32 c = dot(normalize(lightDir), normalize(toPoint)); // toPoint = da luz ao ponto
  return std::clamp((c - outerCos) / std::max(innerCos - outerCos, 1e-4f), 0.f, 1.f);
}

// ---- Cascatas de sombra (esquema pratico: mistura log/linear) ----------------------------
inline std::vector<f32> cascadeSplits(f32 nearZ, f32 farZ, int count, f32 lambda = 0.75f) {
  std::vector<f32> s(static_cast<size_t>(count) + 1);
  s[0] = nearZ;
  for (int i = 1; i <= count; ++i) {
    const f32 t = static_cast<f32>(i) / static_cast<f32>(count);
    const f32 lg = nearZ * std::pow(farZ / nearZ, t), lin = nearZ + (farZ - nearZ) * t;
    s[static_cast<size_t>(i)] = lambda * lg + (1.f - lambda) * lin;
  }
  s[static_cast<size_t>(count)] = farZ;
  return s;
}

// ---- Clusters de luz (Forward+) -----------------------------------------------------------
struct ClusterGrid {
  u32 x = 16, y = 9, z = 24;
  u32 count() const { return x * y * z; }
};

struct ClusterAssignment {
  std::vector<u32> offsets;   // por cluster: inicio em `indices`
  std::vector<u32> counts;    // por cluster: quantas luzes
  std::vector<u32> indices;   // indices de luz
  u32 maxPerCluster = 0;
};

inline u32 clusterSlice(f32 viewDepth, f32 nearZ, f32 farZ, u32 slices) {
  if (viewDepth <= nearZ) return 0;
  const f32 t = std::log(viewDepth / nearZ) / std::log(farZ / nearZ);
  return std::min(slices - 1, static_cast<u32>(std::max(0.f, t) * static_cast<f32>(slices)));
}

// Atribui luzes pontuais/spot aos clusters cuja AABB (no espaco da camera) a esfera da luz toca.
// `view` leva mundo -> camera; a camera olha para -Z.
inline ClusterAssignment assignLights(const ClusterGrid& g, const Mat4& view, f32 fovY, f32 aspect, f32 nearZ, f32 farZ,
                                      const std::vector<Light>& lights, u32 maxPerCluster = 128) {
  ClusterAssignment out;
  const u32 n = g.count();
  std::vector<std::vector<u32>> per(n);
  const f32 tanY = std::tan(fovY * 0.5f), tanX = tanY * aspect;
  for (u32 li = 0; li < lights.size(); ++li) {
    const Light& L = lights[li];
    if (L.type == LightType::Directional) continue; // direcional afeta todos: tratada fora dos clusters
    const Vec3 c = view.transformPoint(L.position);
    const f32 r = L.range;
    for (u32 iz = 0; iz < g.z; ++iz) {
      const f32 z0 = nearZ * std::pow(farZ / nearZ, static_cast<f32>(iz) / static_cast<f32>(g.z));
      const f32 z1 = nearZ * std::pow(farZ / nearZ, static_cast<f32>(iz + 1) / static_cast<f32>(g.z));
      if (-c.z + r < z0 || -c.z - r > z1) continue;
      for (u32 iy = 0; iy < g.y; ++iy)
        for (u32 ix = 0; ix < g.x; ++ix) {
          // limites XY da celula na profundidade mais larga (z1) => conservador
          const f32 nx0 = (static_cast<f32>(ix) / static_cast<f32>(g.x)) * 2.f - 1.f, nx1 = (static_cast<f32>(ix + 1) / static_cast<f32>(g.x)) * 2.f - 1.f;
          const f32 ny0 = (static_cast<f32>(iy) / static_cast<f32>(g.y)) * 2.f - 1.f, ny1 = (static_cast<f32>(iy + 1) / static_cast<f32>(g.y)) * 2.f - 1.f;
          AABB b;
          for (f32 zz : {z0, z1})
            for (f32 nx : {nx0, nx1})
              for (f32 ny : {ny0, ny1}) b.grow({nx * tanX * zz, ny * tanY * zz, -zz});
          // distancia^2 do centro da esfera a AABB
          f32 d2 = 0;
          const f32 cc[3] = {c.x, c.y, c.z}, lo[3] = {b.min.x, b.min.y, b.min.z}, hi[3] = {b.max.x, b.max.y, b.max.z};
          for (int a = 0; a < 3; ++a) { if (cc[a] < lo[a]) d2 += (lo[a] - cc[a]) * (lo[a] - cc[a]); else if (cc[a] > hi[a]) d2 += (cc[a] - hi[a]) * (cc[a] - hi[a]); }
          if (d2 <= r * r) {
            auto& v = per[(iz * g.y + iy) * g.x + ix];
            if (v.size() < maxPerCluster) v.push_back(li);
          }
        }
    }
  }
  out.offsets.resize(n); out.counts.resize(n);
  for (u32 i = 0; i < n; ++i) {
    out.offsets[i] = static_cast<u32>(out.indices.size());
    out.counts[i] = static_cast<u32>(per[i].size());
    out.maxPerCluster = std::max(out.maxPerCluster, out.counts[i]);
    out.indices.insert(out.indices.end(), per[i].begin(), per[i].end());
  }
  return out;
}

} // namespace tool
