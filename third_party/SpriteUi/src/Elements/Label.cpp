#include "SpriteUi/Elements/Label.hpp"

namespace ToolEngine::SpriteUi::Elements {

namespace {

float AlignFactor(HorizontalAlign align) noexcept {
    switch (align) {
    case HorizontalAlign::Left: return 0.0f;
    case HorizontalAlign::Center: return 0.5f;
    case HorizontalAlign::Right: return 1.0f;
    }
    return 0.0f;
}

float AlignFactor(VerticalAlign align) noexcept {
    switch (align) {
    case VerticalAlign::Top: return 0.0f;
    case VerticalAlign::Middle: return 0.5f;
    case VerticalAlign::Bottom: return 1.0f;
    }
    return 0.0f;
}

// Origem do layout do texto dentro da area util. Com texto mais largo que a area a folga e negativa: o texto
// sai pelo lado indicado pelo alinhamento e o recorte corta a parte de fora.
Vec2 AlignedTextOrigin(const Label& label, const Text::TextLayoutResult& layout, Rect content) noexcept {
    const float freeWidth = content.size.x - layout.size.x;
    const float freeHeight = content.size.y - layout.lineHeight;
    return {content.position.x + AlignFactor(label.horizontal) * freeWidth,
            content.position.y + AlignFactor(label.vertical) * freeHeight};
}

} // namespace

Text::TextLayoutConfig MakeLabelLayoutConfig(const Label& label) noexcept {
    Text::TextLayoutConfig config;
    config.fontSize = label.fontSize;
    config.maxWidth = 0.0f;
    config.wrap = Text::TextWrap::None;
    config.alignment = Text::TextAlignment::Left;
    return config;
}

std::vector<Rendering::UIRenderItem> BuildLabelItems(const Label& label, const Text::TextLayoutResult& layout,
                                                     Rect content, const TextTarget& target,
                                                     std::uint32_t clipIndex) {
    if (!IsFinite(content.position) || !IsFinite(content.size) || !IsFinite(layout.size) ||
        !IsFinite(layout.lineHeight)) {
        return {};
    }
    UILayoutRect textBox;
    textBox.resolvedPosition = AlignedTextOrigin(label, layout, content);
    textBox.resolvedSize = layout.size;
    return Text::GenerateTextRenderItems(layout, textBox, label.color, target.textureSlice, clipIndex,
                                         target.zOrder, target.paintOrder, label.handle, target.renderFlags);
}

} // namespace ToolEngine::SpriteUi::Elements
