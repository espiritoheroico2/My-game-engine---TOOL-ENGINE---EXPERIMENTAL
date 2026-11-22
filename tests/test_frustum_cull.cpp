// Culling por frustum (engine/render/frustum_cull.hpp): matrizes conhecidas, GL e D3D, esferas dentro, fora e na borda.
#include "test.hpp"
#include "tool/render/frustum_cull.hpp"

using namespace tool::render;

namespace {
void Identity(float m[4][4]) {
  for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) m[c][r] = (c == r) ? 1.f : 0.f;
}
}  // namespace

// Caixa unitaria: x,y em [-1,1], z em [-1,1] (GL) ou [0,1] (D3D).
TEST(frustum_gl_identity_box) {
  float m[4][4]; Identity(m);
  const Frustum f = frustumFromViewProj(m, false);
  CHECK(sphereInFrustum(f, 0, 0, 0, 0.5f));
  CHECK(!sphereInFrustum(f, 2, 0, 0, 0.5f));        // longe a direita
  CHECK(sphereInFrustum(f, 1.2f, 0, 0, 0.5f));      // cruza a borda direita
  CHECK(!sphereInFrustum(f, 1.6f, 0, 0, 0.5f));     // completamente fora
  CHECK(!sphereInFrustum(f, 0, 0, -2, 0.5f));       // atras do plano proximo
  CHECK(sphereInFrustum(f, 0, 0, -1.2f, 0.5f));     // cruza o plano proximo
}

TEST(frustum_d3d_identity_box) {
  float m[4][4]; Identity(m);
  const Frustum f = frustumFromViewProj(m, true);
  CHECK(sphereInFrustum(f, 0, 0, 0.5f, 0.1f));      // dentro: z em [0,1]
  CHECK(!sphereInFrustum(f, 0, 0, -0.6f, 0.0f));    // z negativo: fora em D3D
  CHECK(!sphereInFrustum(f, 0, 0, 1.6f, 0.0f));     // alem do plano distante
}

TEST(frustum_shadow_culling_keeps_casters_outside_csm_depth) {
  float m[4][4]; Identity(m);
  const Frustum f = frustumFromViewProj(m, true);
  const SphereBounds caster{{0.f, 0.f, -5.f}, 0.25f, true};
  CHECK(!sphereInFrustum(f, caster));
  CHECK(sphereInFrustumSides(f, caster));
}

TEST(frustum_world_sphere_handles_translation_nonuniform_scale_and_shear) {
  float world[4][4]; Identity(world);
  world[0][0] = 2.f;
  world[1][1] = 0.5f;
  world[3][0] = 3.f;
  const SphereBounds local{{1.f, 2.f, 3.f}, 1.f, true};
  const SphereBounds transformed = transformSphere(world, local);
  CHECK(transformed.valid);
  CHECK(transformed.center[0] == 5.f);
  CHECK(transformed.center[1] == 1.f);
  CHECK(transformed.center[2] == 3.f);
  CHECK(transformed.radius >= 2.f);

  world[1][0] = 1.f;
  const SphereBounds sheared = transformSphere(world, local);
  CHECK(sheared.valid);
  CHECK(sheared.radius >= 2.f);
}

TEST(frustum_invalid_world_sphere_is_not_culled) {
  float world[4][4]; Identity(world);
  world[0][3] = 0.25f;
  const SphereBounds invalid = transformSphere(world, SphereBounds{{0.f, 0.f, 0.f}, 1.f, true});
  float identity[4][4]; Identity(identity);
  CHECK(!invalid.valid);
  CHECK(sphereInFrustum(frustumFromViewProj(identity, true), invalid));
}

// Perspectiva GL real, fov 90 graus, near 1, far 100, camera na origem olhando -Z.
TEST(frustum_gl_perspective) {
  const float n = 1.f, fr = 100.f;
  float m[4][4] = {{0}};
  m[0][0] = 1.f; m[1][1] = 1.f;                       // tan(45 graus) = 1, aspecto 1
  m[2][2] = -(fr + n) / (fr - n);
  m[2][3] = -1.f;
  m[3][2] = -2.f * fr * n / (fr - n);
  const Frustum f = frustumFromViewProj(m, false);
  CHECK(sphereInFrustum(f, 0, 0, -10, 0.5f));       // na frente, no centro
  CHECK(!sphereInFrustum(f, 0, 0, 5, 0.5f));        // atras da camera
  CHECK(!sphereInFrustum(f, 0, 0, -200, 0.0f));     // alem do far
  CHECK(!sphereInFrustum(f, 50, 0, -10, 0.5f));     // muito a direita do campo de visao
  CHECK(sphereInFrustum(f, 9.8f, 0, -10, 0.5f));    // na borda do campo de visao
}

TEST(frustum_d3d_perspective) {
  const float n = 1.f, far = 100.f;
  float m[4][4] = {{0}};
  m[0][0] = 1.f; m[1][1] = 1.f;
  m[2][2] = far / (n - far);
  m[2][3] = -1.f;
  m[3][2] = n * far / (n - far);
  const Frustum f = frustumFromViewProj(m, true);
  CHECK(sphereInFrustum(f, 0, 0, -10, 0.5f));
  CHECK(!sphereInFrustum(f, 0, 0, 5, 0.5f));
  CHECK(!sphereInFrustum(f, 0, 0, -200, 0.f));
  CHECK(!sphereInFrustum(f, 50, 0, -10, 0.5f));
  CHECK(sphereInFrustum(f, 9.8f, 0, -10, 0.5f));
}
