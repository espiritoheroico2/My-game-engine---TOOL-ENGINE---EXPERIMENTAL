# Remocao do Dear ImGui: registro final

Data: 2026-11-24. Aprovacao visual: dada pelo usuario (pedido de remocao completa).
Branch de origem: `remocao-imgui` (commit 4d3d949, ja na `main`). Limpeza seguinte: branch `imgui-limpeza`.

## 1. O que foi removido

- Dear ImGui e imnodes: nenhum include, nenhuma chamada, nenhum target no CMake.
  `third_party/imgui` e `third_party/imnodes` foram apagados.
- Janelas ImGui antigas (Hierarchy, Inspector, Scene, Game, Assets, Scenes, Console, Profiler, Settings,
  Input Map, Export game, Help, Test map, Narrative, Coding, Editor info, tela inicial).
- Codigo legado: caminho de fallback sem SpriteUI (`#else` de `TOOL_WITH_SPRITEUI`), dialogo ImGui de projeto
  (`--imgui-start`), `project_dialog.hpp`, flags de janela do ImGui e helpers de desenho ImGui em `fx.hpp`,
  `extra_ui.hpp`, `viewport_tools.hpp` e `theme.hpp`.
- Opcao `TOOL_WITH_SPRITEUI` continua no CMake, mas so para baixar a dependencia SpriteUI (o editor a exige).

## 2. Verificacao

| Verificacao | Resultado |
|---|---|
| Build `tool_editor` (preset editor) | OK |
| `ctest --preset editor` | 47/47 |
| `ToolEditor --selftest` | 250 checks, 0 falhas |
| Captura headless: janela principal | OK |
| Captura: Input Map (`--show-inputmap`) | OK |
| Captura: Export game (`--show-export`) | OK |
| Captura: Help (`--help-topic objects`) | OK, acima das outras janelas (corrigido: estava sob Narrative) |
| Captura: Test map (`--test-map`) | OK, acima das outras janelas (corrigido) |
| Captura: Script (`--select Cube --edit-script`) | OK |
| Captura: Narrative (`--narrative`) | OK |
| Captura: Settings (`--tab 2`) | OK (a aba nao muda com `--tab`; fora do escopo) |
| Escala da interface (UI scale) | Teste unitario de `ui_style` (`ui_editor`) cobre a conta. Captura com `interfaceScale` nao e possivel: o carregamento de `editor_settings.json` e pulado no modo `--screenshot` |

Achado sem relacao com ImGui (nao corrigido): `--edit-script` sem objeto selecionado derruba o editor com
`World::add: entidade invalida`. Com `--select <objeto>` funciona.

## 3. Recursos da versao ImGui sem equivalente na SpriteUI

| Recurso (versao ImGui) | Situacao na SpriteUI | Observacao |
|---|---|---|
| Editar as 5 cores-base do tema (fundo, superficie, texto, destaque, borda) | **Reintroduzido** | Paleta "Custom" (Appearance > Base colors) edita as 5 cores e grava em `editor_theme.json`. Captura confirma os botoes; a troca de cor por clique nao foi testada por interacao |
| Lista "Advanced colors (all)": ajuste de cada cor do ImGui | **Reintroduzido, parcial** | Secao "Advanced colors" na paleta Custom: escolhe o token (< >) e ajusta R/G/B. Cobre os 8 tokens derivados; nao cobre todas as cores internas do ImGui |
| Lista "User themes" (arquivos `assets/themes/*.json` escolhidos na aba) | **Reintroduzido** | Lista aparece na aba (captura mostra `meu-tema`) e aplica o arquivo via `setThemeFile`. Aplicacao por clique nao testada por interacao |
| Renomear tag com duplo clique (Inspector) | **Implementado, nao testado** | Duplo clique abre edicao do nome (Enter confirma, Esc cancela). Compila; sem teste de evento automatizado |
| Layout das janelas do ImGui (`editor_layout.ini`) | **Mudou, sem migracao** | Layout salvo em `editor_dock_layout.txt`. `editor_layout.ini` nao e mais lido e nao e convertido (pendente, ver secao 4) |
| Fonte da interface (`Xmind-Regular.ttf`) | **Corrigido** | A fonte agora vai embutida no binario (`embedded_font.hpp`, gerado por `tools/embed_font.py`). Sem a pasta de fontes, o editor abre normalmente (verificado copiando o binario sem `fonts/`) |
| Desenho dos colisores na Scene | Portado | `view_overlays.hpp` (opcao "Draw colliders") |
| Atalhos Ctrl+S, Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z | Portados | `main.cpp` (bloco de atalhos globais) |
| Arrastar na Hierarquia para reparentar | Portado | `hierarchy_panel.hpp` (acao Reparent) |
| Copiar codigo de erro de script ("Copy code") | Portado | `coding_panel.hpp` e `script_window.hpp` |

Conferir na uso real (nao verificado por captura): edicao de nos do Narrative (zoom, selecao em caixa,
minimapa) e o "Dock com Shift". Se algum deles nao funcionar como antes, registrar aqui.

## 4. Pendencias conhecidas

- Verificar por interacao (nao coberto por captura): clicar nas cores-base e nos tokens, arrastar R/G/B,
  trocar de tema pela lista de user themes e renomear tag com duplo clique.
- Migracao do `editor_layout.ini` antigo para `editor_dock_layout.txt`: nao implementada. Hoje o layout antigo
  e ignorado.
- Rotulo "Background" truncado para "Backgr..." nos botoes de cor-base (largura da linha); encurtar.
- Interface: o slider "UI scale" (Settings > Appearance) aumenta a escala da SpriteUI
  (`ui_style::interfaceFactor`, padrao 125% = fator 1).
- `--edit-script` sem selecao: corrigir a falha (`World::add: entidade invalida`).
- `ui_contract.hpp` ainda tem referencia obsoleta a `project_dialog`.
