// Testes da janela Assets (SpriteUI): cliques na grade e na lista, duplo clique, renomear e apagar com confirmacao,
// filtro, editor de texto, rolagem, subir e migalhas. Nao abre janela: chama View::Frame com ponteiro sintetico.
// Fonte vem de EDITOR_FONT_PATH (CMake). Roda como Editor.AssetsView.
#include "assets_model.hpp"
#include "assets_spriteui.hpp"
#include "sui_panel.hpp"

#include <cstdio>
#include <string>
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
namespace AM = editor::assets_model;
using View = editor::assetsui::View;
using Action = View::Action;
using FrameInput = editor::assetsui::FrameInput;
using Mode = AM::State::Mode;

struct Result {
  std::vector<Action> actions;
};

// Um quadro com o ponteiro em (x, y) e o relogio em t (segundos). pressed/released sao so de um quadro.
Result Run(View& view, float x, float y, bool down, bool pressed, bool released, std::uint64_t id, double t) {
  FrameInput in;
  in.pointerX = x;
  in.pointerY = y;
  in.pointerDown = down;
  in.pointerPressed = pressed;
  in.pointerReleased = released;
  in.timeSeconds = t;
  in.frameId = id;
  Result r;
  view.Frame(kW, kH, in, r.actions);
  return r;
}

// Clique completo (pressionar e soltar em dois quadros) no centro do elemento, usando o retangulo do ultimo quadro.
std::vector<Action> ClickOn(View& view, SU::UIHandle h, std::uint64_t& id, double t = 0.0) {
  const SU::Rect r = view.rectOf(h);
  const float x = r.position.x + 0.5f * r.size.x;
  const float y = r.position.y + 0.5f * r.size.y;
  Run(view, x, y, false, false, false, ++id, t);   // o ponteiro chega antes (como no SDL): o roteador marca o alvo em hover
  std::vector<Action> all = Run(view, x, y, true, true, false, ++id, t).actions;
  const auto release = Run(view, x, y, false, false, true, ++id, t).actions;
  all.insert(all.end(), release.begin(), release.end());
  // O roteador entrega o clique um quadro depois da soltura: um quadro a mais recolhe essa acao.
  const auto late = Run(view, x, y, false, false, false, ++id, t).actions;
  all.insert(all.end(), late.begin(), late.end());
  return all;
}

// Um quadro vazio so para atualizar o layout (depois de mudar o estado fora da view).
void Settle(View& view, std::uint64_t& id) { Run(view, -1.f, -1.f, false, false, false, ++id, 0.0); }

bool Contains(const std::vector<Action>& a, Action what) {
  for (Action x : a) {
    if (x == what) return true;
  }
  return false;
}

AM::Item Make(const std::string& folder, const char* name, bool dir, editor::AssetKind kind) {
  AM::Item it;
  it.name = name;
  it.rel = folder + "/" + name;
  it.dir = dir;
  it.kind = kind;
  it.size = dir ? 0 : 2048;
  return it;
}

AM::State Sample() {
  AM::State s;
  s.folder = "Assets";
  s.items.push_back(Make("Assets", "Animations", true, editor::AssetKind::Folder));
  s.items.push_back(Make("Assets", "Models", true, editor::AssetKind::Folder));
  s.items.push_back(Make("Assets", "Cube.fbx", false, editor::AssetKind::Model));
  s.items.push_back(Make("Assets", "notes.txt", false, editor::AssetKind::Text));
  s.items.push_back(Make("Assets", "Main.scene.json", false, editor::AssetKind::Scene));
  return s;
}

}  // namespace

