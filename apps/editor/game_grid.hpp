#pragma once

#include "scene_view_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace editor::game_grid {

inline constexpr std::size_t kBatchCount = 14;
inline constexpr std::size_t kXAxisBatch = 8;
inline constexpr std::size_t kYAxisBatch = 9;
inline constexpr std::size_t kZAxisBatch = 10;
inline constexpr std::size_t kXAxisLabelBatch = 11;
inline constexpr std::size_t kYAxisLabelBatch = 12;
inline constexpr std::size_t kZAxisLabelBatch = 13;

struct GridKey {
  float spacing = 0.f;
  float centerX = 0.f;
  float centerZ = 0.f;
  float lineWidth = 0.f;
  float rightX = 0.f;
  float rightY = 0.f;
  float rightZ = 0.f;
  float upX = 0.f;
  float upY = 0.f;
  float upZ = 0.f;
  bool ortho = false;
  int viewportHeight = 0;
  [[nodiscard]] bool operator==(const GridKey&) const noexcept = default;
};

inline tool::Vec3 Normalize(tool::Vec3 v) {
  const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return len > 1e-6f ? tool::Vec3{v.x / len, v.y / len, v.z / len} : tool::Vec3{};
}

inline GridKey MakeKey(const tool::Camera& cam, int viewportHeight) {
  const float spacing = std::max(1.f, std::pow(10.f, std::floor(std::log10(std::max(1.f, std::abs(cam.eye.y)) / 10.f))));
  const float viewDistance = std::sqrt((cam.target.x - cam.eye.x) * (cam.target.x - cam.eye.x) +
                                       (cam.target.y - cam.eye.y) * (cam.target.y - cam.eye.y) +
                                       (cam.target.z - cam.eye.z) * (cam.target.z - cam.eye.z));
  const float safeHeight = static_cast<float>(std::max(1, viewportHeight));
  const float pixelWorld = cam.ortho ? cam.orthoHeight / safeHeight
                                     : 2.f * viewDistance * std::tan(cam.fovY * 0.5f) / safeHeight;
  const tool::Vec3 forward = Normalize({cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, cam.target.z - cam.eye.z});
  const tool::Vec3 right = Normalize({forward.y * cam.up.z - forward.z * cam.up.y,
                                       forward.z * cam.up.x - forward.x * cam.up.z,
                                       forward.x * cam.up.y - forward.y * cam.up.x});
  const tool::Vec3 screenUp = Normalize(tool::cross(right, forward));
  return {spacing, std::round(cam.eye.x / spacing) * spacing, std::round(cam.eye.z / spacing) * spacing,
          std::max(0.001f, pixelWorld * 1.25f), right.x, right.y, right.z,
          screenUp.x, screenUp.y, screenUp.z, cam.ortho, viewportHeight};
}

inline void AddRibbon(tool::Mesh& mesh, tool::Vec3 a, tool::Vec3 b, tool::Vec3 side, tool::Vec3 normal) {
  const tool::u32 base = mesh.addVtx(a + side, normal);
  mesh.addVtx(b + side, normal);
  mesh.addVtx(b - side, normal);
  mesh.addVtx(a - side, normal);
  mesh.addTri(base, base + 1, base + 2);
  mesh.addTri(base, base + 2, base + 3);
}

inline void AddFloorLine(tool::Mesh& mesh, tool::Vec3 a, tool::Vec3 b, float width) {
  const tool::Vec3 direction = Normalize({b.x - a.x, 0.f, b.z - a.z});
  const tool::Vec3 side{-direction.z * width * 0.5f, 0.f, direction.x * width * 0.5f};
  a.y = b.y = 0.003f;
  AddRibbon(mesh, a, b, side, {0.f, 1.f, 0.f});
}

inline void AddLabelStroke(tool::Mesh& mesh, tool::Vec3 anchor, tool::Vec3 right, tool::Vec3 up, float height,
                           float width, float ax, float ay, float bx, float by) {
  const tool::Vec3 a = anchor + right * (height * ax) + up * (height * ay);
  const tool::Vec3 b = anchor + right * (height * bx) + up * (height * by);
  const tool::Vec3 side = Normalize(right * (by - ay) - up * (bx - ax)) * (width * 0.5f);
  const tool::Vec3 normal = Normalize(tool::cross(b - a, side) * -1.f);
  AddRibbon(mesh, a, b, side, normal);
}

