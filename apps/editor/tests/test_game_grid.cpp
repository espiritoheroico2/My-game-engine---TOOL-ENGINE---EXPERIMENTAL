#include "game_grid.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void Check(bool ok, const char* expression, int line) {
  ++checks;
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expression);
  }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

std::array<tool::u8, 3> RenderCenterPixel(bool withGrid) {
  tool::Camera camera;
  camera.eye = {0.f, 0.5f, 4.f};
  camera.target = {0.f, 0.003f, 0.f};
  editor::game_grid::GameGrid grid;
  if (withGrid) grid.Sync(camera, 64);

  tool::SoftRenderer renderer(64, 64);
  renderer.begin(camera, {0.f, 0.f, 0.f});
  tool::Material objectMaterial;
  objectMaterial.castShadow = false;
  objectMaterial.receiveShadow = false;
  objectMaterial.emissive = {1.f, 0.f, 0.f};
  renderer.draw(tool::Mesh::cube(1.f), {}, objectMaterial);
  if (withGrid) {
    for (std::size_t i = 0; i < editor::game_grid::kBatchCount; ++i) {
      const tool::Mesh& mesh = grid.MeshAt(i);
      if (!mesh.idx.empty()) renderer.draw(mesh, {}, editor::game_grid::GameGrid::MaterialAt(i));
    }
  }

  const tool::u8* pixels = renderer.pixels();
  const std::size_t center = (32u * 64u + 32u) * 4u;
  return {pixels[center], pixels[center + 1], pixels[center + 2]};
}

void TestGridPassesDepthTestAgainstOpaqueObjects() {
  const std::array<tool::u8, 3> objectPixel = RenderCenterPixel(false);
  const std::array<tool::u8, 3> gridAndObjectPixel = RenderCenterPixel(true);
  CHECK(objectPixel[0] > objectPixel[1]);
  CHECK(gridAndObjectPixel == objectPixel);
}

void TestDepthTestedGridGeometryAndCache() {
  tool::Camera camera;
  camera.eye = {0.f, 4.f, 8.f};
  camera.target = {0.f, 0.5f, 0.f};
  editor::game_grid::GameGrid grid;
  CHECK(grid.Sync(camera, 360));
  CHECK(!grid.Sync(camera, 360));

  bool anyGridLine = false;
  for (std::size_t i = 0; i < editor::game_grid::kXAxisBatch; ++i) {
    const tool::Mesh& mesh = grid.MeshAt(i);
    CHECK(mesh.pos.size() % 4 == 0);
    CHECK(mesh.idx.size() % 6 == 0);
    for (const tool::Vec3& p : mesh.pos) CHECK(p.y == 0.003f);
    anyGridLine = anyGridLine || !mesh.idx.empty();
    const tool::Material material = editor::game_grid::GameGrid::MaterialAt(i);
    CHECK(material.opacity > 0.f && material.opacity < 1.f);
    CHECK(!material.castShadow && !material.receiveShadow);
  }
  CHECK(anyGridLine);
  for (std::size_t i = editor::game_grid::kXAxisBatch; i < editor::game_grid::kBatchCount; ++i) {
    CHECK(!grid.MeshAt(i).idx.empty());
    CHECK(editor::game_grid::GameGrid::MaterialAt(i).opacity < 1.f);
  }
  const tool::Vec3 viewDirection = tool::normalize(camera.eye - camera.target);
  CHECK(tool::dot(grid.MeshAt(editor::game_grid::kYAxisBatch).normal.front(), viewDirection) > 0.f);
  float minX = 0.f, maxX = 0.f;
  for (const tool::Vec3& p : grid.MeshAt(editor::game_grid::kXAxisBatch).pos) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
  }
  CHECK(minX < 0.f && maxX > 0.f);
  float minZ = 0.f, maxZ = 0.f;
  for (const tool::Vec3& p : grid.MeshAt(editor::game_grid::kZAxisBatch).pos) {
    minZ = std::min(minZ, p.z);
    maxZ = std::max(maxZ, p.z);
  }
  CHECK(minZ < 0.f && maxZ > 0.f);

  const std::uint32_t oldRevision = grid.MeshAt(0).revision;
  camera.eye.x += 3.f;
  CHECK(grid.Sync(camera, 360));
  CHECK(grid.MeshAt(0).revision == oldRevision + 1);
}

}  // namespace

int main() {
  TestGridPassesDepthTestAgainstOpaqueObjects();
  TestDepthTestedGridGeometryAndCache();
  if (failures == 0) {
    std::printf("Editor.GameGrid: %d checks, 0 failed\n", checks);
    return 0;
  }
  std::printf("Editor.GameGrid: %d checks, %d failed\n", checks, failures);
  return 1;
}
