#include "SpriteUi/Platform/SDL3InputAdapter.hpp"
#include "SpriteUi/Platform/SDL3Window.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <algorithm>
#include <utility>

namespace {

using ToolEngine::SpriteUi::Input::UINavigationCommand;
using ToolEngine::SpriteUi::Platform::SDL3InputAdapter;
using ToolEngine::SpriteUi::Platform::SDL3Window;
using ToolEngine::SpriteUi::Platform::SDL3WindowConfig;

bool Check(bool condition, const char* message) {
    if (condition) return true;
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool TestNavigationEvents(SDL_Window* window) {
    SDL3InputAdapter adapter(window);
    if (!Check(adapter.IsValid(), "SDL3 input adapter should have a valid window")) return false;

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.key = SDLK_RIGHT;
    if (!Check(!adapter.ProcessEvent(event), "keyboard input should be ignored before focus")) return false;

    event = {};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.windowID = SDL_GetWindowID(window);
    if (!Check(adapter.ProcessEvent(event), "focus-gained event should be accepted")) return false;

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.key = SDLK_RIGHT;
    event.key.repeat = true;
    if (!Check(adapter.ProcessEvent(event), "mapped key repeat should be handled")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::None,
               "key repeat should not generate a navigation command")) return false;

    event.key.repeat = false;
    if (!Check(adapter.ProcessEvent(event), "arrow key should be handled")) return false;
    event.key.key = SDLK_RETURN;
    if (!Check(adapter.ProcessEvent(event), "activation key should be handled")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::Right,
               "only the first navigation command in a frame should be consumed")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::None,
               "navigation commands should be consumed once")) return false;

    event = {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (!Check(adapter.ProcessEvent(event), "gamepad d-pad should be handled while focused")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::Up,
               "gamepad d-pad should map to navigation")) return false;

    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    if (!Check(adapter.ProcessEvent(event), "gamepad south button should be handled")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::Activate,
               "gamepad south button should activate")) return false;

    event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    if (!Check(adapter.ProcessEvent(event), "gamepad east button should be handled")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::Cancel,
               "gamepad east button should cancel")) return false;

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window) + 1;
    event.key.key = SDLK_ESCAPE;
    if (!Check(!adapter.ProcessEvent(event), "events from another window should be ignored")) return false;

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.key = SDLK_ESCAPE;
    if (!Check(adapter.ProcessEvent(event), "escape should be handled")) return false;

    event = {};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = SDL_GetWindowID(window);
    if (!Check(adapter.ProcessEvent(event), "focus-lost event should be accepted")) return false;
    if (!Check(adapter.ConsumeNavigationCommand() == UINavigationCommand::None,
               "focus loss should discard pending navigation")) return false;

    event = {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    if (!Check(!adapter.ProcessEvent(event), "gamepad input should be ignored without window focus")) return false;

    return true;
}

bool TestTextInputEvents(SDL_Window* window) {
    SDL3InputAdapter adapter(window);
    const Uint32 windowId = SDL_GetWindowID(window);

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.windowID = windowId;
    if (!Check(adapter.ProcessEvent(event), "text input test window should gain focus")) return false;

    char committed[] = "caf\xc3\xa9";
    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId;
    event.text.text = committed;
    if (!Check(!adapter.ProcessEvent(event), "text should be ignored before SDL text input is activated")) return false;
    if (!Check(adapter.SetTextInputActive(true), "SDL text input should activate")) return false;
    if (!Check(adapter.SetTextInputArea(4, 8, 24, 16, 12),
               "SDL IME candidate area should be configurable")) return false;
    if (!Check(!adapter.SetTextInputArea(0, 0, -1, 16, 0),
               "negative IME area dimensions should be rejected")) return false;
    if (!Check(adapter.ProcessEvent(event), "committed UTF-8 text should be handled")) return false;
    committed[0] = 'X';
    if (!Check(adapter.ConsumeCommittedText() == "caf\xc3\xa9",
               "committed text should be copied and preserved as UTF-8")) return false;
    if (!Check(adapter.ConsumeCommittedText().empty(),
               "committed text should be consumed only once")) return false;

    char composition[] = "\xe3\x81\x82";
    event = {};
    event.type = SDL_EVENT_TEXT_EDITING;
    event.edit.windowID = windowId;
    event.edit.text = composition;
    event.edit.start = 1;
    event.edit.length = 2;
    if (!Check(adapter.ProcessEvent(event), "IME composition should be handled")) return false;
    composition[0] = 'X';
    const auto& activeComposition = adapter.GetTextComposition();
    if (!Check(activeComposition.active && activeComposition.text == "\xe3\x81\x82" &&
                   activeComposition.start == 1 && activeComposition.length == 2,
               "IME composition text and selection range should be copied")) return false;

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId;
    event.text.text = "done";
    if (!Check(adapter.ProcessEvent(event), "committed text should end composition")) return false;
    if (!Check(!adapter.GetTextComposition().active && adapter.GetTextComposition().text.empty(),
               "committing text should clear active composition")) return false;
    if (!Check(adapter.ConsumeCommittedText() == "done",
               "committed text should be available after composition")) return false;

    event = {};
    event.type = SDL_EVENT_TEXT_EDITING;
    event.edit.windowID = windowId + 1;
    event.edit.text = "ignored";
    if (!Check(!adapter.ProcessEvent(event), "text events from other windows should be ignored")) return false;

    event = {};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = windowId;
    if (!Check(adapter.ProcessEvent(event), "text input window should lose focus")) return false;
    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId;
    event.text.text = "ignored";
    if (!Check(!adapter.ProcessEvent(event), "text input should be ignored after focus loss")) return false;
    if (!Check(adapter.SetTextInputActive(false), "SDL text input should deactivate")) return false;

    return true;
}

