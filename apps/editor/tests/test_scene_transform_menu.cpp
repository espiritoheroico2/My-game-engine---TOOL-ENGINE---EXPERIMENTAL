// Modelo do menu flutuante da Scene (scene_transform_menu.hpp): teclas W, E, R, Q, passo do ima e a conta do arrasto
// do eixo X com e sem encaixe. Sem janela. Roda no ctest como Editor.SceneTransformMenu.
//
// Exemplo: a Caixa azul esta em X = 0. Arrastar o eixo X ate 0,3 m com o ima em 0,25 m grava 0,25 m; com o ima
// desligado grava 0,3 m. Trocar o modo com W, E ou R nao mexe na posicao da Caixa.
#include "scene_transform_menu.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace editor::scene_transform_menu;
namespace vm = editor::scene_view_model;

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

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Camera de teste: olha de (0, 0, 10) para a origem, imagem 800 x 600 no canto (100, 50).
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
const vm::Area kArea{100.f, 50.f, 800.f, 600.f};

// Entrada de arrasto de um objeto em `pos`, pegando o eixo `handle` com o mouse em `mouse0`.
vm::DragStart MoveStart(tool::Vec3 origin, vm::Pixel mouse0, vm::Handle handle, std::array<tool::Vec3, 3> axes, bool worldAxes) {
  vm::DragStart s;
  s.mode = vm::Mode::Move;
  s.handle = handle;
  s.pos = {origin.x, origin.y, origin.z};
  s.mouse0 = mouse0;
  s.startWorld = origin;
  s.eye = TestCamera().eye;
  s.gizmo.origin = origin;
  s.gizmo.len = vm::axisLength(origin, s.eye);
  s.gizmo.axes = axes;
  s.worldAxes = worldAxes;
  return s;
}

// Mouse onde o ponto do mundo `p` aparece na tela (sem erro: usa a mesma projecao da janela).
vm::Pixel ScreenOf(tool::Vec3 p) {
  vm::Pixel out{};
  (void)vm::Projection::make(TestCamera(), kArea).point(p, out);
  return out;
}

// Arrasto de mover pelo eixo X, do ponto (0, 0, 0): o mouse anda ate o ponto `metros` no eixo X (Mundo, sem pai).
float DragXTo(float metros, bool snapOn, float step) {
  const tool::Vec3 origin{0, 0, 0};
  const vm::DragStart s = MoveStart(origin, ScreenOf(origin), vm::Handle::X, vm::worldAxes(), true);
  vm::DragFrame f;
  f.mouse = ScreenOf({metros, 0, 0});
  f.snap = snapOn;
  f.snapStep = step;
  f.proj = vm::Projection::make(TestCamera(), kArea);
  return vm::dragEdit(s, f).pos[0];
}

void TestActionsSetModeAndSpace() {
  int mode = 0;
  bool local = false;
  applyAction(Action::Rotate, mode, local);
  CHECK(mode == 1);
  applyAction(Action::Scale, mode, local);
  CHECK(mode == 2);
  applyAction(Action::Move, mode, local);
  CHECK(mode == 0);
  CHECK(!local);
  applyAction(Action::ToggleSpace, mode, local);
  CHECK(local);
  CHECK(mode == 0);   // Q so troca o espaco
  applyAction(Action::ToggleSpace, mode, local);
  CHECK(!local);
  applyAction(Action::None, mode, local);
  CHECK(mode == 0 && !local);
}

void TestSnapSliderMapsStepAndClamps() {
  CHECK(Near(snapStepFromSlider(0.f), 0.05f, 1e-6f));
  CHECK(Near(snapStepFromSlider(1.f), 2.f, 1e-6f));
  CHECK(Near(snapStepFromSlider(0.5f), 1.025f, 1e-5f));
  CHECK(Near(snapStepFromSlider(-3.f), 0.05f, 1e-6f));   // fora da faixa: cortado
  CHECK(Near(snapStepFromSlider(7.f), 2.f, 1e-6f));
  CHECK(Near(sliderFromSnapStep(0.25f), (0.25f - 0.05f) / 1.95f, 1e-5f));
  CHECK(Near(snapStepFromSlider(sliderFromSnapStep(0.75f)), 0.75f, 1e-5f));   // ida e volta
  CHECK(sliderFromSnapStep(0.f) == 0.f);                                   // passo abaixo do minimo: no zero
}

// Arrastar X de 0 com passo 0,25 e encaixe ligado vai para 0,25. Com encaixe desligado, vai para o valor do mouse.
void TestDragXWithAndWithoutSnap() {
  CHECK(Near(DragXTo(0.3f, true, 0.25f), 0.25f, 1e-5f));
  CHECK(Near(DragXTo(0.3f, false, 0.25f), 0.3f, 1e-3f));
}

// applyAction recebe so o modo e o espaco (nunca o Transform do objeto): trocar com W, E, R, Q so mexe nesses dois.
void TestModeChangeTouchesOnlyModeAndSpace() {
  int mode = 0;
  bool local = false;
  applyAction(Action::Rotate, mode, local);
  applyAction(Action::ToggleSpace, mode, local);
  CHECK(mode == 1 && local);
  applyAction(Action::Scale, mode, local);
  applyAction(Action::Move, mode, local);
  CHECK(mode == 0 && local);
}

// Espaco local: o eixo X do objeto girado 90 graus em Z aponta para +Y do mundo, e arrastar esse eixo move o objeto
// nessa direcao (Transform sem pai: a posicao muda pelo deslocamento).
void TestLocalAxesFollowObjectRotation() {
  const std::array<tool::Vec3, 3> axes = vm::localAxes(tool::Mat4::rotationZ(1.5707963f));
  CHECK(Near(axes[0].x, 0.f, 1e-4f) && Near(axes[0].y, 1.f, 1e-4f) && Near(axes[0].z, 0.f, 1e-4f));
  CHECK(Near(axes[2].z, 1.f, 1e-4f));   // Z nao muda com a rotacao em Z

  const tool::Vec3 origin{0, 0, 0};
  const vm::DragStart s = MoveStart(origin, ScreenOf(origin), vm::Handle::X, axes, false);
  vm::DragFrame f;
  f.mouse = ScreenOf(origin + axes[0] * 0.3f);
  f.snap = false;
  f.proj = vm::Projection::make(TestCamera(), kArea);
  const vm::DragResult r = vm::dragEdit(s, f);
  CHECK(r.changed);
  CHECK(Near(r.pos[1], 0.3f, 0.01f));   // andou 0,3 m para +Y do mundo
  CHECK(Near(r.pos[0], 0.f, 0.01f));
}

// Escala zero nao tem direcao: os eixos voltam aos do mundo.
void TestZeroScaleFallsBackToWorldAxes() {
  const std::array<tool::Vec3, 3> axes = vm::localAxes(tool::Mat4::scale({0, 0, 0}));
  CHECK(Near(axes[0].x, 1.f, 1e-6f) && Near(axes[1].y, 1.f, 1e-6f) && Near(axes[2].z, 1.f, 1e-6f));
}

}  // namespace

int main() {
  TestActionsSetModeAndSpace();
  TestSnapSliderMapsStepAndClamps();
  TestModeChangeTouchesOnlyModeAndSpace();
  TestDragXWithAndWithoutSnap();
  TestLocalAxesFollowObjectRotation();
  TestZeroScaleFallsBackToWorldAxes();
  std::printf("Editor.SceneTransformMenu: %d checks, %d failed\n", run, failed);
  return failed == 0 ? 0 : 1;
}
