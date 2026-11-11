#include "SpriteUi/Platform/SDL3InputAdapter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace ToolEngine::SpriteUi::Platform {
namespace {

[[nodiscard]] Input::UINavigationCommand NavigationCommandFromKey(SDL_Keycode key) noexcept {
    switch (key) {
    case SDLK_UP: return Input::UINavigationCommand::Up;
    case SDLK_DOWN: return Input::UINavigationCommand::Down;
    case SDLK_LEFT: return Input::UINavigationCommand::Left;
    case SDLK_RIGHT: return Input::UINavigationCommand::Right;
    case SDLK_RETURN:
    case SDLK_RETURN2:
    case SDLK_SPACE: return Input::UINavigationCommand::Activate;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: return Input::UINavigationCommand::Cancel;
    default: return Input::UINavigationCommand::None;
    }
}

[[nodiscard]] Input::UINavigationCommand NavigationCommandFromGamepad(
    SDL_GamepadButton button) noexcept {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return Input::UINavigationCommand::Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Input::UINavigationCommand::Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Input::UINavigationCommand::Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Input::UINavigationCommand::Right;
    case SDL_GAMEPAD_BUTTON_SOUTH: return Input::UINavigationCommand::Activate;
    case SDL_GAMEPAD_BUTTON_EAST: return Input::UINavigationCommand::Cancel;
    default: return Input::UINavigationCommand::None;
    }
}

} // namespace

