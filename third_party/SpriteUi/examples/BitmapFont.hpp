#pragma once

#include "SpriteUi/Text/FontMetrics.hpp"
#include "SpriteUi/Text/GlyphInstance.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SpriteUIVisualizer {

class BitmapFont {
public:
    static constexpr std::uint32_t TextureSlice = 1;

    [[nodiscard]] static bool Load(
        const std::filesystem::path& path,
        BitmapFont& font,
        std::string& error);

    [[nodiscard]] std::vector<ToolEngine::SpriteUi::Text::GlyphInstance> Shape(
        std::string_view utf8) const;

    [[nodiscard]] ToolEngine::SpriteUi::Vec4 Sample(
        std::uint32_t slice,
        ToolEngine::SpriteUi::Vec2 uv) const noexcept;

    [[nodiscard]] const ToolEngine::SpriteUi::Text::FontMetrics& Metrics() const noexcept {
        return metrics_;
    }

    [[nodiscard]] const std::string& FamilyName() const noexcept { return familyName_; }
    [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return sourcePath_; }

private:
    ToolEngine::SpriteUi::Text::FontMetrics metrics_{};
    std::filesystem::path sourcePath_;
    std::string familyName_;
    std::vector<float> coverage_;
    std::vector<ToolEngine::SpriteUi::Text::GlyphInstance> glyphs_;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;
};

} // namespace SpriteUIVisualizer
