# Convenção de composição de widgets

Um widget da SpriteUI é um conjunto de peças que o chamador monta e mantém. A biblioteca não guarda widgets:
guarda nós, visuais e itens de desenho que o chamador cria, atualiza e descarta.

## As quatro peças

| Peça | Quem cria | Onde fica | Função |
|---|---|---|---|
| **Nós** (`UIHandle` de `LayoutTree`) | builder ou chamador | `LayoutTree` | Posição e tamanho. Um widget pode ser uma subárvore. |
| **Visual** (`ElementVisual`) | chamador, por quadro | `LayoutSceneBridge::SetVisual` | Cor, zOrder, recorte e se o nó recebe entrada. |
| **Texto** (`TextLayoutResult` + `GenerateTextRenderItems`) | chamador, por quadro | fora da árvore, com o `owner` = handle do nó | Glifos na mesma origem usada por caret e seleção. |
| **Alvo de clique** (`HitTarget`) | `LayoutSceneBridge::Extract` | saída do `Extract` | Entrada do `UIEventRouter`. Nunca montado à mão. |

## Regras

1. **Builder cria nós, não visuais.** Uma função de composição cria só os nós e devolve uma struct com os handles
   (`LabeledFieldNodes`). O chamador anexa visual e texto depois.
2. **O widget é dono dos handles que criou.** Remover o nó raiz remove a subárvore inteira. Handles inválidos
   (pai removido) produzem handles inválidos, sem nós órfãos.
3. **O estado fica fora do widget.** Texto, foco, abas e árvore de docking vivem no modelo do chamador (por exemplo
   `TextEditState`, `UITabGroupComponent`, `UIDockNode`). O widget lê o modelo a cada quadro e só muda o modelo
   pelas operações dele. Não há cache de estado dentro dos builders.
4. **Estilo é valor.** Os construtores de estilo (`Sized`, `Row`, `Spacer`...) devolvem `FlexItem`. Não criam nós.
5. **Alinhamento é por filho.** `AlignItems` vem do estilo do próprio filho (como `align-self` do CSS). Um
   contêiner não centraliza os filhos no eixo transversal; use `CenteredItem` no filho.
6. **Medidas em pixels lógicos.** `Percent` é fração do conteúdo do pai. Tamanho fixo não encolhe.

## Como um quadro usa as peças (esboço)

```cpp
// Estado: fora do widget (no modelo do chamador).
// Quadro: nós -> layout; cena -> visuais e alvos; texto -> itens; tudo passa pelo extrator.
tree.Layout(viewport);
auto scene = bridge.Extract(tree);            // itens de fundo e alvos de clique
const auto events = router.Process(input, scene.hitTargets);
// visual por estado (hover, foco) e texto:
auto text = ST::GenerateTextRenderItems(layout, box, color, slice, clip, z, paint, handle);
```

## Onde a convenção está aplicada

- `include/SpriteUi/Layout/LayoutPatterns.hpp`: builders de estilo e `CreateLabeledField`, que segue a regra 1 e 2.
- Os widgets existentes (botões, campos, docking, grafos) **não foram reescritos**. Eles seguem o mesmo modelo de
  forma implícita, mas ainda não expõem handles por struct. Migrar cada um fica para uma etapa própria.

## Como adicionar um widget novo

1. Escreva um builder que cria só nós e devolve a struct de handles.
2. Teste as posições e tamanhos pelo `LayoutTree` (ver `tests/test_layout_patterns.cpp`).
3. O estado vai para um modelo separado, com testes de operações (ver `tests/test_text_edit.cpp`).
4. Visual e texto são montados pelo chamador a cada quadro, com o handle do nó como `owner`.
