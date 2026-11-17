# Checklist da Especificação SpriteUI v3

Auditoria somente de documentação. Nenhum código, header ou teste foi alterado.

- Base: especificação v3 (texto), seções 2 a 10 e apêndices.
- Método: leitura de headers em `include/SpriteUi`, trechos de `src/`, `tests/`, `CMakeLists.txt` e `.github/workflows/ci.yml`, com grep. Nada foi compilado nem executado.
- Linhas citadas são da worktree `v4-components` (branch `v4-components`).

Legenda:

| Estado | Quer dizer |
|---|---|
| Pronto | A regra existe no código e não encontrei desvio. |
| Parcial | Uma parte existe. Falta o resto. |
| Falta | Não encontrei no código. |
| Divergente | Existe, mas contraria a regra. |
| Não verificado | Só lendo o código não dá para confirmar. |

Nota de método: a biblioteca usa modelos puros por controle (ex.: `CheckboxControl`, `DropdownControl`). A especificação pede a divisão em dados, comportamento e apresentação com `Behavior` e `OnPointer`. Esse padrão não existe no repositório. Por isso muitas linhas ficam como Parcial.

---

## 1. Tabela por seção

### Seção 2. Modelo do elemento

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 2.1 Handle com índice e geração | Pronto | `include/SpriteUi/Core/Handle.hpp` | Controles ainda referenciam itens por índice (ver 8.14). |
| 2.1 Árvore com pai e filhos; destruir remove a subárvore | Pronto | `include/SpriteUi/Layout/LayoutTree.hpp` (`RemoveSubtree`) | Teste de handle antigo após reúso de índice: não verificado. |
| 2.1 Componentes separados (estilo, layout, visual, interação, texto, valor) | Parcial | `include/SpriteUi/Widgets/Widgets.hpp` | Componentes misturam dados e lógica (`UIToggleComponent::Toggle`, linha 98; `UIWindowComponent::ResolvedStyle`, linha 158). Não há componente de valor separado. |
| 2.2 Min e max aplicados depois do cálculo | Pronto | `src/Layout/Layout.cpp` (`ClampSize`, linhas 25 e 313-314) | - |
| 2.2 Content rect = caixa menos padding | Parcial | `include/SpriteUi/Layout/Layout.hpp` (`Edges padding`, linha 80) | Medição com padding não lida em detalhe. |
| 2.3 Âncora, offset e pivô separados | Pronto | `include/SpriteUi/Core/Transform2D.hpp` (linhas 10-14); pivô só na transformada | Offset invertido gerando tamanho zero: não verificado. Âncora invertida é rejeitada (linha 47). |
| 2.4 Estados independentes (hover, pressed, disabled) | Parcial | `include/SpriteUi/Widgets/Widgets.hpp` (`UIButtonState`, linhas 86-90) | Focus não entra no mesmo modelo de botão. Sem Hold. |
| 2.4 Hold derivado de tempo (0,25 s) | Falta | - | Nenhuma ocorrência de Hold no código. |
| 2.4 Checked, Active e Selected mudam só por operação explícita | Pronto | `include/SpriteUi/Widgets/Controls.hpp` (`Toggle`, `Select`, `Confirm`) | - |
| 2.5 Pintura pela ordem dos irmãos, sem z-index numérico | Parcial | `include/SpriteUi/Input/InputRouter.hpp` (`paintOrder`); `src/Widgets/FloatingWindows.cpp` (linhas 89-95, `zOrder` numérico) | Janelas usam contador numérico. Camada existe em `Rendering/DrawList.hpp` (linha 34). |
| 2.5 Ordem de foco segue a ordem de pintura | Parcial | `src/Input/InputRouter.cpp` (foco por clique, linhas 196-211) | Tab global não encontrado. |
| 2.6 Hit test com transformada inversa | Pronto | `src/Input/InputRouter.cpp` (linhas 36-42) | - |
| 2.6 Recorte do ancestral no hit test | Pronto | `src/Input/InputRouter.cpp` (linhas 35 e 103-104) | - |
| 2.6 Alvo desabilitado não repassa o evento | Divergente | `src/Input/InputRouter.cpp` (linhas 30-31) | Alvo desabilitado é pulado no hit test. O evento vai para o elemento de trás. |
| 2.6 Evento sobe ao ancestral interativo mais próximo | Parcial | `src/Input/InputRouter.cpp` (`PushBubbledEvent`) | Sobe por todos os ancestrais; não para no primeiro interativo. |

### Seção 3. Layout

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 3.1 Duas passagens (medir e arranjar) | Pronto | `include/SpriteUi/Layout/LayoutTree.hpp` (`Build`, `Commit`) | Se `Build` só mede: não verificado. |
| 3.1 Recalcular só o que está sujo | Parcial | `include/SpriteUi/Core/DirtyFlags.hpp`, `Core/RefreshPolicy.hpp` | Cache de layout por subárvore: não verificado. |
| 3.2 Auto, Fixed e Percent | Pronto | `src/Layout/Layout.cpp` (linhas 313-314) | Ramo Auto não lido em detalhe. |
| 3.2 Percent relativo ao content rect do pai | Pronto | `src/Layout/Layout.cpp` (linhas 308-314) | - |
| 3.2 Flex | Pronto | `src/Layout/Layout.cpp` (linhas 144-185) | - |
| 3.3 Flex com congelamento de min e max | Pronto | `src/Layout/Layout.cpp` (laço com `frozen`, linhas 144-185) | Soma de fatores zero: não verificado. |
| 3.4 Nine-slice com kx e ky | Pronto | `src/Rendering/NineSlice.cpp` (linhas 55-61) | - |

