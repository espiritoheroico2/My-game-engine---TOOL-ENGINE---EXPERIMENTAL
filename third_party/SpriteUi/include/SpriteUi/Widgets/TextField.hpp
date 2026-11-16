#pragma once

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

struct TextFieldStyle {
    Vec4 background{0.12f, 0.12f, 0.12f, 1.0f};
    Vec4 focusBackground{0.16f, 0.16f, 0.16f, 1.0f};
    Vec4 text{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 selection{0.2f, 0.4f, 0.75f, 0.7f};
    Vec4 caret{1.0f, 1.0f, 1.0f, 1.0f};
    float paddingX = 6.0f;
    float height = 28.0f;
    float fontSize = 14.0f;
};

struct TextFieldRenderTarget {
    UIHandle owner{};
    std::uint32_t textureSlice = 0;
    float zOrder = 0.0f;
    std::uint64_t paintOrder = 0;
};

class TextField {
public:
    TextField() = default;
    explicit TextField(std::string text) : edit_{std::move(text)} {}

    [[nodiscard]] TextEditState& Edit() noexcept { return edit_; }
    [[nodiscard]] const TextEditState& Edit() const noexcept { return edit_; }
    [[nodiscard]] bool Focused() const noexcept { return focused_; }
    void SetFocus(bool focused) noexcept {
        focused_ = focused;
        if (!focused) selectionDragging_ = false;
    }

    void PointerDown(Vec2 localPoint, Vec2 size, const Text::TextLayoutResult& layout, const TextFieldStyle& style) {
        moveCaret(localPoint, size, layout, style, false);
        selectionDragging_ = focused_;
    }

    void PointerMove(Vec2 localPoint, Vec2 size, const Text::TextLayoutResult& layout, const TextFieldStyle& style) {
        if (selectionDragging_ && focused_) moveCaret(localPoint, size, layout, style, true);
    }

    void PointerUp() noexcept { selectionDragging_ = false; }

    [[nodiscard]] bool DraggingSelection() const noexcept { return selectionDragging_; }

    [[nodiscard]] std::vector<Rendering::UIRenderItem> Build(
        Rect bounds,
        const Text::TextLayoutResult& textLayout,
        const TextFieldStyle& style,
        const TextFieldRenderTarget& target,
        std::vector<Rendering::ClipRectGPU>& clips) const
    {
        std::vector<Rendering::UIRenderItem> items;
        if (!IsFinite(bounds.position) || !IsFinite(bounds.size) || bounds.size.x <= 0.0f ||
            bounds.size.y <= 0.0f || !target.owner.IsValid()) {
            return items;
        }

        const auto colorFinite = [](Vec4 c) { return IsFinite(c); };
        if (!colorFinite(style.background) || !colorFinite(style.focusBackground) ||
            !colorFinite(style.text) || !colorFinite(style.selection) || !colorFinite(style.caret)) {
            return items;
        }
        clips.push_back({bounds.position.x, bounds.position.y, bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y});
        const std::uint32_t clip = static_cast<std::uint32_t>(clips.size() - 1);
        std::uint64_t paint = target.paintOrder;
        items.push_back(Solid(bounds, focused_ ? style.focusBackground : style.background, target, clip, paint++));

        const float padding = std::max(0.0f, std::isfinite(style.paddingX) ? style.paddingX : 0.0f);
        const float textY = bounds.position.y + std::max(0.0f, (bounds.size.y - textLayout.lineHeight) * 0.5f);
        const Rect textBounds{{bounds.position.x + padding, textY},
                              {std::max(0.0f, bounds.size.x - 2.0f * padding), textLayout.lineHeight}};
        const auto caretRect = [&](std::size_t offset) {
            Rect rect = Text::GetCaretRect(textLayout, static_cast<std::uint32_t>(offset));
            rect.position += textBounds.position;
            return rect;
        };

        if (focused_ && edit_.HasSelection()) {
            const auto [begin, end] = edit_.SelectionRange();
            const Rect a = caretRect(begin);
            const Rect b = caretRect(end);
            const float left = std::min(a.position.x, b.position.x);
            const float right = std::max(a.position.x, b.position.x);
            const float top = std::min(a.position.y, b.position.y);
            const float bottom = std::max(a.position.y + a.size.y, b.position.y + b.size.y);
            if (right > left && bottom > top) {
                items.push_back(Solid(Rect{{left, top}, {right - left, bottom - top}}, style.selection, target, clip, paint++));
            }
        }

        UILayoutRect textRect;
        textRect.resolvedPosition = textBounds.position;
        textRect.resolvedSize = textBounds.size;
        std::vector<Rendering::UIRenderItem> glyphs = Text::GenerateTextRenderItems(
            textLayout, textRect, style.text, target.textureSlice, clip, target.zOrder, paint, target.owner);
        paint += glyphs.size() + 1;
        items.insert(items.end(), glyphs.begin(), glyphs.end());

        if (focused_) {
            const Rect caret = caretRect(edit_.Caret());
            const float caretWidth = 1.0f;
            const float caretHeight = std::min(caret.size.y, std::max(0.0f, bounds.position.y + bounds.size.y - caret.position.y));
            if (caretHeight > 0.0f && caret.position.x >= bounds.position.x &&
                caret.position.x <= bounds.position.x + bounds.size.x) {
                items.push_back(Solid(Rect{{caret.position.x, caret.position.y}, {caretWidth, caretHeight}},
                                      style.caret, target, clip, paint));
            }
        }
        return items;
    }

private:
    void moveCaret(Vec2 localPoint, Vec2 size, const Text::TextLayoutResult& layout, const TextFieldStyle& style, bool extend) {
        const float verticalPadding = std::max(0.0f, (size.y - layout.lineHeight) * 0.5f);
        const Vec2 textPoint{localPoint.x - std::max(0.0f, style.paddingX), localPoint.y - verticalPadding};
        edit_.MoveCaretTo(Text::FindInsertionOffset(layout, textPoint), extend);
    }

    static Rendering::UIRenderItem Solid(
        Rect bounds, Vec4 color, const TextFieldRenderTarget& target, std::uint32_t clip, std::uint64_t paint)
    {
        Rendering::UIRenderItem item;
        item.handle = target.owner;
        item.layout.resolvedPosition = bounds.position;
        item.layout.resolvedSize = bounds.size;
        item.tintPremultipliedLinear = color;
        item.textureSlice = Rendering::WhiteTextureSlice;
        item.clipIndex = clip;
        item.zOrder = target.zOrder;
        item.paintOrder = paint;
        return item;
    }

    TextEditState edit_;
    bool focused_ = false;
    bool selectionDragging_ = false;
};

} // namespace ToolEngine::SpriteUi::Widgets