SDL3InputAdapter::SDL3InputAdapter(SDL_Window* window) noexcept {
    if (!window) return;
    window_ = window;
    windowId_ = SDL_GetWindowID(window);
    windowFocused_ = windowId_ != 0 &&
                     (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool SDL3InputAdapter::ProcessEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.windowID != windowId_ || windowId_ == 0 ||
            event.motion.which == SDL_TOUCH_MOUSEID) return false;
        pointerId_ = event.motion.which;
        pointerPosition_ = {event.motion.x, event.motion.y};
        pointerDelta_.x += event.motion.xrel;
        pointerDelta_.y += event.motion.yrel;
        return true;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.windowID != windowId_ || windowId_ == 0 ||
            event.button.button != SDL_BUTTON_LEFT ||
            event.button.which == SDL_TOUCH_MOUSEID) {
            return false;
        }
        pointerId_ = event.button.which;
        pointerPosition_ = {event.button.x, event.button.y};
        if (event.button.down && !primaryDown_) primaryPressed_ = true;
        if (!event.button.down && primaryDown_) primaryReleased_ = true;
        primaryDown_ = event.button.down;
        return true;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.windowID != windowId_ || windowId_ == 0 ||
            event.wheel.which == SDL_TOUCH_MOUSEID) return false;
        pointerId_ = event.wheel.which;
        pointerPosition_ = {event.wheel.mouse_x, event.wheel.mouse_y};
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            wheelDelta_.x -= event.wheel.x;
            wheelDelta_.y -= event.wheel.y;
        } else {
            wheelDelta_.x += event.wheel.x;
            wheelDelta_.y += event.wheel.y;
        }
        return true;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (event.window.windowID != windowId_ || windowId_ == 0) return false;
        windowFocused_ = true;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID != windowId_ || windowId_ == 0) return false;
        windowFocused_ = false;
        primaryDown_ = false;
        primaryPressed_ = false;
        primaryReleased_ = false;
        navigationCommand_ = Input::UINavigationCommand::None;
        composition_ = {};
        for (auto& [key, pointer] : touchPointers_) {
            (void)key;
            pointer.down = false;
            pointer.pressed = false;
            pointer.released = false;
            pointer.cancelled = true;
            pointer.ending = true;
        }
        return true;

    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        const SDL_TouchFingerEvent& finger = event.tfinger;
        if (finger.windowID != windowId_ || windowId_ == 0 ||
            !IsFinite(finger.x) || !IsFinite(finger.y) ||
            !IsFinite(finger.dx) || !IsFinite(finger.dy)) {
            return false;
        }
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window_, &width, &height) || width <= 0 || height <= 0) {
            return false;
        }

        const auto key = std::pair{
            static_cast<std::uint64_t>(finger.touchID),
            static_cast<std::uint64_t>(finger.fingerID)
        };
        auto it = touchPointers_.find(key);
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (it == touchPointers_.end()) {
                const std::uint32_t touchPointerId = AllocateTouchPointerId();
                if (touchPointerId == 0) return false;
                it = touchPointers_.emplace(
                    key, TouchPointerState{touchPointerId}).first;
            }
        } else if (it == touchPointers_.end()) {
            return false;
        }

        TouchPointerState& pointer = it->second;
        pointer.position = {finger.x * static_cast<float>(width),
                            finger.y * static_cast<float>(height)};
        pointer.delta.x += finger.dx * static_cast<float>(width);
        pointer.delta.y += finger.dy * static_cast<float>(height);
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (!pointer.down) pointer.pressed = true;
            pointer.down = true;
            pointer.cancelled = false;
            pointer.ending = false;
        } else if (event.type == SDL_EVENT_FINGER_UP) {
            pointer.down = false;
            pointer.released = true;
            pointer.ending = true;
        } else if (event.type == SDL_EVENT_FINGER_CANCELED) {
            pointer.down = false;
            pointer.pressed = false;
            pointer.released = false;
            pointer.cancelled = true;
            pointer.ending = true;
        }
        return true;
    }

    case SDL_EVENT_KEY_DOWN:
        if (event.key.windowID != windowId_ || windowId_ == 0 || !windowFocused_) return false;
        if (!event.key.repeat) {
            const auto command = NavigationCommandFromKey(event.key.key);
            if (navigationCommand_ == Input::UINavigationCommand::None) {
                navigationCommand_ = command;
            }
        }
        return NavigationCommandFromKey(event.key.key) != Input::UINavigationCommand::None;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (!windowFocused_) return false;
        if (navigationCommand_ == Input::UINavigationCommand::None) {
            navigationCommand_ = NavigationCommandFromGamepad(
                static_cast<SDL_GamepadButton>(event.gbutton.button));
        }
        return NavigationCommandFromGamepad(
            static_cast<SDL_GamepadButton>(event.gbutton.button)) != Input::UINavigationCommand::None;

    case SDL_EVENT_TEXT_INPUT:
        if (event.text.windowID != windowId_ || windowId_ == 0 || !windowFocused_ ||
            !textInputActive_ ||
            !event.text.text) {
            return false;
        }
        committedText_.append(event.text.text);
        composition_ = {};
        return true;

    case SDL_EVENT_TEXT_EDITING:
        if (event.edit.windowID != windowId_ || windowId_ == 0 || !windowFocused_ ||
            !textInputActive_ ||
            !event.edit.text) {
            return false;
        }
        composition_.text = event.edit.text;
        composition_.start = event.edit.start;
        composition_.length = event.edit.length;
        composition_.active = !composition_.text.empty();
        return true;

    default:
        return false;
    }
}

Input::UIInputSnapshot SDL3InputAdapter::BuildSnapshot(std::uint64_t frameId) noexcept {
    Input::UIInputSnapshot snapshot;
    snapshot.pointerPositionLogical = pointerPosition_;
    snapshot.pointerDeltaLogical = pointerDelta_;
    snapshot.wheelDelta = wheelDelta_;
    snapshot.pointerId = pointerId_;
    snapshot.primaryDown = primaryDown_ && windowFocused_;
    snapshot.primaryPressed = primaryPressed_ && windowFocused_;
    snapshot.primaryReleased = primaryReleased_ && windowFocused_;
    snapshot.windowFocused = windowFocused_;
    snapshot.frameId = frameId;

    pointerDelta_ = {};
    wheelDelta_ = {};
    primaryPressed_ = false;
    primaryReleased_ = false;
    return snapshot;
}

