// Modelo da janela Scene (scene_view_model.hpp): projecao, gizmo sob o mouse, arrasto, ima e grade. Sem janela.
// Roda no ctest como Editor.SceneViewModel. O teste de geometria compara a projecao com uma conta feita aqui, a mao,
// e exige diferenca menor que 1 px: o gizmo tem de bater com o objeto.
#include "scene_view_model.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace editor::scene_view_model;

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

// Camera de teste: olha de (0, 0, 10) para a origem, fovY 1.0 rad, imagem 800 x 600 no canto (100, 50).
tool::Camera TestCamera() {
  tool::Camera c;
  c.eye = {0, 0, 10};
  c.target = {0, 0, 0};
  c.up = {0, 1, 0};
  c.fovY = 1.0f;
  c.nearZ = 0.1f;
  c.farZ = 100.f;
  return c;
}
const Area kArea{100.f, 50.f, 800.f, 600.f};

// Conta independente (pinhole): objeto a `depth` metros na frente da camera, sem usar Mat4.
Pixel ExpectedPixel(tool::Vec3 p) {
  const double f = 1.0 / std::tan(1.0 * 0.5);
  const double depth = 10.0 - p.z;
  const double ndcX = (f / (800.0 / 600.0)) * p.x / depth;
  const double ndcY = f * p.y / depth;
  return {static_cast<float>(kArea.x + (ndcX * 0.5 + 0.5) * kArea.w), static_cast<float>(kArea.y + (1.0 - (ndcY * 0.5 + 0.5)) * kArea.h)};
}

bool Within1px(Pixel a, Pixel b) { return std::hypot(a.x - b.x, a.y - b.y) < 1.f; }

// Gizmo de um objeto em (x, y, z) com a camera de teste.
Gizmo GizmoAt(tool::Vec3 p, const Projection& proj) {
  Gizmo g;
  g.origin = p;
  g.len = axisLength(p, TestCamera().eye);
  CHECK(proj.point(p, g.center));
  return g;
}

// Pixels de um ponto do mundo estao a menos de 1 px do ponto calculado a mao (o gizmo bate com o objeto).
void TestProjectionMatchesObjectWithinOnePixel() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const tool::Vec3 points[] = {{0, 0, 0}, {1.5f, 2.f, 0.f}, {-3.f, -1.f, 4.f}, {2.f, 0.5f, -5.f}};
  for (const tool::Vec3& p : points) {
    Pixel got{};
    CHECK(proj.point(p, got));
    CHECK(Within1px(got, ExpectedPixel(p)));
  }
  Pixel behind{};
  CHECK(!proj.point({0, 0, 20.f}, behind));   // atras da camera: nao aparece
}

// Objeto e ponta do eixo X: a ponta sai do centro do gizmo, para a direita, e o centro e o pixel do objeto.
void TestGizmoCenterIsObjectPixel() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const tool::Vec3 obj{1.2f, 0.8f, 0.f};
  const Gizmo g = GizmoAt(obj, proj);
  CHECK(Within1px(g.center, ExpectedPixel(obj)));
  Pixel tip{};
  CHECK(proj.point(obj + axisOf(0) * g.len, tip));
  CHECK(tip.x > g.center.x);
  CHECK(std::fabs(tip.y - g.center.y) < 1.f);   // eixo X e horizontal nesta camera
}

// Segmento atras da camera e recortado: a parte visivel continua, sem linha esticada.
void TestSegmentClipsBehindCamera() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  Pixel a{}, b{};
  CHECK(proj.segment({0, 0, 0}, {0, 0, 20.f}, a, b));   // um ponto atras
  CHECK(Within1px(a, ExpectedPixel({0, 0, 0})));
  CHECK(!proj.segment({0, 0, 20.f}, {1, 0, 30.f}, a, b));   // os dois atras
}

// Mouse sobre a ponta do eixo X escolhe X; longe de tudo nao escolhe nada; escala no centro escolhe Center.
void TestHitTestPicksHandle() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const Gizmo g = GizmoAt({0, 0, 0}, proj);
  Pixel tipX{};
  CHECK(proj.point(g.origin + axisOf(0) * g.len, tipX));
  CHECK(hitTest(proj, g, Mode::Move, {(g.center.x + tipX.x) * 0.5f, g.center.y + 2.f}) == Handle::X);
  CHECK(hitTest(proj, g, Mode::Move, {g.center.x + 300.f, g.center.y + 300.f}) == Handle::None);
  CHECK(hitTest(proj, g, Mode::Scale, g.center) == Handle::Center);
  CHECK(hitTest(proj, g, Mode::Move, g.center) != Handle::Center);   // mover nao tem centro: o eixo mais perto vence
}

