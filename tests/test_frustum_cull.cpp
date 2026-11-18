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
