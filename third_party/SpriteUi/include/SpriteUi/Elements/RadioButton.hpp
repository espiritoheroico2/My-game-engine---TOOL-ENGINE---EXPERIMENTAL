#pragma once

// Grupo de botoes de radio: uma lista de opcoes em que so uma pode estar escolhida.
//
// Partes: o grupo guarda a opcao escolhida pelo identificador (selectedId). Cada opcao (RadioOption) guarda
// o seu identificador e o rotulo, e nao existe booleano por opcao. Por isso so uma opcao fica marcada: escolher
// outra troca o identificador do grupo.
//
// Desenho: uma linha por opcao, na ordem da lista, de cima para baixo. Cada linha tem um circulo fixo a
// esquerda (com um ponto dentro na opcao escolhida) e a faixa do rotulo a direita. O grupo cresce: cada
// AddOption cria uma linha nova, sem limite fixo. O texto do rotulo e desenhado pelo chamador na faixa
// RadioLabelRect(), como o rotulo de um botao.
//
// Eventos: Click numa linha escolhe a opcao (ValueChanged so quando muda). Arrastar para fora e soltar nao
// escolhe nada. Desabilitado nao muda a escolha pelo usuario; Select (de programa) continua valendo.
// Teclado: Setas movem a escolha e o foco, sem dar a volta nas pontas. Espaco escolhe a opcao focada.
//
// Exemplo:
//   auto id = tree.Create<RadioGroup>(parent);
//   auto* group = tree.GetAs<RadioGroup>(id);
//   group->AddOption(1, "Pequeno");
//   group->AddOption(2, "Medio");
//   group->Select(2);

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

using RadioOptionId = std::uint64_t;

// Identificador reservado para "nenhuma opcao". Uma opcao nunca usa este valor.
inline constexpr RadioOptionId NoRadioOption = std::numeric_limits<RadioOptionId>::max();
// Indice reservado para "nenhuma linha".
inline constexpr std::size_t NoRadioIndex = std::numeric_limits<std::size_t>::max();

struct RadioOption {
    RadioOptionId id = NoRadioOption;
    std::string label;
};

struct RadioStyle {
    Vec4 circleBackground{0.12f, 0.12f, 0.14f, 1.0f};
    Vec4 disabledBackground{0.15f, 0.15f, 0.16f, 1.0f};
    Vec4 dotColor{0.05f, 0.2f, 0.55f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float circleSize = 18.0f;
    float labelGap = 8.0f;        // Espaco entre o circulo e o rotulo.
    float focusRingWidth = 2.0f;
};

struct RadioEvents {
    bool pressStarted = false;
    bool pressEnded = false;
    bool clicked = false;
    bool valueChanged = false;
};

class RadioGroup final : public Element {
  public:
    std::vector<RadioOption> options;
    RadioOptionId selectedId = NoRadioOption;
    std::size_t focusedIndex = NoRadioIndex;
    float rowHeight = 26.0f;

    // Adiciona uma opcao no fim. Identificador repetido ou NoRadioOption: false.
    bool AddOption(RadioOptionId id, std::string label);

    // Escolhe pelo identificador, sem checar o desabilitado. Desconhecido: false. Retorna true se mudou.
    bool Select(RadioOptionId id) noexcept;

    // Posicao da opcao escolhida, ou NoRadioIndex.
    [[nodiscard]] std::size_t SelectedIndex() const noexcept;

    // Linha sob o ponto (coordenadas do elemento), ou NoRadioIndex.
    [[nodiscard]] std::size_t RowAt(Vec2 point) const noexcept;

    // Ponteiro: Click numa linha escolhe a opcao daquela linha.
    [[nodiscard]] RadioEvents OnPointer(const PointerEvent& event) noexcept;

    // Setas: delta -1 (cima) ou +1 (baixo). Move a escolha e o foco, parando nas pontas. True se a escolha mudou.
    [[nodiscard]] bool MoveSelection(int delta) noexcept;

    // Espaco: escolhe a opcao focada. True se a escolha mudou.
    [[nodiscard]] bool SelectFocused() noexcept;

    // Tab entra no grupo: o foco vai para a opcao escolhida (ou a primeira, se nao houver escolha).
    void FocusSelected() noexcept;

  private:
    PressGesture gesture_;
};

// Retangulo da linha i na area util do grupo. Indice fora da lista: retangulo vazio.
[[nodiscard]] Rect RadioRowRect(const RadioGroup& group, std::size_t index) noexcept;

// Circulo da linha: tamanho fixo a esquerda, centralizado na vertical.
[[nodiscard]] Rect RadioCircleRect(const RadioGroup& group, const RadioStyle& style, std::size_t index) noexcept;

// Faixa do rotulo: a linha menos o circulo e a folga.
[[nodiscard]] Rect RadioLabelRect(const RadioGroup& group, const RadioStyle& style, std::size_t index) noexcept;

// Itens de desenho de todas as linhas: circulos, ponto da escolhida e contorno de foco na linha focada.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildRadioGroupItems(const RadioGroup& group,
                                                                        const RadioStyle& style, float zOrder,
                                                                        std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