### Seção 4. Interação

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 4.1 Ciclo: entrada, hit test, roteamento, transições, tempo do host, layout sujo | Parcial | `src/Input/InputRouter.cpp`; `src/Platform/SDL3InputAdapter.cpp` | Hold ausente. Não há ciclo único que una as etapas. |
| 4.1 Tempo vem de fora (sem relógio do sistema) | Pronto | `src/Widgets/Tooltip.cpp` (linhas 35-53); `Core/RefreshPolicy.hpp` (linha 4) | - |
| 4.2 Click só com Down e Up dentro do mesmo alvo | Pronto | `src/Input/InputRouter.cpp` (captura na linha 196; Click nas linhas 224-241) | Alvo que fica desabilitado no meio do gesto: não verificado. |
| 4.2 Cancel nunca gera Click | Pronto | `src/Input/InputRouter.cpp` (linhas 172-177) | - |
| 4.3 Limite de arraste de 4 px | Parcial | `include/SpriteUi/Widgets/DragDrop.hpp` (linha 50) | `DockInteraction.hpp` (linha 23) usa 6 px. Valores diferentes. |
| 4.3 Hold de 0,25 s | Falta | - | Ver 2.4. |
| 4.3 Latência de tooltip de 0,5 s | Pronto | `include/SpriteUi/Widgets/Tooltip.hpp` (linha 22) | - |
| 4.3 Atraso de submenu de 0,2 s | Falta | - | `Menu.hpp` não tem o atraso. |
| 4.3 Duplo clique de 0,3 s | Falta | - | Não encontrado. |
| 4.3 Constantes configuráveis por controle | Parcial | `Tooltip.hpp` (`TooltipConfig`), `DragDrop.hpp`, `DockInteraction.hpp` | Constantes não estão em um lugar só. |
| 4.4 Tab e Shift+Tab na ordem de foco | Parcial | `src/Widgets/Modal.cpp` (linhas 40-42) | Tab fora de modal não encontrado. |
| 4.4 Enter e Espaço ativam (Activate) | Parcial | `src/Widgets/Modal.cpp` (Enter) | Botão não tem Activate. Espaço não encontrado. |
| 4.4 Escape cancela ou fecha | Parcial | `src/Widgets/Menu.cpp` (linha 94); `src/Widgets/Modal.cpp` (linha 48); `Controls.hpp` (`Close`) | Ligação da tecla fica fora do núcleo. Combo não tem armadilha de foco. |
| 4.4 Setas dentro do controle | Parcial | `Controls.hpp` (`SliderKey`, `ListKey`, `TreeKey`, `DropdownControl`) | Abas sem setas (`TabBarControl` não tem tecla). |
| 4.4 Home, End, PageUp e PageDown | Parcial | `Controls.hpp` (`ListKey`: Up, Down, Home, End; `SliderKey`: sem PageUp) | PageUp e PageDown faltam em lista, slider e árvore. |
| 4.4 Popup prende o foco e volta ao abridor | Parcial | `include/SpriteUi/Widgets/Modal.hpp` (`returnFocusTo`, linha 52) | Modal tem isso. Combo não tem. |
| 4.5 NaN e infinito descartados | Parcial | `include/SpriteUi/Widgets/Widgets.hpp` (linhas 24-30); `src/Widgets/Graphs.cpp` (linha 18); `src/Widgets/ProgressBar.cpp` (linha 29) | Gráfico troca NaN por zero. Progresso descarta sem zerar. |
| 4.5 Valores limitados ao intervalo antes de guardar | Pronto | `Widgets.hpp` (`SetValue`, linhas 24-30); `ProgressBar.cpp` (linha 30) | - |
| 4.5 Texto validado como UTF-8 e limitado em tamanho | Falta | `src/Widgets/TextEdit.cpp` (`InsertText`, linhas 66-71) | Só remove caracteres de controle. Sem validação UTF-8 e sem limite de tamanho. |

### Seção 5. Primitivas de desenho

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| Retângulo sólido | Pronto | `include/SpriteUi/Rendering/Primitives.hpp` (`MakeSolidRect`, linha 20) | - |
| Retângulo arredondado, raio até metade do menor lado | Pronto | `Primitives.hpp` (`MakeRoundedRect`, linha 66) | - |
| Contorno de 1 px | Falta | - | Não encontrado. |
| Linha com espessura e terminação | Parcial | `Primitives.hpp` (`MakeLine`, linha 39) | Sem terminação (plana, redonda, quadrada). |
| Círculo de 3 a 256 segmentos | Falta | - | Não encontrado. Sem divisão por zero possível, porque não existe. |
| Borda suave (cobertura parcial) | Parcial | `include/SpriteUi/Rendering/PanelSdf.hpp` | Cobertura em linha e círculo: não verificado. |
| Recorte lógico convertido pelo backend | Parcial | `Primitives.hpp` (`clipIndex`); `Input/InputRouter.hpp` (`clipRectLogical`) | Conversão para scissor no backend: não verificado. |
| Geometria sem acoplar à API gráfica | Pronto | `include/SpriteUi/Rendering/IRenderBackend.hpp` | - |

