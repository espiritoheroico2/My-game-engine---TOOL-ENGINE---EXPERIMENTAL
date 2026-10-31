// Sandbox alpha: monta uma cena (ECS + Blueprint + luzes), roda alguns quadros de "jogo"
// e renderiza com o SoftRenderer em sandbox.ppm. Uso: sandbox [saida.ppm]
#include "tool/render/soft_renderer.hpp"
#include "tool/scene/scene.hpp"

#include <cstdio>

using namespace tool;

struct Transform { Vec3 pos{}; f32 yaw = 0; f32 scale = 1; };
struct Visual { int shape = 0; Vec3 color{1, 1, 1}; f32 roughness = 0.5f, metallic = 0; };
struct Spin { f32 degPerSec = 45; };

int main(int argc, char** argv) {
  Scene scene;
  const TagId crate = scene.tag("crate");
  Blueprint bp;
  bp.tag(crate).with<Transform>().with<Visual>(0, Vec3{0.8f, 0.5f, 0.2f}, 0.6f, 0.f).with<Spin>(45.f);

  for (int i = 0; i < 5; ++i) {
    auto o = scene.spawn(bp);
    o.get<Transform>()->pos = {static_cast<f32>(i - 2) * 1.5f, 0.5f, static_cast<f32>(i % 2) * -1.2f};
    o.get<Visual>()->shape = i % 2;
    o.get<Visual>()->metallic = i == 3 ? 0.9f : 0.f;
  }
  auto dead = scene.spawn(bp);
  dead.DeleteAndReuse();                       // some agora; o proximo identico reaproveita
  auto reused = scene.spawn(bp);
  reused.get<Transform>()->pos = {0, 1.8f, 0.6f};
  reused.get<Visual>()->shape = 1; reused.get<Visual>()->color = {0.2f, 0.6f, 0.9f}; reused.get<Visual>()->metallic = 0.6f; reused.get<Visual>()->roughness = 0.25f;
  std::printf("pool: reaproveitados=%zu\n", scene.stats().reused);

  // 60 quadros de simulacao
  for (int f = 0; f < 60; ++f)
    scene.world().each<Transform, Spin>([](Entity, Transform& t, Spin& s) { t.yaw += s.degPerSec / 60.f; });

  SoftRenderer r(640, 360);
  Camera cam; cam.eye = {4.5f, 3.2f, 7.f}; cam.target = {0, 0.6f, -0.5f};
  r.begin(cam);
  Light sun; sun.type = LightType::Directional; sun.direction = {-0.5f, -1.f, -0.4f}; sun.intensity = 2.f; sun.color = {1.f, 0.95f, 0.85f};
  Light lamp; lamp.type = LightType::Point; lamp.position = {-1.5f, 2.5f, 1.5f}; lamp.range = 14.f; lamp.intensity = 16.f; lamp.color = {1.f, 0.65f, 0.35f};
  r.setLights({sun, lamp});
  const Mesh cube = Mesh::cube(0.5f), sphere = Mesh::sphere(0.5f), plane = Mesh::plane(0.5f);
  r.draw(plane, Mat4::scale({14, 1, 14}), Material{{0.5f, 0.52f, 0.55f}, 0.9f, 0.f});
  scene.world().each<Transform, Visual>([&](Entity, Transform& t, Visual& v) {
    const Mat4 w = Mat4::translation(t.pos) * Mat4::rotationY(t.yaw * 3.14159265f / 180.f) * Mat4::scale({t.scale, t.scale, t.scale});
    r.draw(v.shape == 1 ? sphere : cube, w, Material{v.color, v.roughness, v.metallic});
  });

  const char* out = argc > 1 ? argv[1] : "sandbox.ppm";
  if (FILE* f = std::fopen(out, "wb")) {
    std::fprintf(f, "P6\n%u %u\n255\n", r.width(), r.height());
    for (size_t i = 0; i < static_cast<size_t>(r.width()) * r.height(); ++i) std::fwrite(r.pixels() + i * 4, 1, 3, f);
    std::fclose(f);
    std::printf("renderizado: %s (%llu triangulos, %llu pixels)\n", out, static_cast<unsigned long long>(r.trianglesDrawn()), static_cast<unsigned long long>(r.pixelsShaded()));
  }
  return 0;
}
