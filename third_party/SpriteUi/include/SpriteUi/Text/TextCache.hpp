#pragma once

#include "SpriteUi/Text/TextLayout.hpp"

#include <string>

namespace ToolEngine::SpriteUi::Text {

struct UITextCacheComponent {
    TextLayoutResult layoutResult;
    bool isDirty = true;
    std::uint64_t contentHash = 0;

    void MarkDirty() noexcept {
        isDirty = true;
    }

    void Clear() noexcept {
        layoutResult.quads.clear();
        layoutResult.size = {0.0f, 0.0f};
        layoutResult.lineCount = 0;
        isDirty = true;
        contentHash = 0;
    }
};

} // namespace ToolEngine::SpriteUi::Text