### Seção 6. Texto

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 6.1 Clusters (base, marcas, ZWJ, bandeira) | Parcial | `src/Text/LineBreak.cpp` (ZWJ, linha 143) | Bandeira e marcas: não verificado. |
| 6.1 Shaping | Parcial | `src/Text/DebugShaper.cpp`; `Text/GlyphInstance.hpp` | Shaper de produção: não verificado. |
| 6.1 Atlas em alfa | Pronto | `include/SpriteUi/Text/TruetypeAtlas.hpp` | - |
| 6.1 Atlas com distância com sinal (MSDF) | Falta | - | Fica para a fase 7. |
| 6.1 Layout com quebra e altura de linha | Pronto | `src/Text/TextLayout.cpp` (linhas 148-161); `src/Text/LineBreak.cpp` | - |
| 6.1 Caret e clique | Parcial | `src/Text/TextLayout.cpp` (`GetCaretRect`, linha 421); `src/Widgets/TextField.cpp` (linha 37) | Ver 6.4. |
| 6.2 Offsets de byte em fronteira de caractere | Pronto | `include/SpriteUi/Widgets/TextEdit.hpp` (linha 8); `src/Widgets/TextEdit.cpp` (`PrevBoundary`, linhas 47-52) | Fronteira é de codepoint, não de cluster. |
| 6.2 Setas movem por caractere | Parcial | `src/Widgets/TextEdit.cpp` (`MoveLeft`, `MoveRight`) | Movem por codepoint. |
| 6.2 Backspace e Delete removem um cluster | Divergente | `src/Widgets/TextEdit.cpp` (`Backspace`, linhas 80-86) | Remove um codepoint. Marca combinante fica sozinha. |
| 6.3 Quebra obrigatória (LF, CR, NEL, U+2028, U+2029) | Pronto | `src/Text/LineBreak.cpp` (linhas 131-132) | CRLF: não verificado. |
| 6.3 Espaço pendurado não conta na largura | Não verificado | `src/Text/LineBreak.cpp` (linhas 152-155) | Cálculo de largura não lido. |
| 6.3 Hífen | Pronto | `src/Text/LineBreak.cpp` (linhas 163-165) | - |
| 6.3 Espaço inseparável | Pronto | `src/Text/LineBreak.cpp` (linha 86) | - |
| 6.3 CJK | Pronto | `src/Text/LineBreak.cpp` (linhas 55-72, 155) | - |
| 6.3 Emergência (quebra entre clusters) | Não verificado | `src/Text/TextLayout.cpp` | Não lido. |
| 6.4 Caret no pen origin | Não verificado | `src/Text/TextLayout.cpp` (`MakeQuad`, `GetCaretRect`) | Confirmar que o caret usa `penOrigin`, não a caixa do glifo. |
| 6.4 Shift+clique e Shift+setas estendem a seleção | Pronto | `src/Widgets/TextEdit.cpp` (`MoveLeft(bool extend)`) | - |
| 6.4 Digitar com seleção substitui a seleção | Não verificado | `src/Widgets/TextEdit.cpp` (`InsertText`) | Não lido até o fim. |
| 6.4 Desfazer restaura texto, caret e âncora | Não verificado | `src/Widgets/TextEdit.cpp` (`Record`, `Undo`) | Conteúdo do registro não lido. |
| 6.4 Ler ou desenhar não cria desfazer | Não verificado | - | Sem teste encontrado com essa regra. |
| 6.5 Atlas por tamanho de fonte | Não verificado | `src/Text/FontRegistry.cpp`; `TruetypeAtlas.cpp` | Não lido. |

### Seção 7. Divisão em classes

| Regra | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| Camadas de dados, comportamento, apresentação e vínculo | Parcial | `include/SpriteUi/Widgets/Controls.hpp` (cabeçalho: modelos puros) | Dados e comportamento juntos em vários structs. |
| Vínculo: valor só muda por operação explícita | Parcial | `include/SpriteUi/Binding/ValueBinding.hpp` (linhas 20-66) | É detector de mudança, não vínculo com o valor da aplicação. |
| 7.1 Despacho por `std::variant` | Falta | - | Nenhum `variant` no repositório. |
| 7.1 Interface virtual para controles de terceiros | Falta | - | Só existe virtual em `IRenderBackend` (renderização). |
| 7.2 Comportamento do checkbox como no exemplo `ToggleBehavior` | Parcial | `include/SpriteUi/Widgets/Controls.hpp` (`CheckboxControl`, linhas 42-50) | Sem `OnPointer`, sem Enter, Leave, Down, Up e Cancel. |

### Seção 8. Catálogo de elementos