bool TestTouchPointerEvents(SDL_Window* window) {
    using ToolEngine::SpriteUi::Input::UIInputSnapshot;

    SDL3InputAdapter adapter(window);
    const Uint32 windowId = SDL_GetWindowID(window);
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.windowID = windowId;
    if (!Check(adapter.ProcessEvent(event), "touch adapter window should gain focus")) return false;

    event = {};
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.windowID = windowId;
    event.tfinger.touchID = 10;
    event.tfinger.fingerID = 100;
    event.tfinger.x = 0.25f;
    event.tfinger.y = 0.5f;
    if (!Check(adapter.ProcessEvent(event), "first finger down should be accepted")) return false;
    auto snapshots = adapter.BuildPointerSnapshots(1);
    if (!Check(snapshots.size() == 2, "mouse and one finger should produce two snapshots")) return false;
    const auto first = std::find_if(snapshots.begin(), snapshots.end(),
        [](const UIInputSnapshot& snapshot) { return snapshot.pointerId >= 0x80000000u; });
    if (!Check(first != snapshots.end() && first->primaryDown && first->primaryPressed &&
                   first->pointerPositionLogical.x == 24.0f &&
                   first->pointerPositionLogical.y == 24.0f,
               "finger snapshot should carry logical position and down edge")) return false;
    const std::uint32_t firstPointerId = first->pointerId;

    event = {};
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.windowID = windowId;
    event.tfinger.touchID = 10;
    event.tfinger.fingerID = 101;
    event.tfinger.x = 0.75f;
    event.tfinger.y = 0.25f;
    if (!Check(adapter.ProcessEvent(event), "second concurrent finger should be accepted")) return false;
    snapshots = adapter.BuildPointerSnapshots(2);
    if (!Check(snapshots.size() == 3, "two fingers and mouse should produce three snapshots")) return false;
    const auto second = std::find_if(snapshots.begin(), snapshots.end(),
        [firstPointerId](const UIInputSnapshot& snapshot) {
            return snapshot.pointerId >= 0x80000000u && snapshot.pointerId != firstPointerId;
        });
    if (!Check(second != snapshots.end() && second->primaryDown && second->primaryPressed,
               "second finger should have an independent pointer ID")) return false;
    const std::uint32_t secondPointerId = second->pointerId;

    event = {};
    event.type = SDL_EVENT_FINGER_MOTION;
    event.tfinger.windowID = windowId;
    event.tfinger.touchID = 10;
    event.tfinger.fingerID = 100;
    event.tfinger.x = 0.5f;
    event.tfinger.y = 0.5f;
    event.tfinger.dx = 0.25f;
    if (!Check(adapter.ProcessEvent(event), "finger motion should be accepted")) return false;

    event = {};
    event.type = SDL_EVENT_FINGER_UP;
    event.tfinger.windowID = windowId;
    event.tfinger.touchID = 10;
    event.tfinger.fingerID = 101;
    event.tfinger.x = 0.75f;
    event.tfinger.y = 0.25f;
    if (!Check(adapter.ProcessEvent(event), "finger up should be accepted")) return false;
    snapshots = adapter.BuildPointerSnapshots(3);
    const auto moved = std::find_if(snapshots.begin(), snapshots.end(),
        [firstPointerId](const UIInputSnapshot& snapshot) {
            return snapshot.pointerId == firstPointerId;
        });
    const auto released = std::find_if(snapshots.begin(), snapshots.end(),
        [secondPointerId](const UIInputSnapshot& snapshot) {
            return snapshot.pointerId == secondPointerId;
        });
    if (!Check(moved != snapshots.end() && moved->primaryDown &&
                   moved->pointerPositionLogical.x == 48.0f &&
                   moved->pointerDeltaLogical.x == 24.0f,
               "finger motion should convert normalized position/delta to logical pixels")) return false;
    if (!Check(released != snapshots.end() && released->primaryReleased &&
                   !released->primaryDown,
               "finger-up should emit a release snapshot")) return false;

    event = {};
    event.type = SDL_EVENT_FINGER_CANCELED;
    event.tfinger.windowID = windowId;
    event.tfinger.touchID = 10;
    event.tfinger.fingerID = 100;
    event.tfinger.x = 0.5f;
    event.tfinger.y = 0.5f;
    if (!Check(adapter.ProcessEvent(event), "finger cancellation should be accepted")) return false;
    snapshots = adapter.BuildPointerSnapshots(4);
    const auto cancelled = std::find_if(snapshots.begin(), snapshots.end(),
        [firstPointerId](const UIInputSnapshot& snapshot) {
            return snapshot.pointerId == firstPointerId;
        });
    if (!Check(cancelled != snapshots.end() && cancelled->pointerCancelled &&
                   !cancelled->primaryDown && !cancelled->primaryReleased,
               "finger cancellation must cancel rather than synthesize a click")) return false;
    if (!Check(adapter.BuildPointerSnapshots(5).size() == 1,
               "ended finger contacts should be removed after their final snapshot")) return false;

    event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = windowId;
    event.motion.which = SDL_TOUCH_MOUSEID;
    if (!Check(!adapter.ProcessEvent(event),
               "touch-generated mouse motion should not duplicate finger input")) return false;
    return true;
}

