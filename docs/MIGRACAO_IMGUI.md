# Migracao do ImGui para SpriteUI: o que falta

Contagem feita com `grep -o "ImGui::"` em `apps/editor` (1445 chamadas no total). Ordem: do mais facil para o mais
dificil. Narrative fica por ultimo, como pediu. Cada etapa so fecha quando o arquivo nao tem mais `ImGui::`.

| Ordem | Arquivo | Chamadas | Observacao |
|---|---|---|---|
| 1 | `view_shapes.hpp` | 1 | |
| 2 | `ui_contract.hpp` | 4 | Contrato de cores/elementos |
| 3 | `editor_scripts.hpp` | 4 | |
| 4 | `viewport_tools.hpp` | 3 | Ferramentas sobre a Cena |
| 5 | `theme.hpp` | 5 | Trocar por tokens de `ui_tokens.hpp` |
| 6 | `fx.hpp` | 15 | Efeitos visuais |
| 7 | `ui_test.cpp` | 8 | Teste de interface; pode virar teste de modelo |
| 8 | `export_ui.hpp` | 40 | Janela Exportar |
| 9 | `project_dialog.hpp` | 40 | Janela de projeto |
| 10 | `script_window.hpp` | 37 | Janela Script |
| 11 | `assets_panel.hpp` | 139 | Assets (ja tem parte em SpriteUI) |
| 12 | `extra_ui.hpp` | 235 | Mistura de janelas: Input Map, Profiler, Console, Inspetor, Cena, Jogo (dividir por janela) |
| 13 | `main.cpp` | 578 | Laco principal, Configuracoes (ainda com corpo em ImGui), menus |
| 14 | `narrative_panel.hpp` | 54 | Narrative controller (ImNodes) - por ultimo |

## Depois das chamadas
- Remover `imgui_lib` do `CMakeLists.txt` do editor e `third_party/imgui` quando nao houver mais referencias.
- Remover `imnodes` junto com o Narrative.
- Rodar a suite (`ctest`) e o editor com `--frames` para confirmar que nada quebrou.

## Pendencias de janela (hoje em ImGui, sem substituto SpriteUI pronto)
- Configuracoes: a faixa e a aba Graficos ja estao em SpriteUI; as outras abas ainda usam ImGui. A reescrita completa esta em `ui-settings-wip` e nao compila.
- Input Map, Script, Projeto, Profiler, Console, Inspetor, Cena e Jogo: placeholders em ImGui ou janelas sem tela SpriteUI.

## Riscos
- Muitas janelas so podem ser validadas visualmente, e aqui nao ha tela. Cada etapa precisa de teste de modelo quando possivel.