| Elemento | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 8.1 Botão | Parcial | `Widgets.hpp` (`UIButtonState`, linhas 86-90); `Controls.hpp` (`ResolveControlState`) | Sem modelo de botão com Click, Activate e Hold. Recorte de rótulo em Fixed: não verificado. |
| 8.2 Caixa de seleção | Parcial | `Controls.hpp` (`CheckboxControl`, linhas 42-50) | Sem evento ValueChanged. Sem Down, Up e Cancel próprios. |
| 8.2 Botão de rádio e grupo | Falta | - | Não encontrado. |
| 8.3 Campo de linha única | Parcial | `include/SpriteUi/Widgets/TextField.hpp`; `TextEdit.hpp`; `src/Widgets/TextField.cpp` (linhas 29-40) | Modo senha falta. Sem validação UTF-8. Backspace por codepoint. Sem ValueChanged. |
| 8.3 Área de texto | Parcial | `include/SpriteUi/Widgets/TextArea.hpp` (`NeedsLayout`, linha 50; `InsertNewline`) | Barra de rolagem integrada: não verificado. |
| 8.4 Seletor de cor | Divergente | `include/SpriteUi/Widgets/ColorPicker.hpp` (linhas 1-14) | Canais RGB inteiros de 0 a 255. Sem HSV, sem área 2D, sem matiz, sem HEX. Alfa não é de 0 a 1. |
| 8.5 Janela | Parcial | `include/SpriteUi/Widgets/FloatingWindows.hpp`; `Widgets.hpp` (`UIWindowComponent`); `FloatingWindows.cpp` (linhas 109-110, `minSize`; linha 185, `BringToFront`) | Arrasto só pela barra e redimensionamento pelas bordas: `ProcessWindowInteraction` não lido. Clique que traz para frente: não verificado. |
| 8.6 Barra de abas | Parcial | `include/SpriteUi/Widgets/Controls.hpp` (`TabBarControl`); `src/Widgets/Controls.cpp` (linhas 139-151); `LayoutTree.cpp` (`SetVisible`, linhas 79-82) | Abas não estão ligadas a painéis. Preservação de estado ao trocar: não verificado. Setas: falta. |
| 8.7 Painel com recorte e rolagem | Parcial | `include/SpriteUi/Widgets/Widgets.hpp` (`UIScrollViewComponent`) | Recorte pelo content rect e descarte de filhos fora: não verificado. |
| 8.8 Árvore e painel lateral | Divergente | `include/SpriteUi/Widgets/TreeView.hpp`; `src/Widgets/TreeView.cpp` (`SetExpanded` chama `RebuildRows`, linha 29) | Reconstrói a lista inteira a cada expansão. Recolher com a seleção dentro move a seleção (linhas 31-33): correto. |
| 8.9 Barra de rolagem | Divergente | `include/SpriteUi/Widgets/ScrollBar.hpp` (`minThumbLength = 24`, linha 28) | Mínimo do polegar é 24 px. A especificação pede 16 px. Passo de roda de 40 px: não encontrado. |
| 8.10 Barra de progresso | Divergente | `src/Widgets/ProgressBar.cpp` (linha 29) | NaN é descartado e o valor anterior fica. A especificação pede que NaN vire zero. Modo indeterminado: falta. |
| 8.11 Separador | Pronto | `include/SpriteUi/Widgets/Separator.hpp` (linhas 20-30) | Expansão pelo Flex do pai: não verificado. |
| 8.12 Slider | Parcial | `include/SpriteUi/Widgets/Widgets.hpp` (`UISliderComponent`); `Controls.hpp` (`SliderControl`) | PageUp e PageDown faltam. Sem ValueChanged. NaN rejeitado (pronto). Min igual a max é válido (pronto). |
| 8.13 Caixa de combinação (combo) | Parcial | `include/SpriteUi/Widgets/Controls.hpp` (`DropdownControl`) | Abre acima quando não cabe: não encontrado. Sem armadilha de foco. Sem evento Open e Close. |
| 8.14 Lista | Divergente | `include/SpriteUi/Widgets/Controls.hpp` (`selectedIndex`, linha 142) | Seleção guardada por posição (erro listado na spec). Ctrl e Shift faltam. PageUp e PageDown faltam. |
| 8.15 Barra de menus | Parcial | `include/SpriteUi/Widgets/Menu.hpp` (linhas 120-135); `src/Widgets/Menu.cpp` (linha 94) | Atraso de submenu de 0,2 s falta. Alt e atalhos: não verificado. |
| 8.16 Dica de ferramenta (tooltip) | Parcial | `src/Widgets/Tooltip.cpp` (linhas 18-27, 35-53); `Tooltip.hpp` (linha 22) | Sem tratamento de movimento: movimento grande não cancela. "Nunca cobre o alvo" não garantido. |
| 8.17 Arrastar e soltar | Parcial | `src/Widgets/DragDrop.cpp` (linhas 8-42, 49, 55); `DragDrop.hpp` (linha 50) | Threshold, entrega única e aceite estão corretos. Escape ligado ao Cancel: não verificado. Prévia visual: fica fora do núcleo. |
| 8.18 Imagem e botão de imagem | Parcial | `include/SpriteUi/Rendering/Icon.hpp` (linhas 14-25, validação de UV); `Rendering/NineSlice.cpp` | Modos preencher, conter e esticar não encontrados. Marcador de textura ausente não encontrado. Botão de imagem não existe. |
| 8.19 Gráficos (linha e histograma) | Parcial | `include/SpriteUi/Widgets/Graphs.hpp` (anel de tamanho fixo); `src/Widgets/Graphs.cpp` (linha 18) | NaN vira zero e entra no histórico. A spec pede que não entre. Mínimo e máximo por coluna: não verificado. |

### Seção 9. Ordem de construção (estado por fase)

| Fase | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| 1. Base (handles, árvore, estados, eventos, hit test, ordem de pintura) | Parcial | `Core/Handle.hpp`; `Input/InputRouter.cpp` | Hold, Tab global, ancestral interativo e alvo desabilitado. |
| 2. Controles simples (botão, caixa, slider, progresso, separador) | Parcial | `Controls.hpp`; `ProgressBar.cpp`; `Separator.hpp` | Botão sem Click e Activate. Rádio falta. Progresso diverge. Slider sem PageUp. |
| 3. Texto (campo de linha e área, caret, seleção, desfazer) | Parcial | `TextEdit.cpp`; `TextField.cpp`; `TextArea.hpp` | Backspace por codepoint. Sem validação UTF-8. Modo senha falta. |
| 4. Contêineres (painel, rolagem, abas, janela) | Parcial | `Widgets.hpp`; `ScrollBar.hpp`; `FloatingWindows.hpp` | Abas sem painéis. Polegar com mínimo errado. |
| 5. Listas e popups (lista, combo, menu, tooltip, árvore) | Parcial | `Controls.hpp`; `Menu.hpp`; `Tooltip.cpp`; `TreeView.cpp` | Lista por índice. Árvore reconstrói tudo. Tooltip sem cancelamento por movimento. Submenu sem atraso. |
| 6. Avançados (cor, imagem, arrastar e soltar, gráficos) | Parcial | `ColorPicker.hpp`; `Icon.hpp`; `DragDrop.cpp`; `Graphs.cpp` | Cor fora do padrão. Imagem sem modos. Gráfico trata NaN errado. |
| 7. Texto de produção (shaping completo, MSDF, composição) | Falta | - | Sem MSDF. Composição de método de entrada: não verificado. |