int TestClipboard() {
    const bool hadPreviousText = SDL3InputAdapter::HasClipboardText();
    std::string previousText;
    if (hadPreviousText && !SDL3InputAdapter::GetClipboardText(previousText)) {
        std::fprintf(stderr, "Could not preserve existing clipboard text: %s\n", SDL_GetError());
        return 77;
    }

    const auto restoreClipboard = [&]() {
        if (hadPreviousText) {
            return SDL3InputAdapter::SetClipboardText(previousText);
        }
        return SDL3InputAdapter::SetClipboardText({});
    };

    constexpr char testText[] = "SpriteUI clipboard \xe2\x9c\x93";
    if (!SDL3InputAdapter::SetClipboardText(testText)) {
        std::fprintf(stderr, "Clipboard backend unavailable: %s\n", SDL_GetError());
        return 77;
    }

    std::string actualText;
    const bool readSucceeded = SDL3InputAdapter::GetClipboardText(actualText);
    const bool hasText = SDL3InputAdapter::HasClipboardText();
    const bool contentMatches = readSucceeded && actualText == testText && hasText;
    if (!contentMatches) {
        std::fprintf(stderr, "Clipboard round-trip failed: %s\n", SDL_GetError());
    }

    const bool emptyTextAccepted = SDL3InputAdapter::SetClipboardText({});
    const bool emptyClipboardReported = emptyTextAccepted && !SDL3InputAdapter::HasClipboardText();
    const bool embeddedNullRejected =
        !SDL3InputAdapter::SetClipboardText(std::string_view("a\0b", 3));
    const bool restored = restoreClipboard();

    if (!contentMatches || !emptyClipboardReported || !embeddedNullRejected || !restored) {
        std::fprintf(stderr, "Clipboard contract checks failed\n");
        return 1;
    }
    return 0;
}

} // namespace

int TestWindowLifecycle() {
    std::string error;
    SDL3WindowConfig invalidConfig;
    invalidConfig.width = 0;
    if (!Check(!SDL3Window::Create(invalidConfig, error).has_value() && !error.empty(),
               "invalid window size should fail with an error")) {
        return 1;
    }

    SDL3WindowConfig config;
    config.title = "SpriteUI SDL3 input tests";
    config.width = 64;
    config.height = 64;
    config.flags = SDL_WINDOW_HIDDEN;
    auto created = SDL3Window::Create(config, error);
    if (!Check(created.has_value() && created->IsValid() && error.empty(),
               "window creation should return a valid owned window")) {
        return 1;
    }
    if (!Check(created->SetTitle("SpriteUI SDL3 test window"), "window title should be set")) return 1;
    if (!Check(!created->SetTitle(std::string_view("bad\0title", 9)),
               "window titles with embedded NUL should be rejected")) return 1;
    if (!Check(created->SetSize(96, 48), "window size should be changeable")) return 1;

    std::int32_t width = 0;
    std::int32_t height = 0;
    if (!Check(created->GetSize(width, height) && width == 96 && height == 48,
               "window size should be queryable")) return 1;
    if (!Check(!created->SetSize(-1, 0), "invalid window sizes should be rejected")) return 1;
    if (!Check(created->SetPosition(12, 20), "window position should be changeable")) return 1;
    if (!Check(created->Show() && created->Hide(), "window visibility should be controllable")) return 1;

    SDL3Window window = std::move(*created);
    if (!Check(window.IsValid() && !created->IsValid(),
               "window ownership should transfer when moved")) return 1;
    if (!Check(TestNavigationEvents(window.GetNativeHandle()),
               "navigation events should pass with the managed SDL window")) return 1;
    if (!Check(TestTextInputEvents(window.GetNativeHandle()),
               "text input events should pass with the managed SDL window")) return 1;
    if (!Check(TestTouchPointerEvents(window.GetNativeHandle()),
               "touch pointer events should be translated to frame snapshots")) return 1;
    return 0;
}

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int result = 0;
    if (argc == 2 && std::string_view(argv[1]) == "--clipboard") {
        result = TestClipboard();
    } else {
        result = TestWindowLifecycle();
    }
    SDL_Quit();
    return result;
}
