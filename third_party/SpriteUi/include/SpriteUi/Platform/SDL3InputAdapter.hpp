#pragma once

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Input/SpatialNavigation.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

typedef union SDL_Event SDL_Event;
typedef struct SDL_Window SDL_Window;

namespace ToolEngine::SpriteUi::Platform {

struct SDL3TextComposition {
    std::string text;
    std::int32_t start = -1;
    std::int32_t length = -1;
    bool active = false;
};

class SDL3InputAdapter {
public:
    explicit SDL3InputAdapter(SDL_Window* window) noexcept;

    [[nodiscard]] bool IsValid() const noexcept { return windowId_ != 0; }

    // Call once for each SDL event from the application's event loop.
    // Returns true when the event belongs to this window and was processed.
    [[nodiscard]] bool ProcessEvent(const SDL_Event& event);

    // Consume accumulated event deltas and edge transitions for one UI frame.
    [[nodiscard]] Input::UIInputSnapshot BuildSnapshot(std::uint64_t frameId) noexcept;
    // Consume a mouse snapshot plus snapshots for active SDL touch contacts.
    [[nodiscard]] std::vector<Input::UIInputSnapshot> BuildPointerSnapshots(
        std::uint64_t frameId);

    // Consume the first navigation command received since the previous call.
    [[nodiscard]] Input::UINavigationCommand ConsumeNavigationCommand() noexcept;

    // SDL text input is opt-in. Call on the SDL main thread when a text field gains/loses focus.
    [[nodiscard]] bool SetTextInputActive(bool active) noexcept;
    [[nodiscard]] bool SetTextInputArea(
        std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
        std::int32_t cursor) noexcept;

    // Committed UTF-8 is accumulated until consumed; composition is borrowed until its next update.
    [[nodiscard]] std::string ConsumeCommittedText() noexcept;
    [[nodiscard]] const SDL3TextComposition& GetTextComposition() const noexcept {
        return composition_;
    }

    // Clipboard access is process-wide and must be called on SDL's main thread.
    [[nodiscard]] static bool SetClipboardText(std::string_view utf8Text);
    [[nodiscard]] static bool GetClipboardText(std::string& utf8Text);
    [[nodiscard]] static bool HasClipboardText() noexcept;

private:
    struct TouchPointerState {
        std::uint32_t pointerId = 0;
        Vec2 position{};
        Vec2 delta{};
        bool down = false;
        bool pressed = false;
        bool released = false;
        bool cancelled = false;
        bool ending = false;
    };

    [[nodiscard]] std::uint32_t AllocateTouchPointerId() noexcept;

    SDL_Window* window_ = nullptr;
    std::uint32_t windowId_ = 0;
    std::uint32_t pointerId_ = 0;
    Vec2 pointerPosition_{};
    Vec2 pointerDelta_{};
    Vec2 wheelDelta_{};
    bool primaryDown_ = false;
    bool primaryPressed_ = false;
    bool primaryReleased_ = false;
    bool windowFocused_ = false;
    bool textInputActive_ = false;
    std::map<std::pair<std::uint64_t, std::uint64_t>, TouchPointerState> touchPointers_;
    std::uint32_t nextTouchPointerId_ = 0x80000000u;
    std::string committedText_;
    SDL3TextComposition composition_;
    Input::UINavigationCommand navigationCommand_ = Input::UINavigationCommand::None;
};

} // namespace ToolEngine::SpriteUi::Platform
