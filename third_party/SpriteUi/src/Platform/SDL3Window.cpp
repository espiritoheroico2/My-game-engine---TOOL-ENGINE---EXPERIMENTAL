#include "SpriteUi/Platform/SDL3Window.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace ToolEngine::SpriteUi::Platform {

SDL3Window::~SDL3Window() {
    Reset();
}

SDL3Window::SDL3Window(SDL3Window&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      windowId_(std::exchange(other.windowId_, 0)) {}

SDL3Window& SDL3Window::operator=(SDL3Window&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    window_ = std::exchange(other.window_, nullptr);
    windowId_ = std::exchange(other.windowId_, 0);
    return *this;
}

std::optional<SDL3Window> SDL3Window::Create(
    const SDL3WindowConfig& config, std::string& error) {
    error.clear();
    if (config.width <= 0 || config.height <= 0) {
        error = "SDL3 window dimensions must be positive";
        return std::nullopt;
    }

    SDL_Window* window = SDL_CreateWindow(
        config.title.c_str(), config.width, config.height,
        static_cast<SDL_WindowFlags>(config.flags));
    if (!window) {
        error = SDL_GetError();
        if (error.empty()) error = "SDL_CreateWindow failed without error details";
        return std::nullopt;
    }

    const std::uint32_t windowId = SDL_GetWindowID(window);
    if (windowId == 0) {
        error = SDL_GetError();
        if (error.empty()) error = "SDL_CreateWindow returned a window without a valid ID";
        SDL_DestroyWindow(window);
        return std::nullopt;
    }

    return SDL3Window(window, windowId);
}

bool SDL3Window::SetTitle(std::string_view title) {
    if (!window_ || title.find('\0') != std::string_view::npos) return false;
    const std::string nullTerminated(title);
    return SDL_SetWindowTitle(window_, nullTerminated.c_str());
}

bool SDL3Window::SetSize(std::int32_t width, std::int32_t height) noexcept {
    return window_ && width > 0 && height > 0 && SDL_SetWindowSize(window_, width, height);
}

bool SDL3Window::GetSize(std::int32_t& width, std::int32_t& height) const noexcept {
    if (!window_) return false;
    int currentWidth = 0;
    int currentHeight = 0;
    if (!SDL_GetWindowSize(window_, &currentWidth, &currentHeight)) return false;
    width = currentWidth;
    height = currentHeight;
    return true;
}

bool SDL3Window::SetPosition(std::int32_t x, std::int32_t y) noexcept {
    return window_ && SDL_SetWindowPosition(window_, x, y);
}

bool SDL3Window::Show() noexcept {
    return window_ && SDL_ShowWindow(window_);
}

bool SDL3Window::Hide() noexcept {
    return window_ && SDL_HideWindow(window_);
}

void SDL3Window::Reset() noexcept {
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    windowId_ = 0;
}

} // namespace ToolEngine::SpriteUi::Platform
