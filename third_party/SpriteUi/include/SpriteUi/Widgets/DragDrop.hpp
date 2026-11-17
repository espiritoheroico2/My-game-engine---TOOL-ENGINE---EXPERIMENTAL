#pragma once

// Arrastar e soltar com carga (payload) como modelo puro. Um gesto passa por tres fases: Idle (nada),
// Pending (botao apertado, ainda sem arrastar) e Dragging (o ponteiro andou o limiar). O chamador traduz o
// ponteiro e as teclas em chamadas deste modelo e desenha a partir do estado (Phase, Payload, HoveredTarget).
//
// Regras:
//  - O gesto so vira arraste depois de o ponteiro andar pelo menos `threshold` pixels desde o aperto.
//    Soltar antes disso nao e arraste: PointerUp devolve nada.
//  - Os alvos chegam a cada movimento e a cada soltura, na ordem de desenho. Quando mais de um alvo contem o
//    ponteiro, vale o ultimo da lista (o de cima).
//  - Alvo com acceptedType vazio aceita qualquer carga. Caso contrario, so aceita carga do mesmo tipo.
//    Alvo que nao aceita a carga nunca e destino, nem durante o arraste.
//  - Soltar sobre um alvo que aceita devolve DropResult. Soltar fora dele, ou sem arraste, cancela o gesto.
//  - Cancel() (Escape) descarta o gesto em curso ou pendente. Cancelar nunca devolve carga.
//  - Todos os pontos e retangulos estao no mesmo espaco (por exemplo, a tela logica).
//  - Entrada nao finita e ignorada: retorna false e nao muda o estado.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ToolEngine::SpriteUi::Widgets {

struct DragPayload {
    std::string type; // Tipo da carga. Os alvos filtram por ele.
    std::string data; // Conteudo. O modelo nao interpreta: o chamador escolhe o formato.
};

struct DropTarget {
    UIHandle handle{};
    Rect bounds{};
    std::string acceptedType; // Vazio aceita qualquer tipo de carga.
};

struct DropResult {
    UIHandle source{};
    UIHandle target{};
    DragPayload payload{};
};

enum class DragPhase : std::uint8_t { Idle, Pending, Dragging };

class DragDropControl {
  public:
    float threshold = 4.0f; // Distancia em pixels que separa um clique de um arraste. Deve ser >= 0.

    // Aperta o botao sobre a origem e comeca um gesto pendente. Retorna false se ja ha um gesto ou se a
    // origem ou o ponto nao sao validos.
    [[nodiscard]] bool PointerDown(UIHandle source, Vec2 pointer, DragPayload payload);

    // Move o ponteiro. Retorna true se o estado mudou: entrou em arraste ou trocou o alvo sob o ponteiro.
    [[nodiscard]] bool PointerMove(Vec2 pointer, std::span<const DropTarget> targets);

    // Solta o botao. Devolve o resultado so se havia arraste e o ponteiro caiu num alvo que aceita a carga.
    // Em qualquer caso o gesto termina.
    [[nodiscard]] std::optional<DropResult> PointerUp(Vec2 pointer, std::span<const DropTarget> targets);

    // Escape. Retorna true se havia um gesto (pendente ou em arraste).
    bool Cancel() noexcept;

    [[nodiscard]] DragPhase Phase() const noexcept { return phase_; }
    [[nodiscard]] const DragPayload& Payload() const noexcept { return payload_; }
    [[nodiscard]] Vec2 Pointer() const noexcept { return pointer_; }

    // Alvo que aceita a carga e esta sob o ponteiro, durante o arraste.
    [[nodiscard]] std::optional<UIHandle> HoveredTarget() const noexcept { return hovered_; }

  private:
    [[nodiscard]] static bool Accepts(const DropTarget& target, const DragPayload& payload) noexcept;
    [[nodiscard]] std::optional<UIHandle> FindTarget(Vec2 pointer,
                                                     std::span<const DropTarget> targets) const noexcept;
    [[nodiscard]] bool PastThreshold(Vec2 pointer) const noexcept;
    void Reset() noexcept;

    DragPhase phase_ = DragPhase::Idle;
    UIHandle source_{};
    DragPayload payload_{};
    Vec2 pressPoint_{};
    Vec2 pointer_{};
    std::optional<UIHandle> hovered_;
};

} // namespace ToolEngine::SpriteUi::Widgets
