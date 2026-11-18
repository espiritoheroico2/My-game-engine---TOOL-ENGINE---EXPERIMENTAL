#pragma once

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ToolEngine::SpriteUi::Input {

struct UIInputSnapshot {
    Vec2 pointerPositionLogical{};
    Vec2 pointerDeltaLogical{};
    Vec2 wheelDelta{};
    std::uint32_t pointerId = 0;
    bool primaryDown = false;
    bool primaryPressed = false;
    bool primaryReleased = false;
    bool pointerCancelled = false;
    bool windowFocused = true;
    std::uint64_t frameId = 0;
    // Tempo do quadro informado pelo host, em segundos. O roteador nunca consulta o relogio do sistema.
    double frameTimeSeconds = 0.0;
};

// Tempo que o ponteiro precisa ficar pressionado para o estado Hold valer.
inline constexpr double kHoldDurationSeconds = 0.25;

struct HitTarget {
    UIHandle handle{};
    UIHandle parent{};
    Affine2D localToScreen{}; // Local pixel coordinates -> logical screen coordinates.
    Vec2 localSize{};         // Logical pixel dimensions in local space.
    Rect clipRectLogical{};   // Effective ancestor clip; used when clipEnabled.
    bool clipEnabled = false;
    // Mesma chave de desenho (DrawOrder.hpp): camada, zOrder e ordem de pintura. Quem e desenhado por cima
    // recebe o ponteiro primeiro. Padroes iguais a 0 mantem o comportamento antigo.
    std::int32_t layer = 0;
    float zOrder = 0.0f;
    std::uint64_t paintOrder = 0;
    bool visible = true;
    bool enabled = true;
    bool focusable = false;
    bool blocksInput = true; // Consumes input from the world; UI events still bubble to parents.
};

enum class UIEventType : std::uint8_t {
    PointerEnter,
    PointerLeave,
    PointerDown,
    PointerMove,
    PointerUp,
    Click,
    Wheel,
    Cancel,
    FocusGained,
    FocusLost
};

struct UIEvent {
    UIEventType type = UIEventType::PointerMove;
    UIHandle target{};
    Vec2 screenPosition{};
    Vec2 localPosition{};
    Vec2 delta{};
    std::uint32_t pointerId = 0;
    std::uint64_t frameId = 0;
    bool inside = false;
};

struct UIPointerRoutingState {
    UIHandle capturedEntity{};
    UIHandle focusedEntity{};
    UIHandle hoveredEntity{};
    bool consumedThisFrame = false;
    // Derivado de tempo: Pressed ha mais de kHoldDurationSeconds. Nao e um evento.
    bool holdActive = false;
};

class UIEventRouter {
public:
    [[nodiscard]] std::vector<UIEvent> Process(
        const UIInputSnapshot& input,
        std::span<const HitTarget> targets);

    [[nodiscard]] const UIPointerRoutingState& GetState() const noexcept { return state_; }
    void Reset() noexcept;

    // Alvo que um clique em `screenPosition` acertaria agora (mesma regra de Process). Sem estado: pode ser
    // usado para decidir a quem entregar um evento antes de o roteador processar o quadro. Retorna handle invalido
    // se nenhum alvo cobre o ponto. Um alvo desabilitado tambem conta como coberto: quem chama decide o que fazer.
    [[nodiscard]] static UIHandle TopmostAt(std::span<const HitTarget> targets, Vec2 screenPosition) noexcept;

private:
    [[nodiscard]] static const HitTarget* FindTarget(
        std::span<const HitTarget> targets, UIHandle handle) noexcept;

    [[nodiscard]] static const HitTarget* HitTest(
        std::span<const HitTarget> targets, Vec2 screenPosition, Vec2* outLocal) noexcept;

    static void PushEvent(
        std::vector<UIEvent>& events,
        UIEventType type,
        const HitTarget& target,
        const UIInputSnapshot& input,
        Vec2 localPosition,
        Vec2 delta = {},
        bool inside = false);

    [[nodiscard]] static bool PushBubbledEvent(
        std::vector<UIEvent>& events,
        UIEventType type,
        const HitTarget& target,
        const UIInputSnapshot& input,
        Vec2 delta,
        std::span<const HitTarget> targets);

    [[nodiscard]] static bool BlocksInput(
        const HitTarget& target, std::span<const HitTarget> targets) noexcept;

    UIPointerRoutingState state_{};
    double pressTimeSeconds_ = 0.0;
    std::uint64_t lastFrameId_ = 0;
    bool hasProcessedFrame_ = false;
};

} // namespace ToolEngine::SpriteUi::Input