### Seção 10. Verificação

| Tipo | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| Transição (comportamento) | Parcial | `tests/test_controls.cpp`, `test_menu.cpp`, `test_modal.cpp`, `test_drag_drop.cpp`, `test_tooltip.cpp` | Não existe teste no padrão de `test_toggle.cpp` (8 casos). Sem teste de Hold. |
| Geometria (layout) | Parcial | `tests/test_layout_patterns.cpp`, `test_flex_wrap.cpp`, `test_nine_slice.cpp`, `test_primitives.cpp`; `tests/Legacy/LegacyLayout.cpp` | Teste diferencial contra o legado: não verificado. Sem teste de círculo. |
| Visual (aparência) | Parcial | `tests/test_visual.cpp` (checagens de pixel); `tests/Visual/VisualHarness.hpp` | Captura PNG por estado e pixels-chave para todos os elementos: não completo. |
| Propriedade e fuzz | Parcial | `tests/test_robustness.cpp`, `test_text_layout.cpp`, `test_text_breaking.cpp`, `test_flex_wrap.cpp`, `test_nine_slice.cpp`; `tests/fuzz_config.cpp` | Só o fuzz de configuração usa libFuzzer. Nenhum fuzz de UTF-8 de texto. Semente fixa em todos: não verificado. |
| Sanitizers (ASan e UBSan em toda a suíte) | Parcial | `.github/workflows/ci.yml` (linhas 58-74) | Roda só no CI, em GCC. Ver seção 5 do documento. |

### Apêndice A. Checklist para revisar um elemento

| Item | Estado | Arquivo(s) | O que falta |
|---|---|---|---|
| Partes e ordem de pintura listadas | Falta | - | Não existe documento por elemento. |
| Dados, comportamento e apresentação separados | Parcial | `Controls.hpp` | Ver seção 7. |
| Estados e eventos listados; Click segue 4.2 | Parcial | `InputRouter.cpp` | Click segue 4.2. Eventos de elementos faltam. |
| Ordem de foco e teclas (4.4) cobertas | Parcial | `Modal.cpp`; `Controls.hpp` | Ver 4.4. |
| Parâmetros com intervalo e padrão, NaN e infinito | Parcial | `Widgets.hpp`; `Tooltip.hpp`; `DragDrop.hpp` | Ver 4.5 e 8.10. |
| Texto validado como UTF-8 e limitado | Falta | `TextEdit.cpp` | Ver 4.5. |
| Geometria conforme 2 e 3 | Parcial | `Layout.cpp`; `Transform2D.hpp` | Ver seções 2 e 3. |
| Erros a evitar revisados | Parcial | - | Esta auditoria revisou os erros. Não há lista por elemento. |
| Critérios de aceite com teste | Parcial | `tests/` | Ver seção 10. |

### Apêndice B. Glossário

Não é código. Não se aplica a esta auditoria.

---

## 2. Alertas explícitos

Cada item marcado como erro a evitar ou como nota de atenção na especificação. Estados: Atende, Viola, Falta, Parcial, Não verificado.

| Alerta | Estado | Arquivo e linha | Nota |
|---|---|---|---|
| `[[nodiscard]]` em `OnPointer` | Falta | - | `OnPointer` não existe. Os métodos de `Controls.hpp` (`Toggle`, `Select`, `Key`, `Confirm`, `Open`, `MoveDown`, `MoveUp`) já têm `[[nodiscard]]` (linhas 47, 65, 68, 71, 95, 101, 104, 105, 108). |
| Hold derivado de tempo, não evento | Falta | - | Nenhum Hold no código. |
| Click só com Down e Up dentro | Atende | `src/Input/InputRouter.cpp` linhas 196 e 224-241 | Click usa o alvo capturado no Down e exige Up dentro. |
| Cancel sem Click | Atende | `src/Input/InputRouter.cpp` linhas 172-177 | Cancel limpa a captura e não emite Click. |
| Ponteiros brutos proibidos | Parcial | `Core/Handle.hpp`; `Controls.hpp` linhas 89 e 142 | Layout e input usam handle. Controles guardam índice `size_t`. Ponteiros de plataforma (`SDL_Window*`) ficam em classes de plataforma. |
| NaN e infinito descartados | Parcial | Atende: `Widgets.hpp` linhas 24-30. Viola: `ProgressBar.cpp` linha 29 (descarta em vez de zerar) e `Graphs.cpp` linha 18 (troca por zero e entra no histórico). | Ver 4.5. |
| Texto UTF-8 validado e limitado | Viola | `src/Widgets/TextEdit.cpp` linhas 66-71 | Só filtra controles. Sem validação e sem limite. |
| Percent relativo ao content rect do pai | Atende | `src/Layout/Layout.cpp` linhas 308-314 | - |
| Flex com congelamento | Atende | `src/Layout/Layout.cpp` linhas 144-185 | Divisão por soma zero: não verificado. |
| Nine-slice com kx e ky | Atende | `src/Rendering/NineSlice.cpp` linhas 55-61 | - |
| Caret em pen origin | Não verificado | `src/Text/TextLayout.cpp` linha 421 (`GetCaretRect`) | Não confirmado que usa `penOrigin`. |
| Caret em offset de byte | Atende | `src/Widgets/TextEdit.cpp` linhas 47-52 | Offsets são de byte e caem em fronteira de codepoint. |
| Backspace por cluster | Viola | `src/Widgets/TextEdit.cpp` linhas 80-86 | Usa `PrevBoundary` por codepoint. |
| Tooltip sem relógio do sistema | Atende | `src/Widgets/Tooltip.cpp` linhas 35-53; `Tooltip.hpp` linha 22 | Tempo entra como parâmetro. Movimento não cancela (ver 8.16). |
| Despacho por `variant`, sem virtual nas embutidas | Falta | - | Não existe `variant`. Não há virtual nos controles embutidos, mas o despacho previsto não existe. |

