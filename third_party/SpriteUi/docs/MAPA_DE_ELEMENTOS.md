# Mapa de elementos da SpriteUI

Cada linha liga um elemento ao seu header, à implementação, ao teste e à impressão da captura.
Caminhos de header e implementação são relativos à raiz do repositório. As impressões ficam em
`scratchpad/progress/` (fora do repositório).

Status:
- **pronto**: header, implementação, teste na suíte e impressão (quando visual) existem e passam.
- **parcial**: funciona, mas falta algo listado na coluna "Pendências".
- **faltando**: não implementado.

Elementos de base (`Element`, `ElementTree`, `PressGesture`, `PointerEvent`, `Paint`) são infraestrutura
e não têm linha própria de comportamento; estão na última tabela.

## Elementos desta entrega (spec 8.x)

| Elemento | Header | Implementação | O que faz | Status | Teste (ctest) | Impressão | Pendências |
|---|---|---|---|---|---|---|---|
| RadioGroup e Toggle | `include/SpriteUi/Elements/RadioButton.hpp`, `Toggle.hpp` | `src/Elements/RadioButton.cpp`, `Toggle.cpp` | Grupo de opções com uma marcada; chave liga e desliga. | pronto | SpriteUI.RadioToggle | elem-01-radio-toggle.png | - |
| ListBox | `include/SpriteUi/Elements/ListBox.hpp` | `src/Elements/ListBox.cpp` | Lista com seleção simples e múltipla, teclado e rolagem; sem limite de itens. | pronto | SpriteUI.ListBox | elem-02-lista.png | - |
| Dropdown | `include/SpriteUi/Elements/Dropdown.hpp` | `src/Elements/Dropdown.cpp` | Botão que abre uma lista abaixo, ou acima se não houver espaço; fecha com Esc ou clique fora. | pronto | SpriteUI.Dropdown | elem-03-dropdown.png, elem-03b-dropdown-acima.png | - |
| TabBar | `include/SpriteUi/Elements/TabBar.hpp` | `src/Elements/TabBar.cpp` | Faixa de abas que cresce com o número de abas, com uma ativa, painel ativo só, setas trocam a aba e rolagem da faixa. | pronto | SpriteUI.TabBar | elem-04-abas.png, elem-04b-abas-rolagem.png | Painéis ocultos ficam congelados por disciplina do chamador: não chamar `Arrange` na subárvore da barra (documentado no header). |
| Window e Panel | `include/SpriteUi/Elements/Window.hpp`, `Panel.hpp` | `src/Elements/Window.cpp`, `Panel.cpp` | Janela com barra de título e botões; arrasta só pela barra; redimensiona pelas bordas. Painel com recorte e rolagem. | parcial | SpriteUI.WindowPanel | elem-05-janela-painel.png | Janela não limita ao canvas; redimensionamento só tem tamanho mínimo. |
| MenuBar | `include/SpriteUi/Elements/MenuBar.hpp`, `include/SpriteUi/Widgets/Menu.hpp` | `src/Elements/MenuBar.cpp`, `Widgets/Menu.cpp` (modelo) | Barra de menus com cascata, atalhos alinhados à direita, Alt ativa, Esc fecha um nível. | parcial | SpriteUI.MenuBar, SpriteUI.Menu | elem-06-barra-de-menus.png | `FindShortcut` só devolve o caminho; quem executa a ação é o chamador. |
| Image e ImageButton | `include/SpriteUi/Elements/Image.hpp` | `src/Elements/Image.cpp` | Imagem com UV limitado, modos esticar, conter e preencher, marcador magenta com X se a textura faltar; botão de imagem com estados. | pronto | SpriteUI.Image | elem-07-imagem.png | Nine-slice só coberto por teste (9 regiões), sem impressão. |
| Graph (linha e histograma) | `include/SpriteUi/Elements/Graph.hpp` | `src/Elements/Graph.cpp` | Gráfico de linha ou histograma sobre anel de tamanho fixo; NaN fora da escala é pulado; custo pela largura. | pronto | SpriteUI.Graph | elem-08-grafico.png | Rótulos de valor são desenhados pelo chamador (áreas nas margens são fornecidas). |
| Moldura de janela (WindowFrame) | `include/SpriteUi/Widgets/WindowFrame.hpp`, `WindowFramePaint.hpp`, `WindowFrameTheme.hpp` | `src/Widgets/WindowFrame.cpp`, `WindowFramePaint.cpp`, `WindowFrameTheme.cpp` | Moldura com barra, botões, aba, raio, rolagem e itens; tema único ligado aos presets. | pronto | SpriteUI.WindowFrame | - | Sem impressão própria. |

## Elementos anteriores

