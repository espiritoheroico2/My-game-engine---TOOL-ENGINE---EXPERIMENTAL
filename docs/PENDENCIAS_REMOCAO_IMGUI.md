# Pendências: remoção do ImGui e independência do SpriteUI

Estado em 2026-11-21. Branch `ui-settings-router`.

## Verificacao realizada nesta atualizacao

- Build GPU sem Direct3D 12 configurada e compilada com sucesso (`build_editor.bat gpu nodx12 norun`).
- Todos os targets foram reconstruidos apos o build; a build completa passou.
- Suíte relevante: 42/43 testes passaram. A unica falha era uma expectativa antiga no teste
  `Editor.MainMenuModel` (F1 estava no indice 15, apos o item SpriteUI diagnostics); o indice foi corrigido no teste.
- Corrigida uma dependencia de cabeçalho: `window_palette.hpp` agora inclui `sui_panel.hpp`, necessária para
  `sui::Srgb` em targets isolados (testes e capturas).
- Estado conferido no repositorio: branch atual `main`, worktree limpo antes desta atualizacao.
- A descrição original de "janela de script portada para SpriteUI" não corresponde ao estado atual:
  `script_window.hpp` ainda contém a implementação antiga com ImGui e não existe `gScriptPanel` neste codigo.
  A janela de Coding ja usa uma moldura SpriteUI, mas o corpo continua sobre `ImDrawList`/ImGui.

## Melhorias de desempenho do SpriteUI verificadas nesta etapa

- Backend SDL: cache do recorte ativo evita chamadas repetidas a `SDL_SetRenderClipRect` em batches consecutivos.
- Backend SDL: reservas exatas de vértices e índices por batch reduzem realocações durante o desenho.
- Cantos arredondados: seno e cosseno dos arcos são calculados uma vez e reutilizados em todos os quadros.
- Conversão linear para sRGB: tabela estática de 256 entradas substitui chamadas repetidas a `std::pow`.
- Medição de texto: `Canvas::lineHeight` guarda o último tamanho medido, evitando reformatar `"Ag"` a cada linha.
- Extrator: comparação de ordenação manual no lugar da tupla temporária em cada comparação do `stable_sort`.
- Build completa passou e a suíte relevante passou com 43/43 testes.
- Captura headless: 120 quadros concluídos; SpriteUI produziu 8639 chamadas SDL, média de 72 chamadas por quadro.

## Adaptador ECS da SpriteUI

- Implementado o adaptador host `apps/editor/spriteui_ecs.hpp`.
- Elementos SpriteUI passam a ter entidades ECS da ToolEngine e componentes separados:
  `UINode`, `UIRectTransform`, `UIVisualState` e `UIVisibility`.
- O adaptador mantém handles com geração em ambos os lados, layout flexível, visual, extração
  de render/hit-test e remoção recursiva da subárvore.
- Existe limpeza para entidades destruídas diretamente pelo `tool::World`, via `PruneDestroyed()`.
- Teste dedicado `Editor.SpriteUiEcs`: criação, pai-filho, layout, visual, visibilidade,
  destruição recursiva e poda de entidade destruída fora do adaptador.
- Próximo passo: ligar um painel piloto real a este adaptador e migrar seus dados/callbacks
  sem substituir o restante do editor. Manter `LayoutTree` como layout interno até essa integração
  estar validada visualmente.

## Light Prism e ECS

- `LightComp` e `GlobalLight` já são componentes ECS da ToolEngine; `collectLights` é a ponte para a Light Prism.
- O core da Light Prism permanece independente de ECS e usa `SlotMap` com handles e geração.
- `LightRig` agora atualiza pontuais/spots existentes pelo handle, evitando remover e recriar toda a lista.

## Correcoes de sobreposicao e cursor

- Settings: o conteudo das secoes agora comeca abaixo da faixa de abas; recolher uma secao limpa seu
  canvas e os controles so sao desenhados quando a secao Graphics esta aberta.
- Hierarchy: botoes da barra usam altura propria de 22 px em vez de reutilizar o padding da janela.
- Menu `+ Create`: fundo, largura limitada, altura limitada e hover por linha; as linhas nao transbordam
  nem desenham texto sobre texto.