Outros erros a evitar que aparecem no código:

| Erro (spec) | Estado | Arquivo e linha |
|---|---|---|
| Guardar a seleção da lista por posição | Viola | `Controls.hpp` linha 142 |
| Recalcular a árvore inteira ao expandir | Viola | `TreeView.cpp` linha 29 |
| Polegar sem descontar o próprio tamanho | Atende | `ScrollBar.cpp` linha 79 |
| Recolher nó que contém a seleção | Atende | `TreeView.cpp` linhas 31-33 |
| Valor de progresso acima de 1 | Atende | `ProgressBar.cpp` linha 30 (clamp) |
| Alvo desabilitado repassa evento | Viola | `InputRouter.cpp` linhas 30-31 |
| Rádio com booleano próprio | Não verificado | Rádio não existe. |
| Mostrar o texto real no modo senha | Não verificado | Modo senha não existe. |

---

## 3. O que pode melhorar e remodelar

### Princípios que valem para todos

1. Separar dados, comportamento e apresentação. Hoje `Toggle()` e `ClampScroll()` ficam dentro de structs de dados. Mudança mínima: manter o struct só com campos e mover a lógica para uma função livre que recebe o struct.
2. Handles com geração no lugar de índices soltos. Controles com lista ou árvore guardam `size_t`. Mudança mínima: guardar o identificador do item (`UIHandle` ou id estável) e resolver a posição só na hora de desenhar.
3. Estados independentes. `ResolveControlState` junta hover, pressed e disabled em um enum só para desenhar. Isso está certo para desenho, mas o comportamento precisa ler os booleanos. Mudança mínima: nenhuma no modelo. Acrescentar Focused e Hold como booleanos próprios.
4. Hold e tempo vindos do host. Tooltip e refresh já recebem tempo de fora. Falta Hold. Mudança mínima: função `IsHeld(state, now, threshold)` pura, chamada a cada quadro.
5. Despacho com `variant`. Hoje não há despacho. Mudança mínima: depois dos controles simples, um `std::variant` em um único arquivo de despacho. Controles de terceiros ficam de fora.

### Por componente

**Botão** (`Widgets.hpp` 86-90; `Controls.hpp` 39)
- Falta: modelo com Click, Activate e Hold; contorno de foco; disabled sem evento.
- Mínimo: `ButtonControl` com `OnPointer` que devolve `clicked`, `activated` e `holdStarted`.

**Caixa de seleção** (`Controls.hpp` 42-50)
- Falta: Down, Up e Cancel próprios; evento ValueChanged; grupo de rádio.
- Mínimo: `CheckboxControl::OnPointer` com a tabela da seção 7.2 e teste de 8 casos.

**Slider** (`Widgets.hpp`, `UISliderComponent`; `Controls.hpp`, `SliderControl`)
- Falta: PageUp e PageDown (dez passos); evento ValueChanged durante o arraste.
- Mínimo: acrescentar `PageUp` e `PageDown` ao enum `SliderKey`.

**Combo** (`Controls.hpp`, `DropdownControl`)
- Falta: abrir acima quando não cabe; armadilha de foco; evento Open e Close.
- Mínimo: calcular posição com a janela; guardar `highlightedIndex` como id.

**Abas** (`Controls.hpp` 139-151, `TabBarControl`)
- Falta: ligação com painéis; setas; estado preservado ao trocar.
- Mínimo: `TabBarControl` guarda ids de painel. Painéis ocultos chamam `LayoutTree::SetVisible(false)`.

**Lista** (`Controls.hpp` 142, `ListViewControl`)
- Divergente: seleção por índice.
- Falta: Ctrl, Shift, PageUp, PageDown.
- Mínimo: trocar `selectedIndex` por um conjunto de ids e âncora por id.

**Campo de texto** (`TextField.hpp`, `TextEdit.hpp`)
- Divergente: Backspace por codepoint.
- Falta: validação UTF-8 e limite; modo senha.
- Mínimo: usar clusters em `PrevBoundary`; validar em `InsertText` antes de gravar.

**Rolagem** (`ScrollBar.hpp` 28)
- Divergente: mínimo do polegar de 24 px.
- Falta: passo de roda de 40 px (não encontrado).
- Mínimo: trocar a constante para 16 e tornar o passo configurável.

**Popup** (`Popup.hpp`, `Modal.hpp`)
- Modal já prende foco e devolve foco. Falta: a mesma regra para combo e menu; posicionamento que verifica a borda da janela.
- Mínimo: uma função de posicionamento compartilhada.

**Dock** (`Docking.hpp`, `DockInteraction.hpp`, `DockOps.hpp`)
- A especificação v3 não define dock. Nada a comparar.
- Divergente interno: limiar de arraste de 6 px contra 4 px no restante. Mínimo: usar a constante única de 4.2.

**Janelas flutuantes** (`FloatingWindows.hpp`)
- Usa contador numérico (`zOrder`). A spec pede reordenar irmãos.
- Mínimo: guardar a lista de janelas em ordem; trazer para frente move o item para o fim.

**Política de atualização** (`Core/RefreshPolicy.hpp`)
- Pronto para tempo injetado e taxa zero. Falta: ligação direta com as partes sujas do layout.
- Mínimo: nenhuma mudança obrigatória. Confirmar que o layout pula quando não há sujeira.

