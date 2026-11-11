#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace ToolEngine::SpriteUi::Input {

enum class NavDirection : std::uint8_t { Up, Down, Left, Right };

struct UIFocusCandidate {
    UIHandle handle{};
    Rect bounds{};
    bool focusable = true;
    bool enabled = true;
    bool visible = true;
    std::int32_t navigationOrder = 0;
};

// Selects the nearest candidate in a 90-degree directional cone (±45 degrees).
// The caller sends Activate/Cancel to the focused widget as semantic events.
[[nodiscard]] UIHandle FindSpatialNeighbor(
    UIHandle current,
    NavDirection direction,
    std::span<const UIFocusCandidate> candidates) noexcept;

enum class UINavigationCommand : std::uint8_t {
    None, Up, Down, Left, Right, Activate, Cancel
};

enum class UISemanticEventType : std::uint8_t {
    FocusChanged, Activate, Cancel
};

struct UISemanticEvent {
    UISemanticEventType type = UISemanticEventType::FocusChanged;
    UIHandle target{};
    UIHandle previousTarget{};
};

// Stateful semantic navigation. Platform adapters (SDL, GLFW, XInput, etc.)
// translate key/gamepad edges into one command and feed it here.
class UINavigationRouter {
public:
    [[nodiscard]] UIHandle Focused() const noexcept { return focused_; }

    [[nodiscard]] std::vector<UISemanticEvent> Process(
        UINavigationCommand command,
        std::span<const UIFocusCandidate> candidates);

    void SetFocused(UIHandle handle) noexcept { focused_ = handle; }
    void Reset() noexcept { focused_ = InvalidUIHandle; }

private:
    [[nodiscard]] static bool IsUsable(const UIFocusCandidate& candidate) noexcept;
    [[nodiscard]] static const UIFocusCandidate* FindCandidate(
        UIHandle handle, std::span<const UIFocusCandidate> candidates) noexcept;
    [[nodiscard]] static UIHandle FindFirst(
        std::span<const UIFocusCandidate> candidates) noexcept;

    UIHandle focused_ = InvalidUIHandle;
};

} // namespace ToolEngine::SpriteUi::Input
