// Testes de elementos da janela Scenes (SpriteUI): lista, botoes, campo de nome, assets, X, grip e clique fora.
// Nao abre janela: chama View::Frame com ponteiro sintetico. Fonte vem de EDITOR_FONT_PATH (CMake). Roda como
// Editor.ScenesView.
#include "scenes_model.hpp"
#include "scenes_ops.hpp"
#include "scenes_spriteui.hpp"
#include "sui_panel.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

// Host de teste: aplica as acoes como o main.cpp (criar = arquivo novo e vira a cena atual; renomear e apagar usam
// scenes_ops). Atualiza a lista a partir da pasta Scenes do projeto, como o refreshScenes do Editor.
void ApplyActions(const std::vector<Action>& acts, SM::Model& m, const editor::Project& proj) {
  for (Action a : acts) {
    std::string err;
    if (a == Action::Delete) {
      if (m.selected != m.current) editor::scenes_ops::DeleteSceneFile(proj, m.selected, err);
      m.status = err;
      SM::FinishName(m);
    } else if (a == Action::Confirm && m.mode == SM::NameMode::New) {
      std::ofstream(proj.scenePath(m.name)) << "{}";
      m.current = m.name;
      m.selected = m.name;
      SM::FinishName(m);
    } else if (a == Action::Confirm && m.mode == SM::NameMode::Rename) {
      if (editor::scenes_ops::RenameSceneFile(proj, m.selected, m.name, err)) {
        if (m.current == m.selected) m.current = m.name;
        m.selected = m.name;
        SM::FinishName(m);
      } else {
        m.status = err;
      }
    }
    m.scenes = proj.listScenes();
  }
}

// Clique no elemento e aplica as acoes no host de teste.
void ClickApply(View& view, SU::UIHandle h, std::uint64_t& id, SM::Model& m, const editor::Project& proj) {
  ApplyActions(ClickOn(view, h, id), m, proj);
}

// Um quadro sem clique (ponteiro parado fora de tudo) que devolve as acoes; usado depois de Enter.
std::vector<Action> IdleFrame(View& view, std::uint64_t& id) {
  std::vector<Action> acts;
  FrameInput in;
  in.pointerX = 2.f;
  in.pointerY = 2.f;
  in.frameId = ++id;
  (void)view.Frame(kW, kH, in, acts);
  return acts;
}

}  // namespace