**Controles novos**
- Rádio: falta. Mínimo: `RadioGroupControl` que guarda o id escolhido.
- Botão de imagem: falta. Mínimo: `ButtonControl` com `Icon` como apresentação.
- Seletor de cor: diverge (RGB inteiro). Mínimo: estado em HSV com alfa de 0 a 1 e conversão só na exibição.
- Imagem: falta modos e marcador de textura ausente. Mínimo: função de ajuste (preencher, conter, esticar) e marcador de erro.
- Gráfico: NaN deve ser ignorado na escala, não zerado. Mínimo: pular a amostra em `AddSample`.
- Círculo e contorno: faltam nas primitivas. Mínimo: função de círculo com segmentos entre 3 e 256.

---

## 4. Plano em microtarefas

Ordem da seção 9 da especificação. Cada tarefa tem arquivo alvo, o teste que prova e o critério de aceite. Tarefas de testes novos entram no `CMakeLists.txt` junto com o arquivo.

### Fase 1. Base

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 1 | Acrescentar Hold derivado de tempo | `include/SpriteUi/Widgets/Widgets.hpp` (`UIButtonState`) | `tests/test_controls.cpp`: pressed em t=0, Hold falso em 0,249 s, verdadeiro em 0,25 s | Hold só depende de `now` e do limiar. Nenhum evento Hold. |
| 2 | Limiar de Hold configurável | `Widgets.hpp` | Mesmo teste com limiar 0,1 s | O limiar muda o resultado. |
| 3 | Alvo desabilitado bloqueia o evento | `src/Input/InputRouter.cpp` (linhas 30-31) | Novo `tests/test_input_routing.cpp`: clique em alvo desabilitado não chega ao de trás | Nenhum evento chega ao elemento de trás. |
| 4 | Subir ao ancestral interativo mais próximo | `src/Input/InputRouter.cpp` (`PushBubbledEvent`) | `test_input_routing.cpp`: filho não interativo passa o clique ao pai interativo | Para no primeiro ancestral interativo. |
| 5 | Ordem de foco com Tab | `src/Input/SpatialNavigation.cpp`; `InputRouter.cpp` | `test_input_routing.cpp`: Tab segue a ordem de pintura | Tab e Shift+Tab percorrem focáveis na ordem certa. |
| 6 | Transições do checkbox (8 casos) | `include/SpriteUi/Widgets/Controls.hpp`; novo `tests/test_checkbox_transitions.cpp` | Os 8 casos: clique, arraste para fora, soltura fora, cancelamento, desabilitado, hover, Click sem Down, Up sem Down | Todos passam com ASan e UBSan. |
| 7 | `OnPointer` com `[[nodiscard]]` | `Controls.hpp` (`CheckboxControl`) | Teste de compilação com `-Werror`: ignorar o resultado falha | Quem ignora usa `(void)` de forma explícita. |
| 8 | Limiar de arraste único de 4 px | `DockInteraction.hpp` (linha 23); `DragDrop.hpp` (linha 50) | `test_dock_interaction.cpp`: arraste começa em 4 px | Uma constante só, igual à da seção 4.3. |

### Fase 2. Controles simples

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 9 | Botão com Click, Activate e Hold | `Controls.hpp` (novo `ButtonControl`) | Novo `tests/test_button.cpp`: Enter e Espaço com foco geram Activate; Click exige Down e Up | Seção 8.1 passa. |
| 10 | Slider com PageUp e PageDown | `Controls.hpp` (`SliderKey`, `Key`) | `test_controls.cpp`: PageUp soma 10 passos e para no máximo | Seção 8.12 passa. |
| 11 | Progresso: NaN vira zero | `src/Widgets/ProgressBar.cpp` (linha 29) | `test_progress_separator.cpp`: NaN vira 0; infinito vira o limite | Seção 8.10 passa. |
| 12 | Grupo de rádio com id | `Controls.hpp` (novo `RadioGroupControl`) | `test_controls.cpp`: escolher desmarca a anterior; desabilitado não muda | Seção 8.2 passa. |
| 13 | Despacho por `std::variant` | Novo `include/SpriteUi/Widgets/BehaviorSet.hpp` | Teste de compilação e sequência de eventos em cada controle | Nenhum `virtual` nos controles embutidos. |

### Fase 3. Texto

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 14 | Validar UTF-8 e limitar tamanho na entrada | `src/Widgets/TextEdit.cpp` (`InsertText`, linhas 66-71); `TextArea.cpp` | `tests/test_text_edit.cpp`: bytes inválidos rejeitados; texto acima do limite cortado; fuzz com semente fixa | Seção 4.5 passa. |
| 15 | Backspace e Delete por cluster | `TextEdit.cpp` (`PrevBoundary`, `Backspace`, linhas 47-52 e 80-86) | `test_text_edit.cpp`: "e" seguido de U+0301 some junto | Seção 6.2 passa. |
| 16 | Modo senha | `include/SpriteUi/Widgets/TextField.hpp`; `src/Widgets/TextField.cpp` | `test_text_field.cpp`: itens de desenho não contêm o texto real | Seção 8.3 passa. |
| 17 | Caret no pen origin | `src/Widgets/TextField.cpp` (linha 37); `src/Text/TextLayout.cpp` (`GetCaretRect`, linha 421) | `test_text_layout.cpp`: caret igual ao `penOrigin` em glifo com bearing | Seção 6.4 passa. |
| 18 | Desfazer restaura caret e âncora | `TextEdit.cpp` (`Record`, `Undo`) | `test_text_edit.cpp`: desfazer devolve texto, caret e âncora | Seção 6.4 passa. |