- Cursor de texto: usa o cursor I-beam do sistema; se o SDL nao suportar o cursor nativo, cria um cursor
  I-beam bitmap de fallback. A arvore da Hierarchy ativa o cursor durante renomeacao e selecao arrastada.

## Janela Coding

- Contador de linhas na coluna esquerda do editor.
- Texto do codigo limitado ao tamanho minimo de 12 px.
- Controles `-` e `+` na barra superior alteram o tamanho da fonte de 12 a 28 px.
- Selecao por arrasto, retorno visual do caret e cursor I-beam no editor de codigo.

## Feito e enviado

- `3d688ef` — remoção do código ImGui morto da Scene, Game e Hierarchy (`drawSceneView`,
  `drawGameView`, `drawGrid`, `drawCulledOverlay`, `circleXZ`, `drawGizmos`, `playButton`,
  `drawHierarchy`, `transformGizmo`, `GizmoState`). Build e testes de layout (44 checks) passando;
  captura headless sem diferença visual.

## Em andamento (não commitado)

Janela de script (`apps/editor/script_window.hpp`) portada para SpriteUI:

- Moldura com `framed::Chrome`; corpo com `view::ShapeList` e `Elements::TextField` multilinha.
- Botões Save, Complete, Snippets, Close; autocomplete; lista de exemplos; verificador com erros por linha.
- Ligação em `main.cpp`: `gScriptPanel`, roteamento de eventos (`scrHandled`), render após o `gDock->overlay`.
- Correção de include em `window_palette.hpp` (faltava `sui_panel.hpp` para `sui::Srgb`).
- Flag de captura `--edit-script` (anexa um script existente ao objeto selecionado, só em memória).

Pendente nesta janela:

1. Verificar a captura após corrigir âncoras do campo (`anchorMin/anchorMax` = 0/1) e a ordem de desenho
   (render após o overlay do dock). A última captura mostrou o campo com largura errada e a janela cortada.
2. Confirmar a direção da roda do mouse (`field_->Wheel(-y * ...)`). Não é testável em headless.
3. Barra de rolagem vertical não arrasta pelo polegar (só roda do mouse).
4. Teclado e mouse reais (seleção por arrasto, Ctrl+S, Ctrl+Espaço, sugestões, snippets) não verificados.
5. Depois da aprovação visual: remover `ed_::scriptWindowUi` restante e o código ImGui do script antigo.

## Próximas etapas (em ordem)

1. **Editor de código do Coding** (`apps/editor/coding_panel.hpp`): ainda desenha pelo draw list do ImGui e lê
   teclado pelo ImGui. Reusar o mesmo editor SpriteUI da janela de script.
2. **Diálogo de projeto** (`project_dialog.hpp`, 41 chamadas).
3. **Exportar** (`export_ui.hpp`, 41 chamadas).
4. **Narrative Controller** (`narrative_panel.hpp`, 55 chamadas).
5. **`extra_ui.hpp`** (236 chamadas; inclui o overlay de física e outros). Inventário função a função antes.
6. **`fx.hpp`** (16 chamadas).
7. **Dependências de tipo ImGui**: `theme.hpp`, `ui_contract.hpp`, `view_shapes.hpp`, `editor_scripts.hpp`,
   `ui_test.cpp`, e helpers de viewport com `ImDrawList` (`ViewXf`, `line3`, `snapToContact`, `pickByRay`,
   `rayFromScreen`, usados por `selftest.hpp`).
8. **Backend do ImGui em `main.cpp`** (contexto, input, `ImGui::Render`, DockBuilder residual). Só sai quando
   nenhum arquivo acima usar `ImGui::`.

## Regras do projeto

- Não remover código ImGui sem aprovação visual do usuário, janela a janela.
- Não commitar `proj_ecs/`.
- Mensagens de commit terminam com `Co-Authored-By` e `Claude-Session` (ver convenção do repositório).

## Perguntas em aberto

- Desagrupar abas: hoje a aba precisa ser ativada e o grip da barra arrastado para fora. Quer um grip por aba,
  ou arrastar o nome da aba também desagrupa?
- A build sem SpriteUI (`TOOL_WITH_SPRITEUI=OFF`) deixou de desenhar Scene e Game. Remover os ramos `#else` restantes?
