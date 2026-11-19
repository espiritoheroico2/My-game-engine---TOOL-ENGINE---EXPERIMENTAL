// Teste da secao Tags do Inspector (inspector_panel.hpp): a secao entra no bloco e ocupa altura.
// Sem janela: prepare() so monta os controles. Fonte vem de EDITOR_FONT_PATH (CMake). Roda como Editor.InspectorTags.
#include "inspector_panel.hpp"
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

}  // namespace

int main() {
  namespace IP = editor::inspector_panel;

  ToolEngine::SpriteUi::Text::TruetypeAtlas atlas;
  if (!editor::sui::LoadTextAtlas(EDITOR_FONT_PATH, atlas, 1.f)) {
    std::printf("FALHOU: fonte nao carregou: %s\n", EDITOR_FONT_PATH);
    return 1;
  }
  editor::sui::SpriteUiSdlRenderer renderer;

  // Objeto sem componentes: so o cabecalho e a secao Tags.
  IP::Context base;
  base.hasObject = true;
  base.entityIndex = 1;
  base.entityGeneration = 1;

  IP::Hooks hooks;
  int removed = -1;
  hooks.removeTagIndex = [&removed](std::size_t i) { removed = static_cast<int>(i); };

  // 1. Sem tags na cena: so o campo "New tag", o botao e a lista vazia.
  IP::InspectorPanel noTags(atlas, renderer);
  const float hEmpty = noTags.prepare(base, hooks, 300.f);

  // 2. Com tags: uma tag atribuida (com Remove) e outra existente na lista. A altura cresce.
  IP::Context withTags = base;
  withTags.tagNames = {"Player", "Enemy", "Boss"};
  withTags.tagOn = {true, false, false};
  IP::InspectorPanel tagged(atlas, renderer);
  const float hTagged = tagged.prepare(withTags, hooks, 300.f);

  Check(hEmpty > 0.f && hTagged > 0.f, "prepare devolve altura positiva");
  Check(hTagged > hEmpty, "com tags a secao ocupa mais altura");

  // 3. Ocultar a secao (Expanders) deixa o bloco menor. Fecha a secao Tags pelo estado do painel.
  tagged.setAllSections(false);
  const float hCollapsed = tagged.prepare(withTags, hooks, 300.f);
  Check(hCollapsed < hTagged, "secao Tags fechada ocupa menos altura");
  tagged.setAllSections(true);

  // 4. Objeto travado: os controles ficam so de leitura, mas a altura da secao continua a mesma.
  IP::Context locked = withTags;
  locked.locked = true;
  const float hLocked = tagged.prepare(locked, hooks, 300.f);
  Check(hLocked == hTagged, "objeto travado: mesma altura (so os controles ficam desabilitados)");

  (void)removed;   // o clique no botao Remove e testado a mao (coordenadas dependem do layout)
  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
