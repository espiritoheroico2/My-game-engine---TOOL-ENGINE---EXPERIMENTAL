#pragma once

// Divisor (Splitter): uma alca entre dois paineis. Arrastar o divisor muda o tamanho do painel da frente (o
// "leading": esquerda ou topo) e o painel de tras (o "trailing") ocupa o resto.
//
// Area de pegada: hitThickness (padrao 8 px), centrada na linha visivel de lineThickness (padrao 2 px). Cada lado
// da linha tem 4 px de pegada a partir do centro, entao o divisor e facil de pegar sem ficar largo na tela.
//
// Sentidos: o arraste funciona nos dois sentidos. Para um divisor vertical (Axis::Vertical), arrastar para a direita
// aumenta o painel da esquerda e arrastar para a esquerda o diminui. Para o horizontal, vale o mesmo com cima e baixo.
//
// Limites por painel: minLeading/maxLeading limitam o painel da frente; minTrailing/maxTrailing limitam o de tras.
// Os de tras so valem quando totalSize (a soma dos dois paineis) e informado. Se os limites nao cabem juntos
// (min de um lado acima do max do outro), o divisor nao muda de tamanho.
//
// O divisor nao move os paineis: devolve o novo tamanho do painel da frente em SplitterEvents::leadingSize, e quem
// chama aplica no layout (largura fixa do painel da frente, por exemplo).
//
// Exemplo de jogo: a lista de jogadores a esquerda do mapa tem entre 180 e 420 px; o chat no rodape tem entre 120 e
// 300 px. Cada divisor usa os limites do seu proprio painel.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

enum class SplitAxis : std::uint8_t {
    Vertical,   // Divisor vertical: separa esquerda e direita; arrasta em X
    Horizontal  // Divisor horizontal: separa cima e baixo; arrasta em Y
};

// Cursor sugerido pelo divisor. O host traduz para o cursor do sistema (o core nao toca no cursor).
enum class SplitterCursor : std::uint8_t { Arrow, ResizeHorizontal, ResizeVertical };

struct SplitterStyle {
    float lineThickness = 2.0f;                             // espessura visivel, em pixels logicos
    Vec4 line{0.3f, 0.32f, 0.38f, 1.0f};                    // linha em repouso
    Vec4 lineHot{0.45f, 0.6f, 0.95f, 1.0f};                 // linha com o ponteiro em cima ou arrastando
};

struct SplitterEvents {
    bool dragStarted = false;
    bool dragEnded = false;
    bool resized = false;
    float leadingSize = 0.0f; // Tamanho do painel da frente depois do evento (valido quando resized)
};

class Splitter final : public Element {
  public:
    SplitAxis axis = SplitAxis::Vertical;
    float hitThickness = 8.0f;   // Area de pegada, em pixels logicos (minimo efetivo: a linha visivel)

    float leadingSize = 200.0f;  // Tamanho atual do painel da frente, na direcao do eixo
    float totalSize = 0.0f;      // Soma dos dois paineis. 0 = desconhecida: so os limites do painel da frente valem
    float minLeading = 60.0f;
    float maxLeading = NoMaxSize;
    float minTrailing = 60.0f;
    float maxTrailing = NoMaxSize;

    // Trata um evento de ponteiro. O chamador faz o teste de acerto e informa inside.
    [[nodiscard]] SplitterEvents OnPointer(const PointerEvent& event) noexcept;

    // Novo tamanho do painel da frente para um deslocamento acumulado desde o inicio do arraste, respeitando os
    // limites dos dois paineis.
    [[nodiscard]] float ClampedLeading(float wanted) const noexcept;

    // Retangulo da linha visivel, centrada na caixa do divisor (Element::border).
    [[nodiscard]] Rect LineRect(const SplitterStyle& style = {}) const noexcept;

    // Area de pegada: a caixa do divisor, com largura de pelo menos hitThickness, centrada na linha. Use para o teste
    // de acerto mesmo que o layout tenha dado uma caixa mais fina.
    [[nodiscard]] Rect HitRect() const noexcept;

    [[nodiscard]] SplitterCursor Cursor() const noexcept;
    [[nodiscard]] bool Dragging() const noexcept { return dragging_; }

  private:
    bool dragging_ = false;
    Vec2 grab_{0.0f, 0.0f};
    float startLeading_ = 0.0f;
};

// Itens da linha visivel (cor em repouso ou destacada). Usa o zOrder e o paintOrder informados.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildSplitterItems(const Splitter& splitter,
                                                                      const SplitterStyle& style, float zOrder,
                                                                      std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
