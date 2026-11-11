#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SpriteUIVisualizer {

class BitmapImage {
public:
    [[nodiscard]] static bool LoadPng(
        const std::filesystem::path& path, BitmapImage& output, std::string& error);

    [[nodiscard]] ToolEngine::SpriteUi::Vec4 Sample(
        ToolEngine::SpriteUi::Vec2 uv) const noexcept;

    [[nodiscard]] int Width() const noexcept { return width_; }
    [[nodiscard]] int Height() const noexcept { return height_; }
    [[nodiscard]] bool IsLoaded() const noexcept { return !pixels_.empty(); }
    [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return sourcePath_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::filesystem::path sourcePath_;
    std::vector<ToolEngine::SpriteUi::Vec4> pixels_;
};

} // namespace SpriteUIVisualizer
