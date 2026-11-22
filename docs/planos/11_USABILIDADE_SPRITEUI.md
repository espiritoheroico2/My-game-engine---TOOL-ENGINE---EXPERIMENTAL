# Usabilidade do SpriteUI no editor

Análise feita em 2026-11-22 a partir do código, sem captura de tela nem medição de tempo (não há GPU aqui).
Cada item diz o que foi corrigido, o que ficou para depois e o que precisa de medição na sua máquina.

## 1. Corrigido nesta rodada

| Problema relatado | Causa encontrada | Correção |
|---|---|---|
| Renomear e criar cena não fazem nada | `scenes_panel.hpp` trata `SDL_EVENT_TEXT_INPUT`, mas nunca chama `SDL_StartTextInput`. Sem isso o SDL não envia o texto digitado, então o nome nunca entra e a confirmação falha em silêncio. | `draw()` liga `SDL_StartTextInput` quando o campo de nome ganha foco e desliga quando perde. |
| `+` e `-` de recolher/expandir pequenos | O sinal era uma barra de 12×2 px, sem fundo. | Botão redondo de 22 px com sinal de 12×3 px. O cabeçalho inteiro já era clicável. |
| Combos quadrados e outros redondos | O combo recebia raio de 3 px (`buttonRadius`), quase reto. Botão, painel e campo de texto usam o raio cheio. | Combos de Settings com raio de metade da altura (formato de pílula). |
| Sem hover amarelo nos cabeçalhos e combos | O painel de Settings não guarda a posição do ponteiro, então nenhum elemento sabe que está sob o mouse. | Cabeçalho com cor de destaque (`theme.accent`) e combo sob o ponteiro pintado com a mesma cor. |

Teste: `ctest --preset editor` passa (44/44). O hover e a forma dos componentes não foram vistos na tela.

## 2. Ainda não corrigido

### 2.1 Seções de Settings sem conteúdo
- Em SpriteUI, **Lighting, Physics, Game, Optimizing e Language** mostram "Não disponível".
- O código ImGui dessas seções ainda existe em `apps/editor/main.cpp` (`drawLightingTab`, `drawLanguageTab`, `drawGameTab`) e `apps/editor/extra_ui.hpp` (`drawPhysicsTab`). Só que o ImGui desenha apenas a aba ativa (`settingsTab`), e o painel SpriteUI usa seções recolhíveis, então essas seções ficam sem chance de aparecer.
- Tags e Project também saíram da janela.
- **Decisão pendente:** (A) deixar o ImGui desenhar o corpo de cada seção abaixo do cabeçalho SpriteUI, rápido mas mantém o ImGui vivo; ou (B) portar cada seção para controles SpriteUI, mais trabalho, mas remove o ImGui de vez, que é o objetivo do `05_REMOVER_IMGUI.md`.

### 2.2 Tema personalizado com nome
- Já existe `apps/editor/theme.hpp`, com `ThemeFile::save` e `load` em `assets/themes/*.json`. Ele guarda 5 cores base, arredondamento e escala, com um nome.
- Falta: cores por elemento (painel, botão, campo, combo, texto, destaque, hover), editor de cores na janela Appearance e opção "Custom" que aplica e salva.
- Dependência: o tema precisa ser aplicado a todos os elementos, então depende de a troca de tema estar rápida (item 2.4).

### 2.3 Janela Cenas: layout
- Ainda não redesenhada. Os botões de ação e o campo de nome estão empilhados sem hierarquia visual clara. Proposta: barra superior com "Nova cena" e "Salvar", lista no centro, ações da cena selecionada numa faixa fixa embaixo.

### 2.4 Troca de tema lenta ou travando (não medido)
Hipóteses pelo código, a confirmar com o Profiler:
- `layoutSections` roda **duas vezes por quadro** em `SettingsPanel::draw()` (uma para medir o conteúdo, outra para o layout final). Isso dobra o trabalho de layout de cada quadro, mesmo sem mudança.
- `refreshColors()` cria um `sui::Theme{}` novo a cada quadro.
- A troca de tema também refaz as métricas (`applyMetrics`) e o estilo do ImGui de uma vez, e isso pode travar o quadro seguinte.

### 2.5 Demais elementos sem hover
- Checkbox, radio e slider da Settings ainda não mudam de cor sob o mouse.
- Os botões de Cenas já têm `buttonHover`, mas não foram revisados nas outras janelas.

## 3. Como verificar na sua máquina
1. Abrir Scenes, clicar em "Nova cena", digitar um nome e confirmar: a cena deve ser criada.
2. Em Settings, passar o mouse sobre o cabeçalho de uma seção e sobre um combo: ambos devem ficar na cor de destaque.
3. Conferir se os combos estão em pílula e se o `+`/`-` aparece nítido.
4. Trocar o tema três vezes seguidas e anotar se há atraso (Profiler, zona de desenho do editor).
