#include "SpriteUi/Widgets/WindowFrameTheme.hpp"

#include "SpriteUi/Elements/Paint.hpp"

namespace ToolEngine::SpriteUi::Widgets {

namespace {

// Medidas fixas de todas as janelas (em pixels logicos). Mudar aqui muda todas as janelas.
constexpr float kHeaderHeight = 28.0f;
constexpr float kButtonSize = 18.0f;
constexpr float kButtonGap = 4.0f;
constexpr float kButtonInset = 6.0f;
constexpr float kTabWidth = 140.0f;

// Quanto o cabecalho ativo clareia (cabecalho arrastando) e quanto o inativo escurece.
constexpr float kDraggingLift = 0.06f;
constexpr float kInactiveShade = 0.06f;

// Cor do preset vira premultiplicada (cor * alfa), como os itens de render esperam.
Vec4 Premultiplied(Vec4 color) noexcept {
    return {color.x * color.w, color.y * color.w, color.z * color.w, color.w};
}

} // namespace

WindowFrameLook MakeWindowFrameLook(StyleKind kind) noexcept {
    const StylePreset preset = GetPreset(kind);
    const StylePalette& palette = preset.palette;

    WindowFrameLook look;
    look.metrics.headerHeight = kHeaderHeight;
    look.metrics.borderWidth = preset.borderWidth;
    look.metrics.cornerRadius = preset.cornerRadius;
    look.metrics.buttonSize = kButtonSize;
    look.metrics.buttonGap = kButtonGap;
    look.metrics.buttonInset = kButtonInset;
    look.metrics.tabWidth = kTabWidth;

    look.colors.body = Premultiplied(palette.background);
    look.colors.border = Premultiplied(palette.border);
    look.colors.borderDragging = Premultiplied(Elements::LightenColor(palette.border, 0.3f));
    look.colors.headerActive = Premultiplied(palette.header);
    look.colors.headerInactive = Premultiplied(Elements::DarkenColor(palette.header, kInactiveShade));
    look.colors.headerDragging = Premultiplied(Elements::LightenColor(palette.header, kDraggingLift));
    look.colors.glyph = Premultiplied(palette.text);
    return look;
}

} // namespace ToolEngine::SpriteUi::Widgets
