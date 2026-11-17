// Area de texto de varias linhas (TextArea.hpp): quebra de linha, layout que se refaz so quando muda,
// subir e descer mantendo a coluna, inicio e fim da linha, rolagem que segue o caret e recorte por linhas
// visiveis. Usa o layout sintetico (um glifo por byte) e nao abre janela.
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Widgets/TextArea.hpp"
#include "SyntheticTextLayout.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using ToolEngine::SpriteUi::Widgets::TextArea;
using ToolEngine::SpriteUi::Widgets::TextAreaStyle;
using ToolEngine::SpriteUi::Widgets::TextFieldRenderTarget;
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

// Caixa de 200 x 120 com recuo 8 (horizontal) e 6 (vertical): area util de 184 x 108.
constexpr Rect kBox{{0.0f, 40.0f}, {200.0f, 120.0f}};

TextAreaStyle Style() {
    return TextAreaStyle{};
}

// Espacamento que torna a linha de 20 px (com as metricas sinteticas, 0,4 px sem multiplicador).
constexpr float kLineSpacing = 50.0f;

// O chamador refaz o layout quando a area pede, como faria a cada quadro antes de tratar entrada.
void Sync(TextArea& area) {
    const float width = TextArea::ContentWidth(kBox.size, Style());
    if (area.NeedsLayout(width)) {
        area.SetLayout(Layout(area.Text(), 20.0f, width, Text::TextWrap::Word, kLineSpacing), width);
    }
}

std::vector<Rendering::UIRenderItem> Frame(TextArea& area) {
    Sync(area);
    TextFieldRenderTarget target;
    target.owner = UIHandle{9, 1};
    std::vector<Rendering::ClipRectGPU> clips;
    return area.Build(kBox, Style(), target, clips);
}

std::size_t CountWithTint(const std::vector<Rendering::UIRenderItem>& items, Vec4 tint) {
    std::size_t n = 0;
    for (const auto& item : items) {
        if (SameColor(item.tintPremultipliedLinear, tint)) ++n;
    }
    return n;
}

std::size_t CountGlyphs(const std::vector<Rendering::UIRenderItem>& items) {
    std::size_t n = 0;
    for (const auto& item : items) {
        if ((item.flags & (Rendering::FlagMsdfText | Rendering::FlagBitmapText)) != 0u) ++n;   // Glifo de texto, qualquer modo.
    }
    return n;
}

void TestLayoutIsRefreshedOnlyWhenTextOrWidthChange() {
    TextArea area(std::string("ab"));
    const float width = TextArea::ContentWidth(kBox.size, Style());
    CHECK(area.NeedsLayout(width)); // ainda sem layout
    Sync(area);
    CHECK(!area.NeedsLayout(width));

    area.MoveLeft();
    area.MoveRight();
    area.SelectAll();
    CHECK(!area.NeedsLayout(width)); // mover e selecionar nao pede layout novo

    CHECK(area.Backspace()); // apaga a selecao: o texto mudou
    CHECK(area.NeedsLayout(width));
    Sync(area);
    CHECK(area.NeedsLayout(width + 10.0f)); // largura diferente tambem pede layout
}

void TestBackspaceAtStartChangesNothing() {
    TextArea area;
    Sync(area);
    const float width = TextArea::ContentWidth(kBox.size, Style());
    CHECK(!area.Backspace());
    CHECK(!area.NeedsLayout(width)); // nada mudou: o layout continua valendo
}

void TestEnterInsertsNewlineAndUndoRestores() {
    TextArea area(std::string("ab"));
    area.InsertNewline();
    CHECK(area.Text() == "ab\n");
    CHECK(area.Edit().Caret() == 3);
    Sync(area);
    // A quebra final nao cria linha na lista: o caret fica na linha vazia de baixo, uma altura abaixo.
    CHECK(area.Layout().lines.size() == 1);
    CHECK(Near(Text::GetCaretRect(area.Layout(), 3).position.y, area.Layout().lineHeight));

    CHECK(area.Undo());
    CHECK(area.Text() == "ab");
    CHECK(area.NeedsLayout(TextArea::ContentWidth(kBox.size, Style())));
}

void TestSoftWrapBreaksWordsAcrossLines() {
    // Largura de 60 px com 10 px por caractere: seis caracteres por linha, entao cada palavra de quatro
    // letras ocupa a sua propria linha.
    TextArea area(std::string("aaaa bbbb cccc"));
    const float width = 60.0f;
    area.SetLayout(Layout(area.Text(), 20.0f, width, Text::TextWrap::Word), width);
    CHECK(area.Layout().lines.size() == 3);
}

void TestUpAndDownKeepColumnAcrossLines() {
    TextArea area(std::string("abcdef\nxyz")); // caret comeca no fim: linha "xyz", coluna 3
    Sync(area);
    CHECK(area.Edit().Caret() == 10);

    CHECK(area.MoveUp());
    CHECK(area.Edit().Caret() == 3); // "abc|def", mesma coluna na linha de cima
    CHECK(area.MoveDown());
    CHECK(area.Edit().Caret() == 10);
    CHECK(!area.MoveDown()); // ja na ultima linha: nada muda
}

