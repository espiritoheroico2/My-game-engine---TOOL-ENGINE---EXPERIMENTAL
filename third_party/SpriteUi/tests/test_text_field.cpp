// Camada L4 (campo de texto generico): rolagem que segue o caret, recorte a caixa e clique com rolagem.
// Usa um shaper sintetico (um glifo por byte, avanco fixo) para nao depender de fonte nem de stb.
#include "SpriteUi/Widgets/TextField.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"
#include "SyntheticTextLayout.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using ToolEngine::SpriteUi::Widgets::TextField;
using ToolEngine::SpriteUi::Widgets::TextFieldRenderTarget;
using ToolEngine::SpriteUi::Widgets::TextFieldStyle;
using ToolEngine::SpriteUi::Widgets::ToggleStyle;
using ToolEngine::SpriteUi::Widgets::UIToggleComponent;
namespace Text = ToolEngine::SpriteUi::Text;
namespace Rendering = ToolEngine::SpriteUi::Rendering;
using SyntheticText::Layout;

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

bool Near(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

bool SameColor(Vec4 a, Vec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

constexpr Rect kBox{{0.0f, 40.0f}, {200.0f, 30.0f}};
constexpr float kPad = 8.0f;

TextFieldRenderTarget Target() {
    TextFieldRenderTarget t;
    t.owner = UIHandle{7, 1};
    return t;
}

// Constroi o campo com o texto dado e o foco, com um recorte por quadro (o vetor de clips e descartado).
std::vector<Rendering::UIRenderItem> Frame(TextField& field, const TextFieldStyle& style) {
    std::vector<Rendering::ClipRectGPU> clips;
    const auto layout = Layout(field.Edit().Text());
    return field.Build(kBox, layout, style, Target(), clips);
}

// Item do caret: cor do caret e largura do caret.
const Rendering::UIRenderItem* FindCaret(const std::vector<Rendering::UIRenderItem>& items,
                                         const TextFieldStyle& s) {
    for (const auto& item : items) {
        if (SameColor(item.tintPremultipliedLinear, s.caret) &&
            Near(item.layout.resolvedSize.x, s.caretWidth)) {
            return &item;
        }
    }
    return nullptr;
}

void TestCaretStaysVisibleWhileTyping() {
    TextFieldStyle style;
    TextField field;
    field.SetFocus(true);
    const float viewport = kBox.size.x - 2.0f * style.paddingX;
    std::string typed;
    bool allVisible = true;
    for (int i = 0; i < 60; ++i) {
        field.Edit().InsertText("W");
        typed += "W";
        const auto items = Frame(field, style);
        const auto* caret = FindCaret(items, style);
        if (!caret) {
            allVisible = false;
            break;
        }
        const float left = caret->layout.resolvedPosition.x;
        const float right = left + caret->layout.resolvedSize.x;
        // A area util vai de paddingX ate paddingX + viewport, dentro da caixa.
        if (left < kBox.position.x + style.paddingX - 1e-3f ||
            right > kBox.position.x + style.paddingX + viewport + 1e-3f) {
            allVisible = false;
        }
    }
    CHECK(allVisible);
    CHECK(field.ScrollX() > 0.0f);
    CHECK(Near(field.ScrollX(), 60 * 10.0f + style.caretWidth - viewport));
}

void TestHomeReturnsScrollToZero() {
    TextFieldStyle style;
    TextField field(std::string(60, 'a'));
    field.SetFocus(true);
    Frame(field, style);
    CHECK(field.ScrollX() > 0.0f);

    field.Edit().MoveHome();
    const auto items = Frame(field, style);
    CHECK(Near(field.ScrollX(), 0.0f));
    const auto* caret = FindCaret(items, style);
    CHECK(caret != nullptr);
    if (caret) CHECK(Near(caret->layout.resolvedPosition.x, kBox.position.x + style.paddingX));
}

void TestCaretAtEndOfLongTextIsScrolled() {
    TextFieldStyle style;
    TextField field(std::string(60, 'a'));
    field.SetFocus(true);
    const auto items = Frame(field, style);
    const float viewport = kBox.size.x - 2.0f * style.paddingX;
    // Caret no fim: x = 600; a rolagem deixa a borda direita do caret na borda da area util.
    CHECK(Near(field.ScrollX(), 600.0f + style.caretWidth - viewport));
    const auto* caret = FindCaret(items, style);
    CHECK(caret != nullptr);
    if (caret)
        CHECK(Near(caret->layout.resolvedPosition.x + caret->layout.resolvedSize.x,
                   kBox.position.x + kBox.size.x - style.paddingX));
}

void TestUnfocusedFieldDoesNotScroll() {
    TextFieldStyle style;
    TextField shortField("abc");
    Frame(shortField, style);
    CHECK(Near(shortField.ScrollX(), 0.0f));

    // Campo sem foco com texto longo tambem mostra o inicio.
    TextField longField(std::string(60, 'a'));
    longField.SetFocus(true);
    Frame(longField, style);
    CHECK(longField.ScrollX() > 0.0f);
    longField.SetFocus(false);
    Frame(longField, style);
    CHECK(Near(longField.ScrollX(), 0.0f));
}

void TestMovingCaretLeftScrollsBack() {
    TextFieldStyle style;
    TextField field(std::string(60, 'a'));
    field.SetFocus(true);
    Frame(field, style);
    const float viewport = kBox.size.x - 2.0f * style.paddingX;
    bool visible = true;
    for (int i = 0; i < 60; ++i) {
        field.Edit().MoveLeft();
        const auto items = Frame(field, style);
        const auto* caret = FindCaret(items, style);
        if (!caret) {
            visible = false;
            break;
        }
        const float left = caret->layout.resolvedPosition.x;
        if (left < kBox.position.x + style.paddingX - 1e-3f ||
            left + style.caretWidth > kBox.position.x + style.paddingX + viewport + 1e-3f) {
            visible = false;
        }
    }
    CHECK(visible);
    CHECK(Near(field.ScrollX(), 0.0f));
}

void TestClickConvertsToOffsetWithScroll() {
    TextFieldStyle style;
    TextField field(std::string(60, 'a'));
    field.SetFocus(true);
    Frame(field, style);
    const float scroll = field.ScrollX();
    CHECK(scroll > 0.0f);

    // Clique sobre o caractere 10 (x = 100 no layout), descontando a rolagem atual e o recuo.
    const auto layout = Layout(field.Edit().Text());
    const Vec2 local{style.paddingX + 100.0f - scroll + 2.0f, kBox.size.y * 0.5f};
    field.PointerDown(local, kBox.size, layout, style);
    CHECK(field.Edit().Caret() == 10);
    CHECK(!field.Edit().HasSelection());

    // Clique dentro da area util ja visivel: o caret anda para frente e a rolagem nao muda sem necessidade.
    const float beforeRight = field.ScrollX();
    const Vec2 right{kBox.size.x - style.paddingX, kBox.size.y * 0.5f};
    field.PointerDown(right, kBox.size, layout, style);
    CHECK(field.Edit().Caret() > 10);
    CHECK(Near(field.ScrollX(), beforeRight));
}

void TestClickFocusesField() {
    TextFieldStyle style;
    TextField field("abc");
    CHECK(!field.Focused());
    const auto layout = Layout(field.Edit().Text());
    field.PointerDown({style.paddingX, 5.0f}, kBox.size, layout, style);
    CHECK(field.Focused());
    CHECK(field.Edit().Caret() == 0);
}

void TestTextSelectionAndCaretUseBoxClip() {
    TextFieldStyle style;
    TextField field(std::string(60, 'a'));
    field.SetFocus(true);
    field.Edit().SelectAll();
    std::vector<Rendering::ClipRectGPU> clips{{-100.0f, -100.0f, 900.0f, 900.0f}}; // Recorte de outro no.
    const auto items = field.Build(kBox, Layout(field.Edit().Text()), style, Target(), clips);

    CHECK(clips.size() == 2);
    CHECK(Near(clips.back().minX, kBox.position.x) && Near(clips.back().minY, kBox.position.y));
    CHECK(Near(clips.back().maxX, kBox.Max().x) && Near(clips.back().maxY, kBox.Max().y));

    const auto clip = static_cast<std::uint32_t>(clips.size() - 1);
    std::size_t clipped = 0;
    std::size_t unclipped = 0;
    for (const auto& item : items) {
        if (item.clipIndex == clip) {
            ++clipped;
        } else if (item.clipIndex == Rendering::NoClipIndex) {
            ++unclipped;
        }
    }
    // Fundo sem recorte; selecao, 60 glifos e caret com o recorte da caixa.
    CHECK(unclipped == 1);
    CHECK(clipped == items.size() - 1);
    CHECK(clipped >= 62);
}

void TestWideFieldKeepsTextUnscrolled() {
    TextFieldStyle style;
    TextField field("abc");
    field.SetFocus(true);
    Frame(field, style);
    CHECK(Near(field.ScrollX(), 0.0f));
    const float tiny = 2.0f * style.paddingX - 3.0f; // Caixa mais estreita que os recuos.
    CHECK(TextField::ViewportWidth({tiny, 30.0f}, style) == 0.0f);
    CHECK(Near(TextField::ViewportWidth(kBox.size, style), kBox.size.x - 2.0f * style.paddingX));
}

// Defaults de estilo (Widgets/Style.hpp) e troca de estilo sem mudar a logica do widget.
void TestStyleDefaultsAndSwap() {
    const TextFieldStyle s;
    CHECK(Near(s.paddingX, 8.0f));
    CHECK(Near(s.height, 30.0f));
    CHECK(Near(s.fontSize, 16.0f));
    CHECK(Near(s.caretWidth, 1.5f));
    CHECK(!s.fontId.IsValid());          // id zero: usa a fonte padrao do FontRegistry
    CHECK(SameColor(s.caret, Vec4{0.95f, 0.95f, 1.0f, 1.0f}));

    // Toggle: o padrao de UIToggleComponent vem de ToggleStyle (mesmos valores de antes).
    const ToggleStyle t;
    CHECK(SameColor(t.onColor, Vec4{0.2f, 0.8f, 0.2f, 1.0f}));
    CHECK(SameColor(t.offColor, Vec4{0.5f, 0.5f, 0.5f, 1.0f}));
    const UIToggleComponent toggle;
    CHECK(SameColor(toggle.onColor, t.onColor));
    CHECK(SameColor(toggle.offColor, t.offColor));

    // Trocar a cor de fundo muda o item de fundo; a logica (rolagem, recorte) continua a mesma.
    TextFieldStyle custom;
    custom.background = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    TextField field("abc");
    std::vector<Rendering::ClipRectGPU> clips;
    const auto items = field.Build(kBox, Layout("abc"), custom, Target(), clips);
    CHECK(!items.empty());
    if (!items.empty()) CHECK(SameColor(items.front().tintPremultipliedLinear, custom.background));
}

} // namespace

// Arrasto com o mouse: PointerDown com foco inicia a selecao; PointerMove estende; PointerUp encerra.
// Sem foco, o arrasto nao comeca. Perder o foco cancela o arrasto em andamento.
void TestDragSelectionFollowsPointer() {
    TextFieldStyle style;
    TextField field;
    field.Edit().InsertText("abcdefghij");
    const auto layout = Layout(field.Edit().Text());

    field.SetFocus(false);
    field.PointerDown({0.0f, 5.0f}, kBox.size, layout, style);
    CHECK(!field.DraggingSelection());   // sem foco nao arrasta

    field.SetFocus(true);
    field.PointerDown({0.0f, 5.0f}, kBox.size, layout, style);   // ancora no inicio, com foco
    CHECK(field.DraggingSelection());
    field.PointerMove({60.0f, 5.0f}, kBox.size, layout, style);  // estende ate o meio
    CHECK(field.Edit().HasSelection());
    field.PointerUp();
    CHECK(!field.DraggingSelection());

    field.PointerDown({0.0f, 5.0f}, kBox.size, layout, style);
    CHECK(field.DraggingSelection());
    field.SetFocus(false);                                        // perder foco cancela o arrasto
    CHECK(!field.DraggingSelection());
}

int main() {
    TestCaretStaysVisibleWhileTyping();
    TestHomeReturnsScrollToZero();
    TestCaretAtEndOfLongTextIsScrolled();
    TestUnfocusedFieldDoesNotScroll();
    TestMovingCaretLeftScrollsBack();
    TestClickConvertsToOffsetWithScroll();
    TestClickFocusesField();
    TestTextSelectionAndCaretUseBoxClip();
    TestWideFieldKeepsTextUnscrolled();
    TestStyleDefaultsAndSwap();
    TestDragSelectionFollowsPointer();
    std::printf("text field: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
