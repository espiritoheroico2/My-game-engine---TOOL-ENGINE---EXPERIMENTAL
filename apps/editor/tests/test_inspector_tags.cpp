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

  // 5. Secao ToolObject Script. Sem script: so o Help e o Add Script. Com script: o arquivo, Edit script e a
  //    checagem Script enabled, por isso o bloco e mais alto. Durante o Play entra o status e o Reload.
  IP::InspectorPanel noScript(atlas, renderer);
  const float hNoScript = noScript.prepare(base, hooks, 300.f);
  editor::ScriptComp script{};
  IP::Context withScript = base;
  withScript.script = &script;
  withScript.scriptPath = "scripts/player.js";
  IP::InspectorPanel scripted(atlas, renderer);
  const float hScripted = scripted.prepare(withScript, hooks, 300.f);
  Check(hScripted > hNoScript, "com script a secao ocupa mais altura que sem script");
  IP::Context playing = withScript;
  playing.playing = true;
  playing.scriptStatus = "running";
  IP::InspectorPanel onPlay(atlas, renderer);
  const float hPlaying = onPlay.prepare(playing, hooks, 300.f);
  Check(hPlaying > hScripted, "durante o Play a secao mostra status e Reload (mais altura)");

  // 6. Secao Physics. Com fisica dinamica ha mais campos (massa, arrastes, gravidade) do que com corpo estatico.
  editor::PhysicsComp phys{};
  phys.body = 0;   // estatico
  IP::Context staticBody = base;
  staticBody.physics = &phys;
  IP::InspectorPanel staticPanel(atlas, renderer);
  const float hStatic = staticPanel.prepare(staticBody, hooks, 300.f);
  phys.body = 1;   // dinamico
  IP::InspectorPanel dynamicPanel(atlas, renderer);
  const float hDynamic = dynamicPanel.prepare(staticBody, hooks, 300.f);
  Check(hDynamic > hStatic, "fisica dinamica ocupa mais altura que estatica");
  phys.overrideGravity = true;
  IP::InspectorPanel ownGravity(atlas, renderer);
  const float hOwnGravity = ownGravity.prepare(staticBody, hooks, 300.f);
  Check(hOwnGravity > hDynamic, "gravidade propria (Vector physics) mostra o campo e os atalhos");

  // 7. Secao Tools. Cada ferramenta acrescenta o seu bloco (titulo, Remove, Enabled, Player e campos do tipo).
  editor::ToolsComp tools{};
  IP::Context noTools = base;
  noTools.tools = &tools;
  IP::InspectorPanel emptyTools(atlas, renderer);
  const float hNoTools = emptyTools.prepare(noTools, hooks, 300.f);
  tools.count = 1;
  tools.items[0].kind = 4;   // On Collision (com a aviso de Physics)
  IP::InspectorPanel oneTool(atlas, renderer);
  const float hOneTool = oneTool.prepare(noTools, hooks, 300.f);
  tools.count = 2;
  tools.items[1].kind = 3;   // Spin
  IP::InspectorPanel twoTools(atlas, renderer);
  const float hTwoTools = twoTools.prepare(noTools, hooks, 300.f);
  Check(hOneTool > hNoTools, "uma ferramenta acrescenta bloco na secao Tools");
  Check(hTwoTools > hOneTool, "cada ferramenta nova deixa a secao mais alta");

  // 8. Secao Voxel. A grade 16x16 da camada acrescenta uma linha por celula da altura; o chunk vazio ja tem a grade.
  editor::VoxelChunk chunk{};
  IP::Context withVoxel = base;
  withVoxel.voxel = &chunk;
  IP::InspectorPanel voxelPanel(atlas, renderer);
  const float hVoxel = voxelPanel.prepare(withVoxel, hooks, 300.f);
  Check(hVoxel > hEmpty, "a secao Voxel (com a grade da camada) ocupa altura");
  voxelPanel.setAllSections(false);
  const float hVoxelClosed = voxelPanel.prepare(withVoxel, hooks, 300.f);
  Check(hVoxelClosed < hVoxel, "secao Voxel fechada ocupa menos altura");
  voxelPanel.setAllSections(true);

  (void)removed;   // o clique no botao Remove e testado a mao (coordenadas dependem do layout)
  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
