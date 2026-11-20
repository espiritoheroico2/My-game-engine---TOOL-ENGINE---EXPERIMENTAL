// Testes de elementos da janela Scenes (SpriteUI): lista, botoes, campo de nome, assets, X, grip e clique fora.
// Nao abre janela: chama View::Frame com ponteiro sintetico. Fonte vem de EDITOR_FONT_PATH (CMake). Roda como
// Editor.ScenesView.
#include "scenes_model.hpp"
#include "scenes_spriteui.hpp"
#include "sui_panel.hpp"

#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

constexpr float kW = 760.f;
constexpr float kH = 720.f;

namespace SU = ToolEngine::SpriteUi;
namespace SW = ToolEngine::SpriteUi::Widgets;
using View = editor::scenesui::View;
using Action = View::Action;
using FrameInput = editor::scenesui::FrameInput;
namespace SM = editor::scenes_model;

struct Result {
  std::vector<Action> actions;
  std::size_t batches = 0;
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
  return r;
}

// Clique completo (pressionar e soltar em dois quadros). Devolve as acoes dos dois quadros.
std::vector<Action> Click(View& view, float x, float y, std::uint64_t& id) {
  std::vector<Action> all = Run(view, x, y, true, true, false, ++id).actions;
  const auto release = Run(view, x, y, false, false, true, ++id).actions;
  all.insert(all.end(), release.begin(), release.end());
  return all;
}

// Clique no centro de um elemento (usa o retangulo do ultimo quadro).
std::vector<Action> ClickOn(View& view, SU::UIHandle h, std::uint64_t& id) {
  const SU::Rect r = view.rectOf(h);
  return Click(view, r.position.x + 0.5f * r.size.x, r.position.y + 0.5f * r.size.y, id);
}

bool Contains(const std::vector<Action>& a, Action what) {
  for (Action x : a) {
    if (x == what) return true;
  }
  return false;
}

}  // namespace

int main() {
  ToolEngine::SpriteUi::Text::TruetypeAtlas atlas;
  if (!editor::sui::LoadTextAtlas(EDITOR_FONT_PATH, atlas, 1.f)) {
    std::printf("FALHOU: fonte nao carregou: %s\n", EDITOR_FONT_PATH);
    return 1;
  }

  SM::Model m;
  m.scenes = {"Main", "Alpha", "Zeta"};   // na lista: Alpha, Main (atual), Zeta
  m.current = "Main";
  m.assets = {"Assets/Models/a.obj"};
  View view(m, atlas);
  std::uint64_t id = 0;

  // 1. Um quadro desenha a janela (batches) e coloca cada elemento no lugar.
  const auto first = Run(view, 2.f, 2.f, false, false, false, ++id);
  Check(first.batches > 0, "a janela desenha algo (batches)");

  // 2. Botoes de cima: Salvar e Recarregar disparam a acao; Nova cena abre o campo de nome.
  Check(Contains(ClickOn(view, view.saveHandle(), id), Action::Save), "Save scene dispara Save");
  Check(Contains(ClickOn(view, view.reloadHandle(), id), Action::Reload), "Reload list dispara Reload");
  ClickOn(view, view.newHandle(), id);
  Check(m.mode == SM::NameMode::New && m.name.empty(), "New scene abre o campo de nome vazio");
  SM::FinishName(m);

  // 3. Lista: a primeira linha (ordem de nome) seleciona Alpha.
  ClickOn(view, view.rowHandle(0), id);
  Check(m.selected == "Alpha", "clicar na lista seleciona a cena (ordem de nome)");
  Check(Contains(ClickOn(view, view.openHandle(), id), Action::Open), "Open em outra cena dispara Open");
  Check(Contains(ClickOn(view, view.deleteHandle(), id), Action::Delete), "Delete em outra cena dispara Delete");
  ClickOn(view, view.renameHandle(), id);
  Check(m.mode == SM::NameMode::Rename && m.name == "Alpha", "Rename preenche o campo com o nome da selecao");
  SM::FinishName(m);

  // 4. Cena atual selecionada: Open e Delete nao valem (botoes apagados).
  ClickOn(view, view.rowHandle(1), id);   // Main, a atual
  Check(m.selected == "Main", "seleciona a cena atual");
  Check(!Contains(ClickOn(view, view.openHandle(), id), Action::Open), "Open na cena atual nao faz nada");
  Check(!Contains(ClickOn(view, view.deleteHandle(), id), Action::Delete), "Delete na cena atual nao faz nada");

  // 5. Nome: criar com nome repetido nao confirma (mostra o problema); digitar e apagar mudam o campo.
  ClickOn(view, view.newHandle(), id);
  ClickOn(view, view.nameHandle(), id);
  Check(m.nameFocus, "clicar no campo de nome foca quando ha criar em curso");
  view.TypeText("Alpha");
  Check(m.name == "Alpha", "digitar entra no campo de nome");
  Check(!Contains(ClickOn(view, view.confirmHandle(), id), Action::Confirm), "nome repetido nao confirma");
  Check(!m.status.empty(), "nome repetido mostra o problema");
  ClickOn(view, view.nameHandle(), id);   // o clique no Confirm tirou o foco: volta ao campo
  for (int i = 0; i < 5; ++i) view.Backspace();
  Check(m.name.empty(), "apagar tira os caracteres do campo");
  view.TypeText("Level 2");
  Check(Contains(ClickOn(view, view.confirmHandle(), id), Action::Confirm), "nome novo confirma (Confirm)");
  Check(m.name == "Level 2" && m.status.empty(), "confirmar mantem o nome e limpa o problema");
  SM::FinishName(m);
  ClickOn(view, view.nameHandle(), id);
  Check(!m.nameFocus, "clicar no campo sem criar ou renomear nao foca");

  // 6. Assets: Assign dispara Assign; o x do asset 0 dispara RemoveAsset com indice 0.
  Check(Contains(ClickOn(view, view.assignHandle(), id), Action::Assign), "Assign asset dispara Assign");
  const auto remove = ClickOn(view, view.assetRemoveHandle(0), id);
  Check(Contains(remove, Action::RemoveAsset) && view.removedIndex() == 0, "x do asset dispara RemoveAsset (indice 0)");

  // 7. X fecha (Close) e o grip arrasta a janela; o mesmo arraste no conteudo nao arrasta.
  const SU::Rect frame = view.frameRect();
  const SU::Rect bar = SW::WindowFrameHeader(frame, view.frameMetrics(), false);
  const SU::Vec2 closePoint{frame.position.x + frame.size.x - 12.f, bar.position.y + bar.size.y * 0.5f};
  Check(Contains(Click(view, closePoint.x, closePoint.y, id), Action::Close), "X dispara Close");

  const SU::Rect grip = SW::WindowGripRect(bar, 14.f);
  const SU::Vec2 start{grip.position.x + grip.size.x * 0.5f, grip.position.y + grip.size.y * 0.5f};
  const SU::Vec2 before = view.windowPos();
  Run(view, start.x, start.y, true, true, false, ++id);
  Run(view, start.x + 40.f, start.y + 20.f, true, false, false, ++id);
  Run(view, start.x + 40.f, start.y + 20.f, false, false, true, ++id);
  Check(view.windowPos().x != before.x || view.windowPos().y != before.y, "o grip arrasta a janela");

  // 8. Clique fora de qualquer elemento (canto da janela, sem botao) nao gera acao.
  const auto outside = Click(view, 2.f, 2.f, id);
  Check(outside.empty(), "clique fora nao gera acao");

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
