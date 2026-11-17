// Presets de estilo (StylePresets.hpp): formas distintas, Classic sem raio e sem animacao, troca de paleta
// que muda so cores e tipo invalido caindo em Modern.
#include "SpriteUi/Widgets/StylePresets.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Widgets;

int run = 0;
int failed = 0;

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        ++run;                                                                                               \
        if (!(expr)) {                                                                                       \
            ++failed;                                                                                        \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);                           \
        }                                                                                                    \
    } while (0)

bool SameVec4(Vec4 a, Vec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool SameVec2(Vec2 a, Vec2 b) {
    return a.x == b.x && a.y == b.y;
}

bool SameShadow(const ShadowStyle& a, const ShadowStyle& b) {
    return SameVec2(a.offset, b.offset) && a.blur == b.blur && SameVec4(a.color, b.color);
}

bool SamePalette(const StylePalette& a, const StylePalette& b) {
    return SameVec4(a.background, b.background) && SameVec4(a.header, b.header) && SameVec4(a.text, b.text) &&
           SameVec4(a.border, b.border);
}

// Compara os campos de forma (tudo que nao e cor).
bool SameShape(const StylePreset& a, const StylePreset& b) {
    return a.kind == b.kind && a.cornerRadius == b.cornerRadius && a.borderWidth == b.borderWidth &&
           SameShadow(a.shadow, b.shadow) && a.animationMs == b.animationMs && a.easing == b.easing &&
           a.headerGradient == b.headerGradient && a.icons == b.icons;
}

void TestNamesAreFixed() {
    CHECK(std::string(StyleKindName(StyleKind::Classic)) == "Classic");
    CHECK(std::string(StyleKindName(StyleKind::Modern)) == "Modern");
    CHECK(std::string(StyleKindName(StyleKind::Effects)) == "Effects");
    CHECK(std::string(StyleKindName(static_cast<StyleKind>(200))) == "Modern");
}

void TestClassicShape() {
    const StylePreset c = GetPreset(StyleKind::Classic);
    CHECK(c.kind == StyleKind::Classic);
    CHECK(c.cornerRadius == 0.0f);
    CHECK(c.borderWidth == 1.0f);
    CHECK(c.shadow.blur == 0.0f);
    CHECK(c.shadow.color.w == 0.0f); // Sem sombra: alfa zero.
    CHECK(SameVec2(c.shadow.offset, Vec2{0.0f, 0.0f}));
    CHECK(c.animationMs == 0.0f);
    CHECK(!c.headerGradient);
    CHECK(!c.icons);
}

void TestModernShape() {
    const StylePreset m = GetPreset(StyleKind::Modern);
    CHECK(m.kind == StyleKind::Modern);
    CHECK(m.cornerRadius == 8.0f);
    CHECK(m.borderWidth == 1.0f);
    CHECK(m.shadow.blur > 0.0f);
    CHECK(m.shadow.color.w > 0.0f);
    CHECK(m.animationMs == 120.0f);
    CHECK(!m.headerGradient);
    CHECK(!m.icons);
    // Borda suave: alfa menor que a borda opaca do Classic.
    CHECK(m.palette.border.w < GetPreset(StyleKind::Classic).palette.border.w);
}

void TestEffectsShape() {
    const StylePreset e = GetPreset(StyleKind::Effects);
    const StylePreset m = GetPreset(StyleKind::Modern);
    CHECK(e.kind == StyleKind::Effects);
    CHECK(e.cornerRadius == 12.0f);
    CHECK(e.shadow.blur > m.shadow.blur); // Sombra maior que a do Modern.
    CHECK(e.shadow.offset.y > m.shadow.offset.y);
    CHECK(e.animationMs == 180.0f);
    CHECK(e.easing == AnimationEasing::EaseOut);
    CHECK(e.headerGradient);
    CHECK(e.icons);
}

void TestPresetsDifferInShape() {
    const StylePreset c = GetPreset(StyleKind::Classic);
    const StylePreset m = GetPreset(StyleKind::Modern);
    const StylePreset e = GetPreset(StyleKind::Effects);
    // Cada par difere em pelo menos um campo de forma (raio, sombra ou duracao).
    CHECK(c.cornerRadius != m.cornerRadius && m.cornerRadius != e.cornerRadius &&
          c.cornerRadius != e.cornerRadius);
    CHECK(c.shadow.blur != m.shadow.blur && m.shadow.blur != e.shadow.blur);
    CHECK(c.animationMs != m.animationMs && m.animationMs != e.animationMs);
    CHECK(!SameShape(c, m) && !SameShape(m, e) && !SameShape(c, e));
}

void TestPresetsHaveDefaultPalette() {
    // Cada preset traz cores com alfa positivo; so a sombra pode ter alfa zero (Classic).
    for (StyleKind kind : {StyleKind::Classic, StyleKind::Modern, StyleKind::Effects}) {
        const StylePreset p = GetPreset(kind);
        CHECK(p.palette.background.w > 0.0f);
        CHECK(p.palette.header.w > 0.0f);
        CHECK(p.palette.text.w > 0.0f);
        CHECK(p.palette.border.w > 0.0f);
    }
}

void TestApplyPaletteChangesOnlyColors() {
    const StylePreset before = GetPreset(StyleKind::Effects);
    StylePalette fresh;
    fresh.background = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    fresh.header = Vec4{0.0f, 1.0f, 0.0f, 1.0f};
    fresh.text = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
    fresh.border = Vec4{1.0f, 1.0f, 0.0f, 1.0f};

    const StylePreset after = ApplyPalette(before, fresh);
    // Forma inteira preservada.
    CHECK(SameShape(before, after));
    // Cores trocadas.
    CHECK(SamePalette(after.palette, fresh));
    CHECK(!SamePalette(before.palette, after.palette));
    // A entrada nao e alterada.
    CHECK(SamePalette(before.palette, GetPreset(StyleKind::Effects).palette));
}

void TestApplyPaletteKeepsShapeForEveryKind() {
    StylePalette other;
    other.background = Vec4{0.5f, 0.5f, 0.5f, 1.0f};
    for (StyleKind kind : {StyleKind::Classic, StyleKind::Modern, StyleKind::Effects}) {
        const StylePreset p = GetPreset(kind);
        CHECK(SameShape(p, ApplyPalette(p, other)));
    }
}

void TestInvalidKindFallsBackToModern() {
    const auto invalid = static_cast<StyleKind>(200);
    const StylePreset p = GetPreset(invalid);
    const StylePreset m = GetPreset(StyleKind::Modern);
    CHECK(p.kind == StyleKind::Modern);
    CHECK(SameShape(p, m));
    CHECK(SamePalette(p.palette, m.palette));
}

void TestWindowDefaultStyleIsClassic() {
    // Padrao sem mudanca visual: Classic, sem paleta propria, cantos retos e sem sombra.
    UIWindowComponent w;
    CHECK(w.style == StyleKind::Classic);
    CHECK(!w.palette.has_value());
    CHECK(std::strcmp(w.title, "Window") == 0);
    const StylePreset resolved = w.ResolvedStyle();
    CHECK(SameShape(resolved, GetPreset(StyleKind::Classic)));
    CHECK(SamePalette(resolved.palette, GetPreset(StyleKind::Classic).palette));
}

void TestWindowCopyKeepsStyle() {
    UIWindowComponent w;
    w.style = StyleKind::Effects;
    StylePalette custom;
    custom.header = Vec4{0.2f, 0.4f, 0.8f, 1.0f};
    w.palette = custom;

    const UIWindowComponent copy = w;
    CHECK(copy.style == StyleKind::Effects);
    CHECK(copy.palette.has_value());
    CHECK(copy.palette.has_value() && SamePalette(*copy.palette, custom));
    CHECK(SameShape(copy.ResolvedStyle(), w.ResolvedStyle()));
    CHECK(SamePalette(copy.ResolvedStyle().palette, custom));
}

void TestWindowPaletteOverridesOnlyColors() {
    UIWindowComponent w;
    w.style = StyleKind::Modern;
    StylePalette custom;
    custom.background = Vec4{0.9f, 0.9f, 0.9f, 1.0f};
    w.palette = custom;
    const StylePreset resolved = w.ResolvedStyle();
    CHECK(SameShape(resolved, GetPreset(StyleKind::Modern)));
    CHECK(SamePalette(resolved.palette, custom));
}

} // namespace

int main() {
    TestNamesAreFixed();
    TestClassicShape();
    TestModernShape();
    TestEffectsShape();
    TestPresetsDifferInShape();
    TestPresetsHaveDefaultPalette();
    TestApplyPaletteChangesOnlyColors();
    TestApplyPaletteKeepsShapeForEveryKind();
    TestInvalidKindFallsBackToModern();
    TestWindowDefaultStyleIsClassic();
    TestWindowCopyKeepsStyle();
    TestWindowPaletteOverridesOnlyColors();
    std::printf("style presets: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
