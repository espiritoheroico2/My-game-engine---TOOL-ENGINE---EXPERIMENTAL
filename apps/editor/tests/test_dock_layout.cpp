// Testes do docking das janelas (dock_layout.hpp): layout padrao, abas, encaixe por arraste, remover e flutuar.
// Sem janela e sem SDL. Roda no ctest como Editor.DockLayout.
#include "dock_layout.hpp"

#include <cstdio>

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const char* what) {
  ++gChecks;
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

bool Near(float a, float b) { return a > b - 0.5f && a < b + 0.5f; }

ToolEngine::SpriteUi::UIHandle Win(std::uint32_t index) {
  return ToolEngine::SpriteUi::UIHandle{index, 1u};
}

}  // namespace

int main() {
  namespace dl = editor::dock_layout;
  namespace SW = ToolEngine::SpriteUi::Widgets;
  namespace SU = ToolEngine::SpriteUi;

  const SU::UIHandle hier = Win(1), scene = Win(2), console = Win(3), assets = Win(4), profiler = Win(5);
  const SU::Rect area{{0.f, 0.f}, {1000.f, 600.f}};

  // 1. Layout vazio e primeira janela na raiz.
  {
    dl::Model m;
    Check(m.empty(), "modelo novo esta vazio");
    Check(m.place(scene), "primeira janela entra no dock");
    m.setArea(area);
    Check(m.contains(scene) && !m.empty(), "a janela colocada esta no dock");
    Check(!m.place(scene), "a mesma janela nao entra duas vezes");
    Check(m.leafRect(scene)->size.x == 1000.f, "a unica janela ocupa a area inteira");
  }

  // 2. Divisao padrao: Hierarquia a esquerda, Scene a direita.
  dl::Model m;
  m.place(scene);
  m.setArea(area);
  Check(m.split(scene, SW::DockSlot::Left, dl::kSideRatio, hier), "divide a Scene e poe a Hierarquia a esquerda");
  m.setArea(area);
  Check(Near(m.leafRect(hier)->size.x, 1000.f * dl::kSideRatio), "a Hierarquia tem a largura da lateral");
  Check(Near(m.leafRect(scene)->position.x, 1000.f * dl::kSideRatio), "a Scene comeca depois da lateral");
  Check(!m.split(scene, SW::DockSlot::Left, 0.2f, hier), "janela ja no dock nao e dividida de novo");
  Check(!m.split(profiler, SW::DockSlot::Left, 0.2f, console), "alvo que nao existe nao divide");

  // 3. Abas: Console junto da Scene. A faixa de abas tira altura do conteudo.
  Check(m.split(scene, SW::DockSlot::Bottom, dl::kBottomRatio, console), "poe o Console embaixo da Scene");
  m.setArea(area);
  const SU::Rect sceneBefore = *m.contentRect(scene);
  Check(m.tabsOf(scene).empty(), "folha com uma janela nao tem abas");
  SW::DockDropResult tab;
  tab.action = SW::DockAction::Tab;
  tab.targetLeaf = scene;
  Check(m.apply(profiler, tab), "Profiler entra como aba da Scene");
  m.setArea(area);
  Check(m.tabsOf(scene).size() == 2, "a folha da Scene agora tem duas abas");
  Check(Near(m.contentRect(scene)->position.y, m.leafRect(scene)->position.y),
        "com abas, o conteudo comeca no topo da folha (abas ficam na barra da moldura)");
  Check(Near(m.contentRect(scene)->size.y, m.leafRect(scene)->size.y),
        "com abas, o conteudo tem a altura da folha");
  Check(m.select(profiler) && m.tabsOf(profiler).size() == 2, "selecionar uma aba mantem o grupo");
  (void)sceneBefore;

  // 4. Arraste de verdade: solta o Assets na borda esquerda da Scene. Vira divisao, nao aba.
  {
    SW::DockDragController controller;
    controller.BeginPress(assets, {500.f, 10.f}, false);
    controller.Move({520.f, 12.f});
    controller.Move({600.f, 300.f});
    SW::DockDropResult drop = m.release(controller, {m.leafRect(scene)->position.x + 2.f, 300.f});
    Check(drop.action == SW::DockAction::Dock && drop.slot == SW::DockSlot::Left, "soltar na borda esquerda divide a folha");
    Check(m.apply(assets, drop), "aplica o encaixe por borda");
    m.setArea(area);
    Check(m.contains(assets), "Assets esta no dock depois do encaixe");
    Check(m.leafRect(assets)->position.x < m.leafRect(scene)->position.x, "Assets ficou a esquerda da Scene");
  }

  // 5. Arraste solto fora do dock: flutuante. Remover tira a janela e colapsa.
  {
    SW::DockDragController controller;
    controller.BeginPress(console, {10.f, 10.f}, true);
    controller.Move({400.f, 400.f});
    SW::DockDropResult drop = m.release(controller, {5000.f, 5000.f});
    Check(drop.action == SW::DockAction::Float, "soltar fora do dock vira flutuante");
    Check(m.apply(console, drop) && !m.contains(console), "Float tira o Console do dock");
    m.setArea(area);
    Check(!m.leafRect(console).has_value(), "janela flutuante nao tem folha");
    Check(!m.remove(console), "remover de novo nao faz nada");
  }

  // 5b. Aba inativa nao esta ativa; a aba selecionada esta.
  {
    dl::Model t;
    t.place(scene);
    t.setArea(area);
    t.split(scene, SW::DockSlot::Right, 0.5f, console);
    SW::DockDropResult tabDrop;
    tabDrop.action = SW::DockAction::Tab;
    tabDrop.targetLeaf = scene;
    t.apply(hier, tabDrop);
    t.setArea(area);
    Check(t.active(hier) != t.active(scene), "de duas abas, so uma esta ativa");
    t.select(hier);
    Check(t.active(hier) && !t.active(scene), "selecionar a aba troca a janela ativa");
    Check(!t.active(scene) && t.contains(scene), "aba inativa continua no dock");
  }

  // 6. Remover a aba: sobra a janela e a faixa some.
  Check(m.remove(profiler), "remove o Profiler da aba");
  m.setArea(area);
  Check(m.tabsOf(scene).empty() && !m.contains(profiler), "sem a aba, a folha volta a ter so a Scene");
  Check(Near(m.contentRect(scene)->position.y, m.leafRect(scene)->position.y), "sem aba, o conteudo volta ao topo da folha");

  // 7. Folhas: a Hierarquia, a Scene, o Console e os Assets (Profiler saiu) estao nas folhas visiveis.
  m.setArea(area);
  const auto leafList = m.leaves();
  Check(leafList.size() >= 3, "as folhas visiveis cobrem as janelas do dock");

  // 8. Layout salvo: texto ida e volta, abas preservadas, e texto invalido nao muda o dock.
  {
    dl::Model src;
    src.place(scene);
    src.setArea(area);
    src.split(scene, SW::DockSlot::Left, dl::kSideRatio, hier);
    src.split(scene, SW::DockSlot::Bottom, dl::kBottomRatio, console);
    SW::DockDropResult tabDrop;
    tabDrop.action = SW::DockAction::Tab;
    tabDrop.targetLeaf = console;
    src.apply(profiler, tabDrop);
    src.select(profiler);
    src.setArea(area);
    const std::string text = src.serialize();
    Check(!text.empty() && text != "E", "layout com janelas gera texto");

    auto known = [&](SU::UIHandle h) { return h == hier || h == scene || h == console || h == assets || h == profiler; };
    dl::Model back;
    back.setArea(area);
    Check(back.deserialize(text, known), "texto salvo e lido de volta");
    back.setArea(area);
    Check(back.serialize() == text, "ler e salvar de novo da o mesmo texto");
    Check(back.tabsOf(profiler).size() == 2 && back.active(profiler), "a aba ativa salva continua ativa");
    Check(back.leafRect(hier)->size.x == src.leafRect(hier)->size.x, "tamanhos das folhas sao preservados");

    dl::Model bad;
    bad.setArea(area);
    bad.place(scene);
    bad.setArea(area);
    const std::string before = bad.serialize();
    Check(!bad.deserialize("L 1 0 9:9", known), "janela desconhecida recusa o layout");
    Check(!bad.deserialize("S H 0.5 L 1 0 2:1 L 1 0 2:1", known), "janela repetida recusa o layout");
    Check(!bad.deserialize("S X 0.5 L 1 0 2:1 L 1 0 3:1", known), "direcao invalida recusa o layout");
    Check(!bad.deserialize("L 2 5 2:1 3:1", known), "aba ativa fora da lista recusa o layout");
    Check(!bad.deserialize("L 1 0 2:1 lixo", known), "texto sobrando recusa o layout");
    Check(bad.serialize() == before, "layout recusado deixa o dock como estava");
    Check(bad.deserialize("E", known) && bad.empty(), "dock vazio e lido como vazio");
  }

  std::printf("Editor.DockLayout: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
