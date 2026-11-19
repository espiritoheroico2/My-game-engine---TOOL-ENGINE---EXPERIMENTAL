// Testes de elementos da janela Editor info (SpriteUI): X, grip, campo, botao Save e contagem de batches.
// Padrao para as demais janelas: cada elemento tem um teste que clica e confere a acao ou o estado.
// Nao abre janela: chama View::Frame com ponteiro sintetico. Fonte vem de EDITOR_FONT_PATH (CMake).
#include "editor_info_spriteui.hpp"
#include "project_settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

constexpr float kW = 760.f;
constexpr float kH = 720.f;

using View = editor::infoui::View;
using Action = View::Action;
using FrameInput = editor::infoui::FrameInput;

struct Result {
  std::vector<Action> actions;
  std::size_t batches = 0;
  std::size_t instances = 0;
  std::vector<ToolEngine::SpriteUi::Rendering::DrawBatch> batchList;
};

// Um quadro com o ponteiro em (x, y). pressed/released sao so de um quadro (como o SDL entrega).
Result Run(View& view, float x, float y, bool down, bool pressed, bool released, std::uint64_t id) {
  FrameInput in;
  in.pointerX = x;
  in.pointerY = y;
  in.pointerDown = down;
  in.pointerPressed = pressed;
  in.pointerReleased = released;
  in.frameId = id;
  Result r;
  const auto packet = view.Frame(kW, kH, in, r.actions);
  r.batches = packet.batches.size();
  r.instances = packet.instances.size();
  r.batchList = packet.batches;
  return r;
}

// Clique completo (pressionar e soltar em dois quadros). Devolve as acoes dos dois quadros: o X age ao
// pressionar, o botao e o campo agem ao soltar.
std::vector<Action> Click(View& view, float x, float y, std::uint64_t& id) {
  std::vector<Action> all = Run(view, x, y, true, true, false, ++id).actions;
  const auto release = Run(view, x, y, false, false, true, ++id).actions;
  all.insert(all.end(), release.begin(), release.end());
  return all;
}

bool Contains(const std::vector<Action>& a, Action what) {
  for (Action x : a) {
    if (x == what) return true;
  }
  return false;
}

ToolEngine::SpriteUi::Rect Center(ToolEngine::SpriteUi::Rect r) {
  return {{r.position.x + 0.5f * r.size.x, r.position.y + 0.5f * r.size.y}, {0.f, 0.f}};
}

}  // namespace

int main() {
  namespace SU = ToolEngine::SpriteUi;
  namespace SW = ToolEngine::SpriteUi::Widgets;
  namespace ST = ToolEngine::SpriteUi::Text;

  ST::TruetypeAtlas atlas;
  if (!editor::sui::LoadTextAtlas(EDITOR_FONT_PATH, atlas, 1.f)) {
    std::printf("FALHOU: fonte nao carregou: %s\n", EDITOR_FONT_PATH);
    return 1;
  }

  editor::ProjectSettings settings;
  settings.info.version = "1.0.0.0";
  editor::info::Model model(settings);
  View view(model, atlas);
  std::uint64_t id = 0;

  // 1. Desenho: o primeiro quadro gera batches e instancias.
  const Result first = Run(view, -100.f, -100.f, false, false, false, ++id);
  Check(first.batches > 0 && first.instances > 0, "desenha moldura e conteudo no primeiro quadro");
  if (const char* dump = std::getenv("EDITOR_INFO_BATCHES")) {   // EDITOR_INFO_BATCHES=1: lista os lotes
    (void)dump;
    for (const auto& b : first.batchList) {
      std::printf("  lote: %u instancias, textura %u, clip %u\n", b.instanceCount, b.textureSlice, b.clipIndex);
    }
  }
  std::printf("info: draw calls (batches) = %zu, instancias = %zu\n", first.batches, first.instances);

  const SU::Rect frame = view.frameRect();
  const auto& metrics = view.frameMetrics();

  // 2. X da moldura: clique fecha (Action::Close).
  const SU::Rect closeRect = SW::WindowFrameButtonRect(frame, metrics, false, SW::WindowFrameButton::Close);
  const auto closeCenter = Center(closeRect).position;
  Check(Contains(Click(view, closeCenter.x, closeCenter.y, id), Action::Close), "X fecha a janela (Action::Close)");

  // 3. Grip: arrastar so pelo grip move a janela; o mesmo arraste no campo nao move.
  const SU::Rect bar = SW::WindowFrameHeader(view.frameRect(), metrics, false);
  const SU::Rect grip = SW::WindowGripRect(bar, 14.f);
  const auto gripCenter = Center(grip).position;
  const SU::Vec2 before = view.windowPos();
  Run(view, gripCenter.x, gripCenter.y, true, true, false, ++id);
  Run(view, gripCenter.x - 20.f, gripCenter.y, true, false, false, ++id);
  Run(view, gripCenter.x - 20.f, gripCenter.y, false, false, true, ++id);
  Check(view.windowPos().x == before.x - 20.f && view.windowPos().y == before.y, "grip arrasta a janela (dx = -20)");

  // Campo: pressionar no conteudo nao inicia arraste.
  const SU::Rect productRect = view.rectOf(view.fieldHandle(editor::info::Field::Product));
  const auto fieldCenter = Center(productRect).position;
  const SU::Vec2 beforeField = view.windowPos();
  Run(view, fieldCenter.x, fieldCenter.y, true, true, false, ++id);
  const bool draggingOnField = view.dragging();
  Run(view, fieldCenter.x, fieldCenter.y, false, false, true, ++id);
  Check(!draggingOnField && view.windowPos().x == beforeField.x, "pressionar um campo nao arrasta a janela");

  // 4. Campo: clicar foca e coloca o caret; digitar entra no campo focado.
  Click(view, fieldCenter.x, fieldCenter.y, id);
  Check(model.hasFocus(editor::info::Field::Product), "clicar no campo Product foca o campo");
  model.InsertText("Teste");
  Check(model.text(editor::info::Field::Product) == "Teste", "digitar entra no campo focado");

  // 5. Botao Save info: clique dispara Action::Save.
  const SU::Rect saveRect = view.rectOf(view.saveHandle());
  const auto saveCenter = Center(saveRect).position;
  Check(Contains(Click(view, saveCenter.x, saveCenter.y, id), Action::Save), "botao Save info dispara Action::Save");

  // 6. Clique fora de tudo nao dispara acao.
  Check(Click(view, 5.f, 5.f, id).empty(), "clique fora da janela nao dispara acao");

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