int main() {
  editor::sui::TextAtlasSet text;
  if (!text.load(EDITOR_FONT_PATH, 1.f)) {
    std::printf("FALHOU: fonte nao carregou: %s\n", EDITOR_FONT_PATH);
    return 1;
  }

  SM::Model m;
  m.scenes = {"Main", "Alpha", "Zeta"};   // na lista: Alpha, Main (atual), Zeta
  m.current = "Main";
  m.assets = {"Assets/Models/a.obj"};
  View view(m, text);
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
  {   // Duplo clique numa cena que nao e a atual abre (Open); numa cena so selecionada com clique simples, nao.
    const SU::Rect r0 = view.rectOf(view.rowHandle(0));
    const float cx = r0.position.x + 0.5f * r0.size.x, cy = r0.position.y + 0.5f * r0.size.y;
    FrameInput dn;
    dn.pointerX = cx; dn.pointerY = cy; dn.pointerDown = true; dn.pointerPressed = true; dn.doubleClick = true; dn.frameId = ++id;
    std::vector<Action> acts;
    (void)view.Frame(kW, kH, dn, acts);
    FrameInput up;
    up.pointerX = cx; up.pointerY = cy; up.pointerReleased = true; up.frameId = ++id;
    (void)view.Frame(kW, kH, up, acts);
    Check(Contains(acts, Action::Open), "duplo clique numa cena abre (Open)");
    Check(!Contains(Click(view, cx, cy, id), Action::Open), "clique simples so seleciona");
  }
  // Apagar pede confirmacao: o primeiro clique so abre a pergunta; Cancelar desfaz; Apagar confirma.
  Check(!Contains(ClickOn(view, view.deleteHandle(), id), Action::Delete) && m.mode == SM::NameMode::Delete,
        "Delete pede confirmacao antes de apagar");
  Check(!Contains(ClickOn(view, view.openHandle(), id), Action::Open), "com confirmacao em curso, Open nao age");
  Check(!Contains(ClickOn(view, view.cancelHandle(), id), Action::Delete) && m.mode == SM::NameMode::None,
        "Cancelar desfaz o pedido de apagar");
  ClickOn(view, view.deleteHandle(), id);
  Check(Contains(ClickOn(view, view.confirmHandle(), id), Action::Delete), "Apagar confirmado dispara Delete");
  SM::FinishName(m);
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
  Check(!Contains(ClickOn(view, view.assignHandle(), id), Action::Assign) && !m.status.empty(),
        "Assign sem asset selecionado na janela Assets avisa e nao age");
  m.assignCandidate = "Assets/Models/b.obj";
  Check(Contains(ClickOn(view, view.assignHandle(), id), Action::Assign), "Assign com asset selecionado dispara Assign");
  m.assignCandidate.clear();
  const auto remove = ClickOn(view, view.assetRemoveHandle(0), id);
  Check(Contains(remove, Action::RemoveAsset) && view.removedIndex() == 0, "x do asset dispara RemoveAsset (indice 0)");

  // 7. "?" pede a Help da janela Scenes; minimizar recolhe ao cabecalho e volta ao clicar de novo.
  const SU::Rect help = view.helpRect();
  Check(Contains(Click(view, help.position.x + 0.5f * help.size.x, help.position.y + 0.5f * help.size.y, id), Action::Help),
        "o ? dispara Help");
  const SU::Rect minBtn = SW::WindowFrameButtonRect(view.frameRect(), view.frameMetrics(), false, SW::WindowFrameButton::Minimize);
  Click(view, minBtn.position.x + 0.5f * minBtn.size.x, minBtn.position.y + 0.5f * minBtn.size.y, id);
  Check(view.minimized() && view.frameRect().size.y < 60.f, "minimizar recolhe a janela ao cabecalho");
  Click(view, minBtn.position.x + 0.5f * minBtn.size.x, minBtn.position.y + 0.5f * minBtn.size.y, id);
  Check(!view.minimized() && view.frameRect().size.y > 400.f, "minimizar de novo restaura a janela");

  // 8. X fecha (Close).
  const SU::Rect frame = view.frameRect();
  const SU::Rect bar = SW::WindowFrameHeader(frame, view.frameMetrics(), false);
  const SU::Vec2 closePoint{frame.position.x + frame.size.x - 12.f, bar.position.y + bar.size.y * 0.5f};
  Check(Contains(Click(view, closePoint.x, closePoint.y, id), Action::Close), "X dispara Close");

  // O arraste pelo grip e o redimensionamento sao do host do dock (dock_host.hpp): a view so desenha o grip.

  // 9. Clique fora de qualquer elemento (canto da janela, sem botao) nao gera acao.
  const auto outside = Click(view, 2.f, 2.f, id);
  Check(outside.empty(), "clique fora nao gera acao");

  // 10. Fluxo completo com arquivos: criar, renomear (Enter e Esc), apagar, lista longa e layout.
  {
    namespace fs = std::filesystem;
    editor::Project proj;
    proj.root = fs::temp_directory_path() / "scenes_view_test_project";
    std::error_code ec;
    fs::remove_all(proj.root, ec);
    proj.ensure();
    std::ofstream(proj.scenePath("Main")) << "{}";
    std::ofstream(proj.scenePath("Alpha")) << "{}";

    SM::Model fm;
    fm.scenes = proj.listScenes();
    fm.current = "Main";
    fm.selected = "Main";
    View fv(fm, text);
    std::uint64_t fid = 0;
    (void)Run(fv, 2.f, 2.f, false, false, false, ++fid);

    // Barra: botoes na mesma linha e mesma altura; linhas da lista com altura fixa, dentro da lista, abaixo da barra.
    const SU::Rect rn = fv.rectOf(fv.newHandle()), rr = fv.rectOf(fv.renameHandle()), rd = fv.rectOf(fv.deleteHandle());
    const SU::Rect rl = fv.rectOf(fv.reloadHandle());
    Check(rn.size.y > 0.f && rn.size.y == rr.size.y && rr.size.y == rd.size.y && rd.size.y == rl.size.y,
          "botoes da barra tem a mesma altura");
    Check(rn.position.y == rr.position.y && rr.position.y == rd.position.y && rd.position.y == rl.position.y,
          "New, Rename, Delete e Reload ficam na mesma linha");
    const SU::Rect list = fv.rectOf(fv.listHandle());
    const SU::Rect row0 = fv.rectOf(fv.rowHandle(0)), row1 = fv.rectOf(fv.rowHandle(1));
    Check(row0.size.y == editor::scenesui::Theme::kRowHeight && row1.size.y == row0.size.y, "linhas da lista tem altura fixa");
    Check(row1.position.y == row0.position.y + row0.size.y, "linhas da lista sao consecutivas");
    Check(row0.position.y >= list.position.y && row1.position.y + row1.size.y <= list.position.y + list.size.y, "linhas dentro da lista");
    Check(list.position.y >= rn.position.y + rn.size.y, "lista fica abaixo da barra de botoes (sem sobrepor)");

    // Nova cena: abre o campo, digita, Enter cria o arquivo.
    ClickApply(fv, fv.newHandle(), fid, fm, proj);
    Check(fm.mode == SM::NameMode::New && fm.nameFocus, "New abre o campo de nome focado");
    fv.TypeText("Level 1");
    fv.PressEnter();
    const auto createActs = IdleFrame(fv, fid);
    Check(Contains(createActs, Action::Confirm), "Enter confirma a criacao (Confirm)");
    ApplyActions(createActs, fm, proj);
    Check(fs::exists(proj.scenePath("Level 1")), "criar gera o arquivo na pasta Scenes");
    Check(fm.current == "Level 1" && fm.mode == SM::NameMode::None, "a cena nova vira a atual e o campo fecha");
    Check(fm.scenes.size() == 3, "a lista tem a cena nova");

    // Esc cancela criar sem criar nada.
    ClickApply(fv, fv.newHandle(), fid, fm, proj);
    fv.TypeText("Ghost");
    Check(fv.PressEscape() && fm.mode == SM::NameMode::None && !fs::exists(proj.scenePath("Ghost")), "Esc cancela criar");

    // Renomear Alpha -> Beta (botao Rename, nome pre-preenchido, Enter confirma).
    (void)IdleFrame(fv, fid);
    ClickOn(fv, fv.rowHandle(0), fid);   // ordem: Alpha, Level 1, Main
    Check(fm.selected == "Alpha", "a linha 0 e Alpha e o clique seleciona");
    ClickApply(fv, fv.renameHandle(), fid, fm, proj);
    Check(fm.mode == SM::NameMode::Rename && fm.name == "Alpha", "Rename abre o campo com o nome atual");
    for (int i = 0; i < 5; ++i) fv.Backspace();
    fv.TypeText("Beta");
    fv.PressEnter();
    ApplyActions(IdleFrame(fv, fid), fm, proj);
    Check(fs::exists(proj.scenePath("Beta")) && !fs::exists(proj.scenePath("Alpha")), "renomear troca o arquivo");
    Check(fm.selected == "Beta" && fm.mode == SM::NameMode::None, "renomear fecha o campo e mantem a selecao");
    // Esc cancela renomear sem mexer no arquivo.
    ClickApply(fv, fv.renameHandle(), fid, fm, proj);
    fv.TypeText("X");
    fv.PressEscape();
    Check(fs::exists(proj.scenePath("Beta")) && fm.mode == SM::NameMode::None, "Esc cancela renomear");
    // Nome repetido: nao renomeia e mostra o problema; o campo continua em foco.
    ClickApply(fv, fv.renameHandle(), fid, fm, proj);
    for (int i = 0; i < 4; ++i) fv.Backspace();
    fv.TypeText("Main");
    ClickApply(fv, fv.confirmHandle(), fid, fm, proj);
    Check(fs::exists(proj.scenePath("Beta")) && !fm.status.empty() && fm.nameFocus, "nome repetido nao renomeia e mantem o campo");
    fv.PressEscape();

    // Apagar: pergunta, confirma, arquivo some. A cena atual nao se apaga.
    (void)IdleFrame(fv, fid);
    ClickApply(fv, fv.deleteHandle(), fid, fm, proj);
    Check(fm.mode == SM::NameMode::Delete && fs::exists(proj.scenePath("Beta")), "Delete so pergunta");
    ClickApply(fv, fv.confirmHandle(), fid, fm, proj);
    Check(!fs::exists(proj.scenePath("Beta")) && fm.mode == SM::NameMode::None, "confirmar apaga o arquivo");
    Check(fm.scenes.size() == 2, "a lista perde a cena apagada");
    fm.selected = fm.current;
    (void)IdleFrame(fv, fid);
    ClickApply(fv, fv.deleteHandle(), fid, fm, proj);
    Check(fm.mode == SM::NameMode::None && fs::exists(proj.scenePath(fm.current)), "a cena atual nao se apaga");

    // Lista longa: a roda mostra as cenas alem das 6 primeiras e o clique acerta a linha certa.
    for (int i = 0; i < 12; ++i) {
      char nm[16];
      std::snprintf(nm, sizeof nm, "S%02d", i);
      std::ofstream(proj.scenePath(nm)) << "{}";
    }
    fm.scenes = proj.listScenes();
    (void)IdleFrame(fv, fid);
    const SU::Rect lr = fv.rectOf(fv.listHandle());
    fv.wheelAt(lr.position.x + 20.f, lr.position.y + 20.f, 40.f * 4.f);
    (void)IdleFrame(fv, fid);
    Check(fv.firstVisibleRow() > 0, "a roda rola a lista de cenas");
    const auto rowsNow = SM::Rows(fm.scenes, fm.current);
    ClickOn(fv, fv.rowHandle(0), fid);
    Check(fm.selected == rowsNow[fv.firstVisibleRow()].name, "clicar na primeira linha visivel seleciona a cena certa");
    fs::remove_all(proj.root, ec);
  }

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