std::vector<Input::UIInputSnapshot> SDL3InputAdapter::BuildPointerSnapshots(
    std::uint64_t frameId) {
    std::vector<Input::UIInputSnapshot> snapshots;
    snapshots.reserve(touchPointers_.size() + 1u);
    snapshots.push_back(BuildSnapshot(frameId));

    for (auto it = touchPointers_.begin(); it != touchPointers_.end();) {
        TouchPointerState& pointer = it->second;
        Input::UIInputSnapshot snapshot;
        snapshot.pointerId = pointer.pointerId;
        snapshot.pointerPositionLogical = pointer.position;
        snapshot.pointerDeltaLogical = pointer.delta;
        snapshot.primaryDown = pointer.down && windowFocused_;
        snapshot.primaryPressed = pointer.pressed && windowFocused_;
        snapshot.primaryReleased = pointer.released && windowFocused_;
        snapshot.pointerCancelled = pointer.cancelled || !windowFocused_;
        snapshot.windowFocused = windowFocused_;
        snapshot.frameId = frameId;
        snapshots.push_back(snapshot);

        pointer.delta = {};
        pointer.pressed = false;
        pointer.released = false;
        pointer.cancelled = false;
        if (pointer.ending) {
            it = touchPointers_.erase(it);
        } else {
            ++it;
        }
    }
    return snapshots;
}

std::uint32_t SDL3InputAdapter::AllocateTouchPointerId() noexcept {
    const std::size_t attempts = touchPointers_.size() + 2u;
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        const std::uint32_t candidate = nextTouchPointerId_;
        nextTouchPointerId_ = candidate == 0xFFFFFFFFu
            ? 0x80000000u
            : candidate + 1u;
        if (candidate == pointerId_) continue;
        const bool inUse = std::any_of(touchPointers_.begin(), touchPointers_.end(),
            [candidate](const auto& entry) {
                return entry.second.pointerId == candidate;
            });
        if (!inUse) return candidate;
    }
    return 0;
}

Input::UINavigationCommand SDL3InputAdapter::ConsumeNavigationCommand() noexcept {
    const Input::UINavigationCommand command = windowFocused_
        ? navigationCommand_
        : Input::UINavigationCommand::None;
    navigationCommand_ = Input::UINavigationCommand::None;
    return command;
}

bool SDL3InputAdapter::SetTextInputActive(bool active) noexcept {
    if (!window_ || windowId_ == 0) return false;
    if (active == textInputActive_) return true;

    const bool success = active ? SDL_StartTextInput(window_) : SDL_StopTextInput(window_);
    if (success) {
        textInputActive_ = active;
        composition_ = {};
    }
    return success;
}

bool SDL3InputAdapter::SetTextInputArea(
    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
    std::int32_t cursor) noexcept {
    if (!window_ || windowId_ == 0 || width < 0 || height < 0) return false;
    const SDL_Rect area{x, y, width, height};
    return SDL_SetTextInputArea(window_, &area, cursor);
}

std::string SDL3InputAdapter::ConsumeCommittedText() noexcept {
    std::string text;
    text.swap(committedText_);
    return text;
}

bool SDL3InputAdapter::SetClipboardText(std::string_view utf8Text) {
    if (utf8Text.find('\0') != std::string_view::npos) return false;
    const std::string nullTerminated(utf8Text);
    return SDL_SetClipboardText(nullTerminated.c_str());
}

bool SDL3InputAdapter::GetClipboardText(std::string& utf8Text) {
    std::unique_ptr<char, decltype(&SDL_free)> clipboardText(
        SDL_GetClipboardText(), &SDL_free);
    if (!clipboardText) return false;

    std::string result(clipboardText.get());
    if (result.empty() && SDL_HasClipboardText()) return false;
    utf8Text.swap(result);
    return true;
}

bool SDL3InputAdapter::HasClipboardText() noexcept {
    return SDL_HasClipboardText();
}

} // namespace ToolEngine::SpriteUi::Platform