// Girar: o anel tem 40 lados por eixo; o mouse sobre um lado do anel de Z escolhe Z. O ponto fica a 45 graus: os
// aneis de X e Y estao de lado para a camera e passam pelo eixo vertical e horizontal, longe dele.
void TestRingHitAndStrokeCount() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const Gizmo g = GizmoAt({0, 0, 0}, proj);
  Pixel onRing{};
  CHECK(proj.point(ringPoint(g, 2, 5), onRing));
  CHECK(hitTest(proj, g, Mode::Rotate, onRing) == Handle::Z);
  CHECK(strokes(proj, g, Mode::Rotate, Handle::None).size() == 3u * kRingSegments);
  CHECK(strokes(proj, g, Mode::Move, Handle::X).size() == 3u);
  CHECK(strokes(proj, g, Mode::Move, Handle::X)[0].highlighted == true);
}

// Arrastar a ponta do eixo X exatamente ate a ponta (o vetor da tela) move o objeto um comprimento de eixo.
// O objeto novo tem de ficar sobre a ponta antiga, com menos de 1 px de diferenca.
void TestDragAlongAxisFollowsMouse() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const tool::Vec3 obj{0, 0, 0};
  const Gizmo g = GizmoAt(obj, proj);
  Pixel tip{};
  CHECK(proj.point(obj + axisOf(0) * g.len, tip));

  DragStart s;
  s.mode = Mode::Move;
  s.handle = Handle::X;
  s.pos = {0.f, 0.f, 0.f};
  s.mouse0 = g.center;
  s.startWorld = obj;
  s.gizmo = g;
  s.eye = TestCamera().eye;

  DragFrame f;
  f.mouse = {g.center.x + (tip.x - g.center.x), g.center.y + (tip.y - g.center.y)};
  f.proj = proj;
  const DragResult r = dragEdit(s, f);
  CHECK(r.changed);
  CHECK(std::fabs(r.pos[0] - g.len) < 1e-3f);
  CHECK(r.pos[1] == 0.f && r.pos[2] == 0.f);

  Pixel moved{};
  CHECK(proj.point({r.pos[0], 0.f, 0.f}, moved));
  CHECK(Within1px(moved, tip));
}

// Com o ima, o valor absoluto cai na grade: 0.3 + 0.6 = 0.9 vai para 1.0 (passo 0.25).
void TestSnapRoundsAbsolutePosition() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  Pixel s0{}, s1{};
  CHECK(proj.point({0.3f, 0.f, 0.f}, s0));
  CHECK(proj.point({1.3f, 0.f, 0.f}, s1));
  DragStart s;
  s.mode = Mode::Move;
  s.handle = Handle::X;
  s.pos = {0.3f, 0.f, 0.f};
  s.mouse0 = s0;
  s.startWorld = {0.3f, 0.f, 0.f};
  s.gizmo.origin = {0.3f, 0.f, 0.f};
  s.gizmo.len = 1.f;
  s.eye = TestCamera().eye;
  DragFrame f;
  f.proj = proj;
  f.snap = true;
  f.snapStep = 0.25f;
  f.mouse = {s0.x + (s1.x - s0.x) * 0.6f, s0.y};   // 0.6 m ao longo do eixo
  const DragResult r = dragEdit(s, f);
  CHECK(std::fabs(r.pos[0] - 1.0f) < 1e-3f);
}

