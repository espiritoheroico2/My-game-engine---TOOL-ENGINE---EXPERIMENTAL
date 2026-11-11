#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct SDL_Window;

namespace ToolEngine::SpriteUi::Platform {

struct SDL3WindowConfig {
    std::string title = "SpriteUI";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    std::uint64_t flags = 0;
};

class SDL3Window {
public:
    SDL3Window() noexcept = default;
    ~SDL3Window();

    SDL3Window(const SDL3Window&) = delete;
    SDL3Window& operator=(const SDL3Window&) = delete;
    SDL3Window(SDL3Window&& other) noexcept;
    SDL3Window& operator=(SDL3Window&& other) noexcept;

    [[nodiscard]] static std::optional<SDL3Window> Create(
        const SDL3WindowConfig& config, std::string& error);

    [[nodiscard]] bool IsValid() const noexcept { return window_ != nullptr && windowId_ != 0; }
    [[nodiscard]] SDL_Window* GetNativeHandle() const noexcept { return window_; }
    [[nodiscard]] std::uint32_t GetId() const noexcept { return windowId_; }

    [[nodiscard]] bool SetTitle(std::string_view title);
    [[nodiscard]] bool SetSize(std::int32_t width, std::int32_t height) noexcept;
    [[nodiscard]] bool GetSize(std::int32_t& width, std::int32_t& height) const noexcept;
    [[nodiscard]] bool SetPosition(std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] bool Show() noexcept;
    [[nodiscard]] bool Hide() noexcept;

private:
    explicit SDL3Window(SDL_Window* window, std::uint32_t windowId) noexcept
        : window_(window), windowId_(windowId) {}

    void Reset() noexcept;

    SDL_Window* window_ = nullptr;
    std::uint32_t windowId_ = 0;
};

} // namespace ToolEngine::SpriteUi::Platform
