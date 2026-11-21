# Pendências: remoção do ImGui e independência do SpriteUI

Estado em 2026-11-21. Branch `ui-settings-router`.

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