// A coluna sobrevive a uma linha curta: "abcdef" / "xy" / "abcdef", caret na coluna 5 da ultima linha. Subir
// passa pela linha curta e volta a coluna 5 da primeira; descer refaz o caminho.
void TestColumnSurvivesShortLine() {
    TextArea area(std::string("abcdef\nxy\nabcdef")); // caret no fim: offset 16
    Sync(area);
    area.MoveLeft();
    CHECK(area.Edit().Caret() == 15); // "abcde|f" da ultima linha

    CHECK(area.MoveUp());
    CHECK(area.Edit().Caret() >= 7 && area.Edit().Caret() <= 9); // "xy", no fim da linha curta
    CHECK(area.MoveUp());
    CHECK(area.Edit().Caret() == 5); // "abcde|f" da primeira linha
    CHECK(area.MoveDown());
    CHECK(area.MoveDown());
    CHECK(area.Edit().Caret() == 15);
}

void TestUpFromFirstLineGoesToStart() {
    TextArea area(std::string("abc\ndef"));
    Sync(area);
    area.MoveHome();
    CHECK(area.Edit().Caret() == 4);
    CHECK(area.MoveUp());
    CHECK(area.Edit().Caret() == 0);
    CHECK(!area.MoveUp());
}

void TestHomeAndEndStayInsideLine() {
    TextArea area(std::string("abcdef\nxyz"));
    Sync(area);
    area.MoveHome();
    CHECK(area.Edit().Caret() == 7); // inicio da linha "xyz"
    area.MoveEnd();
    CHECK(area.Edit().Caret() == 10); // fim do texto, sem quebra no fim
    CHECK(area.MoveUp());
    CHECK(area.Edit().Caret() == 3); // mesma coluna (3) na linha de cima
    area.MoveHome();
    CHECK(area.Edit().Caret() == 0);
    area.MoveEnd();
    CHECK(area.Edit().Caret() == 6); // antes da quebra de linha
}

void TestPointerDownPlacesCaretAndFocuses() {
    TextArea area(std::string("abc\ndef"));
    Sync(area);
    const float lineHeight = area.Layout().lineHeight;
    const TextAreaStyle style = Style();
    area.PointerDown({style.paddingX + 10.0f, style.paddingY + 1.5f * lineHeight}, kBox.size, style);
    CHECK(area.Focused());
    CHECK(area.Edit().Caret() == 5); // segunda linha, depois do primeiro caractere ("d|ef")
}

void TestCaretFollowsScrollAndOnlyVisibleLinesAreDrawn() {
    std::string text;
    for (int i = 0; i < 40; ++i)
        text += "x\n";
    TextArea area(text);
    area.SetFocus(true);
    const std::vector<Rendering::UIRenderItem> items = Frame(area);

    const float viewport = kBox.size.y - 2.0f * Style().paddingY;
    const Rect caret = Text::GetCaretRect(area.Layout(), static_cast<std::uint32_t>(area.Edit().Caret()));
    CHECK(area.ScrollY() > 0.0f);
    CHECK(caret.position.y - area.ScrollY() >= -1.0e-3f);
    CHECK(caret.position.y + caret.size.y - area.ScrollY() <= viewport + 1.0e-3f);

    // Quarenta linhas com glifo: so as poucas que cabem na caixa geram itens de texto.
    const float lineHeight = area.Layout().lineHeight;
    const auto maxVisible = static_cast<std::size_t>(viewport / lineHeight) + 2;
    CHECK(CountGlyphs(items) <= maxVisible);
    CHECK(CountGlyphs(items) < 40);
}

void TestScrollResetsWithoutFocus() {
    std::string text;
    for (int i = 0; i < 40; ++i)
        text += "x\n";
    TextArea area(text);
    area.SetFocus(true);
    Frame(area);
    CHECK(area.ScrollY() > 0.0f);
    area.SetFocus(false);
    Frame(area);
    CHECK(area.ScrollY() == 0.0f);
}

void TestSelectionIsDrawnOncePerLine() {
    TextArea area(std::string("abc\ndef\nghi"));
    area.SetFocus(true);
    area.SelectAll();
    const TextAreaStyle style = Style();
    const std::vector<Rendering::UIRenderItem> items = Frame(area);
    CHECK(CountWithTint(items, style.selection) == 3);
}

} // namespace

int main() {
    TestLayoutIsRefreshedOnlyWhenTextOrWidthChange();
    TestBackspaceAtStartChangesNothing();
    TestEnterInsertsNewlineAndUndoRestores();
    TestSoftWrapBreaksWordsAcrossLines();
    TestUpAndDownKeepColumnAcrossLines();
    TestColumnSurvivesShortLine();
    TestUpFromFirstLineGoesToStart();
    TestHomeAndEndStayInsideLine();
    TestPointerDownPlacesCaretAndFocuses();
    TestCaretFollowsScrollAndOnlyVisibleLinesAreDrawn();
    TestScrollResetsWithoutFocus();
    TestSelectionIsDrawnOncePerLine();

    std::printf("TextArea: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