int main() {
  editor::sui::TextAtlasSet text;
  if (!text.load(EDITOR_FONT_PATH, 1.f)) {
    std::printf("FALHOU: fonte nao carregou: %s\n", EDITOR_FONT_PATH);
    return 1;
  }

  // 1. Clique na primeira celula seleciona o primeiro item visivel.
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.cellHandle(0, 0), id, 0.0);
    Check(s.selected == "Assets/Animations", "clique na celula seleciona o item");
  }

  // 2. Dois cliques seguidos na mesma celula viram duplo clique (Activate com o indice do item).
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.cellHandle(0, 1), id, 1.0);
    const auto acts = ClickOn(view, view.cellHandle(0, 1), id, 1.1);
    Check(Contains(acts, Action::Activate), "duplo clique pede Activate");
    Check(view.activatedItem() == 1, "duplo clique aponta o item certo (Models)");
  }

  // 3. Dois cliques com intervalo longo nao contam como duplo.
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.cellHandle(0, 1), id, 1.0);
    const auto acts = ClickOn(view, view.cellHandle(0, 1), id, 3.0);
    Check(!Contains(acts, Action::Activate), "dois cliques lentos nao viram duplo clique");
  }

  // 4. Renomear: abre o campo com o nome atual; nome de outro item e recusado; nome novo e aceito.
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.cellHandle(0, 2), id, 0.0);   // Cube.fbx
    ClickOn(view, view.renameHandle(), id, 0.0);
    Check(s.mode == Mode::Rename && s.name == "Cube.fbx", "renomear abre o campo com o nome atual");
    ClickOn(view, view.nameHandle(), id, 0.0);        // foca o campo
    for (int i = 0; i < 20; ++i) view.Backspace();
    view.TypeText("Models");
    const auto refused = ClickOn(view, view.confirmHandle(), id, 0.0);
    Check(!Contains(refused, Action::Confirm), "nome repetido nao confirma");
    Check(s.status == "A file with that name already exists.", "nome repetido mostra o motivo");
    ClickOn(view, view.nameHandle(), id, 0.0);        // Confirmar tirou o foco do campo: clica de novo para editar
    for (int i = 0; i < 20; ++i) view.Backspace();
    view.TypeText("Props.fbx");
    const auto ok = ClickOn(view, view.confirmHandle(), id, 0.0);
    Check(Contains(ok, Action::Confirm), "nome novo confirma");
  }

  // 5. Apagar pede confirmacao; so Confirmar entrega Delete; Cancelar desfaz.
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.cellHandle(0, 3), id, 0.0);   // notes.txt
    ClickOn(view, view.deleteHandle(), id, 0.0);
    Check(s.mode == Mode::Delete, "apagar entra em confirmacao");
    ClickOn(view, view.cancelHandle(), id, 0.0);
    Check(s.mode == Mode::None, "cancelar sai da confirmacao");
    ClickOn(view, view.deleteHandle(), id, 0.0);
    const auto acts = ClickOn(view, view.confirmHandle(), id, 0.0);
    Check(Contains(acts, Action::Delete), "confirmar apagar entrega Delete");
  }

  // 6. Filtro: digitar reduz a lista visivel (sem diferenciar maiusculas).
  {
    AM::State s = Sample();
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    ClickOn(view, view.filterHandle(), id, 0.0);
    view.TypeText("MO");
    Settle(view, id);
    Check(s.filter == "MO" && view.visible().size() == 1, "filtro acha Models sem diferenciar maiusculas");
    view.Backspace();
    view.Backspace();
    Settle(view, id);
    Check(view.visible().size() == 5, "apagar o filtro volta a lista inteira");
  }

  // 7. Editor de texto: digita no fim, Enter quebra linha, Save entrega SaveText; as celulas nao agem.
  {
    AM::State s = Sample();
    s.textOpen = true;
    s.text = "ab";
    s.textPath = "Assets/notes.txt";
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    view.TypeText("c");
    view.EnterKey();
    view.TypeText("d");
    Settle(view, id);
    Check(s.text == "abc\nd", "texto aberto recebe digitacao e Enter no fim");
    const std::string before = s.selected;
    ClickOn(view, view.cellHandle(0, 0), id, 0.0);
    Check(s.selected == before, "com o texto aberto, a grade nao seleciona");
    const auto acts = ClickOn(view, view.openHandle(), id, 0.0);
    Check(Contains(acts, Action::SaveText), "Save entrega SaveText");
  }

  // 8. Rolagem da lista: com mais itens que linhas, a roda avança e para no fim.
  {
    AM::State s;
    s.folder = "Assets";
    for (int i = 0; i < 20; ++i) s.items.push_back(Make("Assets", ("item" + std::to_string(i)).c_str(), false, editor::AssetKind::Other));
    s.iconView = false;
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    view.Scroll(-3);
    Settle(view, id);
    Check(s.scroll == 3, "roda do mouse rola a lista");
    view.Scroll(-100);
    Settle(view, id);
    Check(s.scroll == AM::MaxScroll(s.items.size(), false), "rolagem para no fim da lista");
  }

  // 9. Subir e migalhas: "<" sobe uma pasta; a migalha devolve a pasta clicada.
  {
    AM::State s = Sample();
    s.folder = "Assets/Models";
    View view(s, text);
    std::uint64_t id = 0;
    Settle(view, id);
    const auto up = ClickOn(view, view.upHandle(), id, 0.0);
    Check(Contains(up, Action::Up), "< pede Up");
    const auto crumb = ClickOn(view, view.crumbHandle(0), id, 0.0);
    Check(Contains(crumb, Action::Crumb) && view.pickedCrumb() == "Assets", "migalha devolve a pasta Assets");
  }

  if (gFailures == 0) std::printf("OK: janela Assets (SpriteUI)\n");
  return gFailures == 0 ? 0 : 1;
}