| Elemento | Header | Implementação | O que faz | Status | Teste (ctest) | Impressão | Pendências |
|---|---|---|---|---|---|---|---|
| Label | `include/SpriteUi/Elements/Label.hpp` | `src/Elements/Label.cpp` | Texto com alinhamento e quebra. | pronto | SpriteUI.Elements | elements-1-rotulos.png | - |
| Button | `include/SpriteUi/Elements/Button.hpp` | `src/Elements/Button.cpp` | Botão com estados normal, foco, pressionado e desabilitado. | pronto | SpriteUI.Elements | elements-2-botoes.png | - |
| Checkbox e Slider | `include/SpriteUi/Elements/Checkbox.hpp`, `Slider.hpp` | `src/Elements/Checkbox.cpp`, `Slider.cpp` | Caixa de marcação e deslizante. | pronto | SpriteUI.Controls | elements-6-caixa-e-deslizante.png | - |
| TextField | `include/SpriteUi/Elements/TextField.hpp` | `src/Elements/TextField.cpp` | Campo de uma linha com seleção, caret e rolagem. | pronto | SpriteUI.TextField | elements-3 a elements-5 | - |
| TextArea | `include/SpriteUi/Widgets/TextArea.hpp` | `src/Widgets/TextArea.cpp` | Área multilinha com rolagem. | pronto | SpriteUI.TextArea | texto-03, texto-04 | - |
| TextEdit | `include/SpriteUi/Widgets/TextEdit.hpp` | `src/Widgets/TextEdit.cpp` | Edição de texto com desfazer. | pronto | SpriteUI.TextEdit | - | Sem impressão própria. |
| Controls (modelos) | `include/SpriteUi/Widgets/Controls.hpp` | `src/Widgets/Controls.cpp` | Modelos de Dropdown, TabBar e ListView. | pronto | SpriteUI.Controls | - | - |
| Modal | `include/SpriteUi/Widgets/Modal.hpp` | `src/Widgets/Modal.cpp` | Diálogo com bloqueio do fundo e foco preso. | pronto | SpriteUI.Modal | components-5-modal-cor.png | - |
| Tooltip | `include/SpriteUi/Widgets/Tooltip.hpp` | `src/Widgets/Tooltip.cpp` | Dica com atraso. | pronto | SpriteUI.Tooltip | components-4-menu-dica.png | - |
| TreeView | `include/SpriteUi/Widgets/TreeView.hpp` | `src/Widgets/TreeView.cpp` | Árvore expansível sem limite de nós. | pronto | SpriteUI.TreeView | components-3-tabela-arvore-rolagem.png | - |
| Table | `include/SpriteUi/Widgets/Table.hpp` | `src/Widgets/Table.cpp` | Tabela com rolagem. | pronto | SpriteUI.Table | components-3-tabela-arvore-rolagem.png | - |
| ScrollBar | `include/SpriteUi/Widgets/ScrollBar.hpp` | `src/Widgets/ScrollBar.cpp` | Barra de rolagem. | pronto | SpriteUI.ScrollBar | - | - |
| ProgressBar e Separator | `include/SpriteUi/Widgets/ProgressBar.hpp`, `Separator.hpp` | `src/Widgets/ProgressBar.cpp`, `Separator.cpp` | Progresso e separador. | pronto | SpriteUI.ProgressSeparator | components-1-progresso-grade.png | - |
| ColorPicker | `include/SpriteUi/Widgets/ColorPicker.hpp` | `src/Widgets/ColorPicker.cpp` | Seletor de cor. | pronto | SpriteUI.ColorPicker | components-5-modal-cor.png | - |
| DragDrop, Docking, FloatingWindows | `include/SpriteUi/Widgets/DragDrop.hpp`, `Docking.hpp`, `FloatingWindows.hpp` | `src/Widgets/*.cpp` | Arrastar e soltar, encaixe de painéis e janelas flutuantes. | pronto | SpriteUI.DragDrop, SpriteUI.FloatingWindows, SpriteUI.DockInteraction | - | Sem impressão própria nesta sessão. |
| Graphs (widget antigo) | `include/SpriteUi/Widgets/Graphs.hpp` | `src/Widgets/Graphs.cpp` | Gráfico do conjunto anterior de widgets; não é o `Elements/Graph`. | pronto | SpriteUI.Elements | - | Duplica o papel do `Graph` novo; decidir qual permanece. |
| Style e StylePresets | `include/SpriteUi/Widgets/Style.hpp`, `StylePresets.hpp` | `src/Widgets/*.cpp` | Estilos e predefinições. | pronto | SpriteUI.StylePresets | - | - |

## Infraestrutura

| Parte | Header | Implementação | Função | Teste (ctest) |
|---|---|---|---|---|
| Element | `include/SpriteUi/Elements/Element.hpp` | `src/Elements/Element.cpp` | Base com transform, padding, estados e borda. | SpriteUI.Elements |
| ElementTree | `include/SpriteUi/Elements/ElementTree.hpp` | `src/Elements/ElementTree.cpp` | Árvore, criação, busca e arranjo. | SpriteUI.Elements |
| PressGesture e PointerEvent | `include/SpriteUi/Elements/PressGesture.hpp`, `PointerEvent.hpp` | `src/Elements/PressGesture.cpp` | Gesto de clique (Down e Up dentro). | SpriteUI.Elements |
| Paint | `include/SpriteUi/Elements/Paint.hpp` | `src/Elements/Paint.cpp` | Contorno e pintura comum. | SpriteUI.Primitives |

## Verificação

Suíte completa em `scratchpad/elementos-build`: 34 de 34 testes passando, sem avisos de compilação.
Captura visual: `tests/Visual/ElementCaptures.cpp`, 22 cenas, 0 falhas.
