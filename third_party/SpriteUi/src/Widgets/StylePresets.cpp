#include "SpriteUi/Widgets/StylePresets.hpp"

namespace ToolEngine::SpriteUi::Widgets {

namespace {

// Classic: retro, sem sombra, sem animacao.
StylePreset MakeClassic() noexcept {
    StylePreset p;
    p.kind = StyleKind::Classic;
    p.cornerRadius = 0.0f;
    p.borderWidth = 1.0f;
    p.shadow = ShadowStyle{};
    p.animationMs = 0.0f;
    p.easing = AnimationEasing::Linear;
    p.headerGradient = false;
    p.icons = false;
    return p;
}

// Modern: cantos medios, borda suave (alfa menor), sombra leve, hover curto.
StylePreset MakeModern() noexcept {
    StylePreset p;
    p.kind = StyleKind::Modern;
    p.cornerRadius = 8.0f;
    p.borderWidth = 1.0f;
    p.shadow.offset = Vec2{0.0f, 2.0f};
    p.shadow.blur = 6.0f;
    p.shadow.color = Vec4{0.0f, 0.0f, 0.0f, 0.25f};
    p.animationMs = 120.0f;
    p.easing = AnimationEasing::Linear;
    p.headerGradient = false;
    p.icons = false;
    p.palette.border = Vec4{0.40f, 0.40f, 0.46f, 0.6f};
    return p;
}

// Effects: cantos grandes, sombra maior, brilho no cabecalho, icones e animacao de botao com easing.
StylePreset MakeEffects() noexcept {
    StylePreset p;
    p.kind = StyleKind::Effects;
    p.cornerRadius = 12.0f;
    p.borderWidth = 1.0f;
    p.shadow.offset = Vec2{0.0f, 6.0f};
    p.shadow.blur = 16.0f;
    p.shadow.color = Vec4{0.0f, 0.0f, 0.0f, 0.45f};
    p.animationMs = 180.0f;
    p.easing = AnimationEasing::EaseOut;
    p.headerGradient = true;
    p.icons = true;
    p.palette.border = Vec4{0.50f, 0.50f, 0.60f, 0.5f};
    return p;
}

} // namespace

const char* StyleKindName(StyleKind kind) noexcept {
    switch (kind) {
    case StyleKind::Classic:
        return "Classic";
    case StyleKind::Effects:
        return "Effects";
    case StyleKind::Modern:
        break;
    }
    return "Modern";
}

StylePreset GetPreset(StyleKind kind) noexcept {
    switch (kind) {
    case StyleKind::Classic:
        return MakeClassic();
    case StyleKind::Effects:
        return MakeEffects();
    case StyleKind::Modern:
        break;
    }
    return MakeModern(); // Inclui valores fora do enum.
}

StylePreset ApplyPalette(const StylePreset& preset, const StylePalette& palette) noexcept {
    StylePreset out = preset;
    out.palette = palette;
    return out;
}

} // namespace ToolEngine::SpriteUi::Widgets