### Fase 4. Contêineres

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 19 | Abas ligadas a painéis com estado preservado | `Controls.hpp` (`TabBarControl`); `Layout/LayoutTree.hpp` (`SetVisible`) | `test_controls.cpp`: digitar, trocar de aba e voltar mantém o texto | Seção 8.6 passa. |
| 20 | Filho fora do painel não desenha nem recebe clique | `src/Widgets/Widgets.hpp` (`UIScrollViewComponent`); `DrawList` | `test_scroll_bar.cpp`: filho totalmente fora não gera item nem alvo | Seção 8.7 passa. |
| 21 | Polegar com mínimo de 16 px | `include/SpriteUi/Widgets/ScrollBar.hpp` (linha 28) | `test_scroll_bar.cpp`: conteúdo grande gera polegar de 16 px | Seção 8.9 passa. |

### Fase 5. Listas e popups

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 22 | Seleção de lista por id | `Controls.hpp` (`ListViewControl`, linha 142) | `test_controls.cpp`: inserir item antes preserva a seleção | Seção 8.14 passa. |
| 23 | Ctrl, Shift, PageUp e PageDown na lista | `Controls.hpp` (`ListViewControl`, `ListKey`) | `test_controls.cpp`: Shift+clique seleciona o intervalo exato | Seção 8.14 passa. |
| 24 | Árvore sem reconstruir tudo | `src/Widgets/TreeView.cpp` (`RebuildRows`, linha 29) | `test_tree_view.cpp`: expandir em árvore de 10 mil nós toca só os filhos | Seção 8.8 passa. |
| 25 | Tooltip cancela com movimento grande | `src/Widgets/Tooltip.cpp`; `Tooltip.hpp` | `test_tooltip.cpp`: movimento acima do limite cancela; sem movimento não cancela | Seção 8.16 passa. |
| 26 | Atraso de submenu de 0,2 s | `include/SpriteUi/Widgets/Menu.hpp` | `test_menu.cpp`: submenu abre só depois de 0,2 s | Seção 4.3 passa. |
| 27 | Combo abre acima quando não cabe | `Controls.hpp` (`DropdownControl`) | `test_controls.cpp`: janela pequena deixa o popup inteiro visível | Seção 8.13 passa. |

### Fase 6. Avançados

| # | Tarefa | Arquivo alvo | Teste que prova | Critério de aceite |
|---|---|---|---|---|
| 28 | Seletor de cor em HSV com HEX | `include/SpriteUi/Widgets/ColorPicker.hpp`; `src/Widgets/ColorPicker.cpp` | `test_color_picker.cpp`: RGB para HSV e de volta com erro menor que 1 unidade; HEX inválido rejeitado | Seção 8.4 passa. |
| 29 | Imagem com marcador de textura ausente e modos de ajuste | `include/SpriteUi/Rendering/Icon.hpp`; `NineSlice.cpp` | `test_visual.cpp`: pixel do marcador aparece sem textura | Seção 8.18 passa. |
| 30 | Gráfico ignora NaN na escala | `src/Widgets/Graphs.cpp` (linha 18) | `test_graph_sampling.cpp`: NaN não vira pico nem entra na escala | Seção 8.19 passa. |
| 31 | Círculo e contorno de 1 px | `include/SpriteUi/Rendering/Primitives.hpp` | `test_primitives.cpp`: 3 e 256 segmentos; raio zero não divide por zero | Seção 5 passa. |

### Fase 7. Texto de produção

Não iniciar. Depende de shaper completo e atlas MSDF. Fica fora desta versão.

---

## 5. Verificação

### O que existe

- Testes de transição: `tests/test_controls.cpp` (muitas checagens), `test_menu.cpp`, `test_modal.cpp`, `test_drag_drop.cpp`, `test_tooltip.cpp`, `test_tree_view.cpp`, `test_text_edit.cpp`, `test_text_field.cpp` e `test_main.cpp` (com casos de Click e do roteador).
- Testes de geometria: `test_layout_patterns.cpp`, `test_flex_wrap.cpp`, `test_nine_slice.cpp`, `test_primitives.cpp`. Existe `tests/Legacy/LegacyLayout.cpp`, que pode servir de referência para teste diferencial. Uso como diferencial: não verificado.
- Testes visuais: `tests/test_visual.cpp` checa pixels. `tests/Visual/VisualHarness.hpp` e `ComponentsShowcase.cpp` geram imagens.
- Propriedade e fuzz: `test_robustness.cpp`, `test_text_layout.cpp`, `test_text_breaking.cpp`, `test_flex_wrap.cpp`, `test_nine_slice.cpp` usam entradas geradas. Semente fixa em todos: não verificado.
- Fuzz com libFuzzer: só `tests/fuzz_config.cpp`. Exige Clang e a opção `SPRITEUI_BUILD_FUZZERS`, que vem desligada (`CMakeLists.txt`, linhas 134-148).

### O que falta

- Teste no padrão de `test_toggle.cpp` (8 casos de transição).
- Teste de Hold, Tab global, ancestral interativo e alvo desabilitado.
- Fuzz de UTF-8 para entrada de texto.
- Teste de círculo, contorno e rádio.
- Teste de tempo de submenu e duplo clique.
- Teste de posicionamento do combo quando não cabe.

### Sanitizers

Resposta direta: sim, mas só no CI, e não executei nada nesta auditoria.

- `.github/workflows/ci.yml` (linhas 58-74): job "GCC ASan + UBSan" configura `-fsanitize=address,undefined -fno-sanitize-recover=all` e roda `ctest`. Esse job usa Debug e desliga os exemplos.
- Os testes SDL3 (`SpriteUI.SDL3Input`, `SDL3Clipboard`) só entram se `SPRITEUI_BUILD_SDL3_ADAPTER` estiver ligado. O job não liga essa opção, então eles não rodam com sanitizers.
- O visualizador e o benchmark não entram no job de sanitizers.
- Se a suíte passa de fato com sanitizers localmente: não verificado.