// Com pai (inversa identidade), o movimento e o mesmo que sem pai.
void TestParentIdentityMatchesNoParent() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const Gizmo g = GizmoAt({0, 0, 0}, proj);
  DragStart s;
  s.mode = Mode::Move;
  s.handle = Handle::Y;
  s.mouse0 = g.center;
  s.startWorld = {0, 0, 0};
  s.gizmo = g;
  s.eye = TestCamera().eye;
  DragFrame f;
  f.proj = proj;
  Pixel tipY{};
  CHECK(proj.point({0, g.len, 0}, tipY));
  f.mouse = tipY;
  const DragResult noParent = dragEdit(s, f);
  s.hasParent = true;
  s.parentInv = tool::Mat4{};
  s.parentInv.m[0][0] = s.parentInv.m[1][1] = s.parentInv.m[2][2] = s.parentInv.m[3][3] = 1.f;
  const DragResult withParent = dragEdit(s, f);
  CHECK(std::fabs(noParent.pos[1] - withParent.pos[1]) < 1e-3f);
  CHECK(std::fabs(noParent.pos[1] - g.len) < 1e-3f);
}

// Escala uniforme: arrastar 100 px para a direita (dx - dy = 100) aumenta 60 por cento; o limite minimo vale.
void TestUniformScaleFromCenterDrag() {
  DragStart s;
  s.mode = Mode::Scale;
  s.handle = Handle::Center;
  s.scale = {1.f, 2.f, 3.f};
  s.mouse0 = {0.f, 0.f};
  DragFrame f;
  f.mouse = {100.f, 0.f};
  const DragResult r = dragEdit(s, f);
  CHECK(std::fabs(r.scale[0] - 1.6f) < 1e-4f);
  CHECK(std::fabs(r.scale[2] - 4.8f) < 1e-4f);
  CHECK(r.readout == "scale x1.60");
  f.mouse = {-100000.f, 0.f};   // k fica no minimo 0.01
  CHECK(dragEdit(s, f).scale[0] >= 0.01f);
}

// Girar: ângulo do mouse em torno do centro. Com ima, passos de 15 graus.
void TestRotateStepsWithSnap() {
  const Projection proj = Projection::make(TestCamera(), kArea);
  const Gizmo g = GizmoAt({0, 0, 0}, proj);
  DragStart s;
  s.mode = Mode::Rotate;
  s.handle = Handle::Z;
  s.rot = {0.f, 0.f, 0.f};
  s.mouse0 = g.center;
  s.gizmo = g;
  s.startWorld = {0, 0, 0};
  s.eye = TestCamera().eye;
  s.angle0 = 0.f;
  DragFrame f;
  f.proj = proj;
  f.mouse = {g.center.x + 100.f, g.center.y};   // angulo 0 na tela: nada gira
  CHECK(std::fabs(dragEdit(s, f).rot[2]) < 1e-3f);
  f.snap = true;
  f.mouse = {g.center.x + 100.f * std::cos(0.2f), g.center.y + 100.f * std::sin(0.2f)};   // ~11 graus, sobe para 15
  CHECK(std::fabs(std::fabs(dragEdit(s, f).rot[2]) - 15.f) < 1e-2f);
}

void TestWrapDegrees() {
  CHECK(std::fabs(wrapDegrees(190.f) - (-170.f)) < 1e-4f);
  CHECK(std::fabs(wrapDegrees(-190.f) - 170.f) < 1e-4f);
  CHECK(std::fabs(wrapDegrees(45.f) - 45.f) < 1e-4f);
}

// Grade: linhas em volta da camera; uma a cada 5 e a mais forte; a linha do centro nao some.
void TestGridIsCenteredAndFaded() {
  const auto lines = gridLines({0.f, 2.f, 0.f});
  CHECK(lines.size() == 2u * 121u);
  int majors = 0;
  for (const GridLine& l : lines) majors += l.major ? 1 : 0;
  CHECK(majors == 2 * 25);   // a cada 5 linhas: -60, -55, ..., 60 (25 por direcao)
  CHECK(lines[60 * 2].alpha == 70);   // linha do centro (i = 0), pelo lado de X: forte e sem queda
  CHECK(lines[0].alpha < 70);         // borda: mais apagada
}

}  // namespace

int main() {
  TestProjectionMatchesObjectWithinOnePixel();
  TestGizmoCenterIsObjectPixel();
  TestSegmentClipsBehindCamera();
  TestHitTestPicksHandle();
  TestRingHitAndStrokeCount();
  TestDragAlongAxisFollowsMouse();
  TestSnapRoundsAbsolutePosition();
  TestParentIdentityMatchesNoParent();
  TestUniformScaleFromCenterDrag();
  TestRotateStepsWithSnap();
  TestWrapDegrees();
  TestGridIsCenteredAndFaded();
  std::printf("Editor.SceneViewModel: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