inline void AddAxisLabel(tool::Mesh& mesh, char label, tool::Vec3 anchor, tool::Vec3 right, tool::Vec3 up,
                         float height, float width) {
  auto stroke = [&](float ax, float ay, float bx, float by) {
    AddLabelStroke(mesh, anchor, right, up, height, width, ax, ay, bx, by);
  };
  if (label == 'X') {
    stroke(-0.5f, -0.5f, 0.5f, 0.5f);
    stroke(-0.5f, 0.5f, 0.5f, -0.5f);
  } else if (label == 'Y') {
    stroke(-0.5f, 0.5f, 0.f, 0.f);
    stroke(0.5f, 0.5f, 0.f, 0.f);
    stroke(0.f, 0.f, 0.f, -0.5f);
  } else if (label == 'Z') {
    stroke(-0.5f, 0.5f, 0.5f, 0.5f);
    stroke(0.5f, 0.5f, -0.5f, -0.5f);
    stroke(-0.5f, -0.5f, 0.5f, -0.5f);
  }
}

class GameGrid {
 public:
  bool Sync(const tool::Camera& cam, int viewportHeight) {
    if (viewportHeight <= 0) return false;
    const GridKey key = MakeKey(cam, viewportHeight);
    if (ready_ && key == key_) return false;

    std::array<tool::Mesh, kBatchCount> next{};
    const float reach = 60.f * key.spacing;
    for (const scene_view_model::GridLine& line : scene_view_model::gridLines(cam.eye)) {
      if (line.alpha <= 0) continue;
      const int maxAlpha = line.major ? 70 : 34;
      const std::size_t bucket = static_cast<std::size_t>(std::min(3, (line.alpha * 4 - 1) / maxAlpha));
      const std::size_t index = bucket + (line.major ? 4u : 0u);
      AddFloorLine(next[index], line.a, line.b, key.lineWidth);
    }

    const float axisReach = std::max(10000.f, reach);
    AddFloorLine(next[kXAxisBatch], {-axisReach, 0.f, 0.f}, {axisReach, 0.f, 0.f}, key.lineWidth * 1.6f);
    AddFloorLine(next[kZAxisBatch], {0.f, 0.f, -axisReach}, {0.f, 0.f, axisReach}, key.lineWidth * 1.6f);
    const tool::Vec3 yStart{0.f, 0.003f, 0.f};
    const tool::Vec3 yEnd{0.f, 6.f, 0.f};
    const tool::Vec3 ySide{key.rightX * key.lineWidth * 0.8f, key.rightY * key.lineWidth * 0.8f,
                           key.rightZ * key.lineWidth * 0.8f};
    const tool::Vec3 yNormal = Normalize(tool::cross(ySide, {0.f, 1.f, 0.f}));
    AddRibbon(next[kYAxisBatch], yStart, yEnd, ySide, yNormal);

    const tool::Vec3 screenRight{key.rightX, key.rightY, key.rightZ};
    const tool::Vec3 screenUp{key.upX, key.upY, key.upZ};
    const float labelHeight = key.lineWidth * 10.f;
    const float labelWidth = key.lineWidth * 1.6f;
    AddAxisLabel(next[kXAxisLabelBatch], 'X', {6.4f, 0.f, 0.f}, screenRight, screenUp, labelHeight, labelWidth);
    AddAxisLabel(next[kYAxisLabelBatch], 'Y', {0.f, 6.4f, 0.f}, screenRight, screenUp, labelHeight, labelWidth);
    AddAxisLabel(next[kZAxisLabelBatch], 'Z', {0.f, 0.f, 6.4f}, screenRight, screenUp, labelHeight, labelWidth);

    for (std::size_t i = 0; i < next.size(); ++i) {
      next[i].revision = meshes_[i].revision + 1;
    }
    meshes_ = std::move(next);
    key_ = key;
    ready_ = true;
    return true;
  }

  [[nodiscard]] const tool::Mesh& MeshAt(std::size_t index) const { return meshes_.at(index); }

  [[nodiscard]] static tool::Material MaterialAt(std::size_t index) {
    tool::Material material;
    material.castShadow = false;
    material.receiveShadow = false;
    material.roughness = 1.f;
    if (index < 4) {
      material.albedo = {0.32f, 0.34f, 0.4f};
      material.emissive = {0.08f, 0.09f, 0.12f};
      material.opacity = 0.14f * static_cast<float>(index + 1) / 4.f;
    } else if (index < 8) {
      const float level = static_cast<float>(index - 3) / 4.f;
      material.albedo = {0.4f, 0.42f, 0.5f};
      material.emissive = {0.1f, 0.11f, 0.15f};
      material.opacity = 0.3f * level;
    } else {
      const tool::Vec3 colors[] = {
          {0.75f, 0.16f, 0.14f}, {0.18f, 0.7f, 0.2f}, {0.16f, 0.28f, 0.8f}};
      const std::size_t colorIndex = index <= kZAxisBatch ? index - kXAxisBatch : index - kXAxisLabelBatch;
      material.albedo = colors[colorIndex];
      material.emissive = colors[colorIndex] * 0.6f;
      material.opacity = 0.72f;
    }
    return material;
  }

 private:
  std::array<tool::Mesh, kBatchCount> meshes_{};
  GridKey key_{};
  bool ready_ = false;
};

}  // namespace editor::game_grid
