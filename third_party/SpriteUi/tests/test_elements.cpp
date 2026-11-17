// Base de elementos: geometria da caixa (padding, content rect, margem, ancoras, min e max) e arvore com
// handles de geracao. Os valores esperados foram calculados a mao e estao nos comentarios.
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/Toggle.hpp"
#include "SpriteUi/Elements/Label.hpp"
#include "SpriteUi/Elements/TextField.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SyntheticTextLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Elements;

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

bool Near(float a, float b) {
    return std::fabs(a - b) <= 1.0e-4f;
}

bool SameRect(Rect actual, Rect expected) {
    return Near(actual.position.x, expected.position.x) && Near(actual.position.y, expected.position.y) &&
           Near(actual.size.x, expected.size.x) && Near(actual.size.y, expected.size.y);
}

// Elemento de teste sem comportamento: so existe para a arvore.
class Box final : public Element {};

void TestShrinkRect() {
    // Caixa (10,20) 100x50, padding topo 5, direita 10, base 5, esquerda 20.
    // Area util: x = 10 + 20 = 30, y = 20 + 5 = 25, largura = 100 - 30 = 70, altura = 50 - 10 = 40.
    CHECK(SameRect(ShrinkRect({{10, 20}, {100, 50}}, {5, 10, 5, 20}), {{30, 25}, {70, 40}}));

    // Padding maior que a caixa: tamanho zero, nunca negativo.
    CHECK(SameRect(ShrinkRect({{0, 0}, {10, 10}}, {20, 20, 20, 20}), {{20, 20}, {0, 0}}));

    // Insets negativos ou nao finitos contam como zero.
    CHECK(SameRect(ShrinkRect({{0, 0}, {10, 10}}, {-5, std::numeric_limits<float>::quiet_NaN(), 0, 0}),
                   {{0, 0}, {10, 10}}));

    // Caixa com tamanho nao finito vira vazia.
    CHECK(SameRect(ShrinkRect({{0, 0}, {std::numeric_limits<float>::infinity(), 10}}, {}), {}));
}

void TestClampSize() {
    // Largura 5 sobe ao minimo 10; altura 500 desce ao maximo 200.
    const Vec2 clamped = ClampSize({5, 500}, {10, 0}, {100, 200});
    CHECK(Near(clamped.x, 10.0f));
    CHECK(Near(clamped.y, 200.0f));

    // Minimo maior que maximo: vale o maximo.
    CHECK(Near(ClampSize({50, 0}, {80, 0}, {60, NoMaxSize}).x, 60.0f));

    // Entrada nao finita vira zero; minimo negativo nao gera tamanho negativo.
    CHECK(Near(ClampSize({std::numeric_limits<float>::quiet_NaN(), 0}, {0, 0}, {10, 10}).x, 0.0f));
    CHECK(Near(ClampSize({-30, 0}, {-5, 0}, {10, 10}).x, 0.0f));
}

void TestArrangeAnchorsAndOffsets() {
    Element element;
    element.transform.anchorMin = {0.0f, 0.0f};
    element.transform.anchorMax = {1.0f, 1.0f};
    element.transform.offsetMin = {10.0f, 20.0f};
    element.transform.offsetMax = {-10.0f, -20.0f};
    // Pai 800x600: minimo (10,20), maximo (790,580), tamanho 780x560.
    CHECK(SameRect(element.ArrangeIn({{0, 0}, {800, 600}}), {{10, 20}, {780, 560}}));

    // Ancora no centro com offsets simetricos: tamanho fixo 100x40 centralizado em (100,50).
    Element centered;
    centered.transform.anchorMin = {0.5f, 0.5f};
    centered.transform.anchorMax = {0.5f, 0.5f};
    centered.transform.offsetMin = {-50.0f, -20.0f};
    centered.transform.offsetMax = {50.0f, 20.0f};
    CHECK(SameRect(centered.ArrangeIn({{0, 0}, {200, 100}}), {{50, 30}, {100, 40}}));

    // Offsets invertidos (maximo antes do minimo): tamanho zero no eixo, nunca retangulo negativo.
    Element inverted;
    inverted.transform.anchorMax = {0.0f, 0.0f};
    inverted.transform.offsetMin = {30.0f, 0.0f};
    inverted.transform.offsetMax = {10.0f, 40.0f};
    CHECK(Near(inverted.ArrangeIn({{0, 0}, {200, 100}}).size.x, 0.0f));

    // Ancoras invalidas (minimo maior que maximo) geram caixa vazia.
    Element invalid;
    invalid.transform.anchorMin = {0.8f, 0.0f};
    invalid.transform.anchorMax = {0.2f, 1.0f};
    CHECK(SameRect(invalid.ArrangeIn({{0, 0}, {200, 100}}), {}));
}

void TestArrangeMarginAndMinMax() {
    // Margem 10 nos quatro lados, ancora esticada: pai 200x100 vira area (10,10) 180x80.
    Element withMargin;
    withMargin.transform.anchorMax = {1.0f, 1.0f};
    withMargin.transform.offsetMax = {0.0f, 0.0f};
    withMargin.margin = {10, 10, 10, 10};
    CHECK(SameRect(withMargin.ArrangeIn({{0, 0}, {200, 100}}), {{10, 10}, {180, 80}}));

    // Largura 180 com minimo 150 e maximo 120: minimo maior que maximo, entao vale o maximo (120).
    withMargin.maxSize = {120, 80};
    withMargin.minSize = {150, 0};
    CHECK(Near(withMargin.ArrangeIn({{0, 0}, {200, 100}}).size.x, 120.0f));
}

// Ponto no centro do botao de teste (border 100 x 28): dentro do desenho quando inside e verdadeiro.
PointerEvent Pointer(PointerPhase phase, bool inside, std::uint32_t pointerId = 1) {
    PointerEvent event;
    event.phase = phase;
    event.position = {50.0f, 14.0f};
    event.inside = inside;
    event.pointerId = pointerId;
    return event;
}

// Botao: clique so com Down e Up dentro, ponteiro capturado, cancelamento e desabilitado.
void TestButtonClickRules() {
    Button button;
    button.border = {{0, 0}, {100, 28}};
    ButtonEvents e = button.OnPointer(Pointer(PointerPhase::Down, true));
    CHECK(e.pressStarted && button.states.pressed);
    e = button.OnPointer(Pointer(PointerPhase::Up, true));
    CHECK(e.clicked && e.pressEnded && !button.states.pressed);

    // Arrastar para fora e soltar: sem Click.
    (void)button.OnPointer(Pointer(PointerPhase::Down, true));
    (void)button.OnPointer(Pointer(PointerPhase::Leave, false));
    e = button.OnPointer(Pointer(PointerPhase::Up, false));
    CHECK(!e.clicked && e.pressEnded);

    // Sair e voltar durante o gesto: o Up dentro ainda gera Click (captura).
    (void)button.OnPointer(Pointer(PointerPhase::Down, true));
    (void)button.OnPointer(Pointer(PointerPhase::Leave, false));
    (void)button.OnPointer(Pointer(PointerPhase::Enter, true));
    e = button.OnPointer(Pointer(PointerPhase::Up, true));
    CHECK(e.clicked);

    // Cancel solta o press e nunca gera Click.
    (void)button.OnPointer(Pointer(PointerPhase::Down, true));
    e = button.OnPointer(Pointer(PointerPhase::Cancel, true));
    CHECK(!e.clicked && !button.states.pressed);

    // Outro ponteiro nao solta o press deste.
    (void)button.OnPointer(Pointer(PointerPhase::Down, true, 1));
    e = button.OnPointer(Pointer(PointerPhase::Up, true, 2));
    CHECK(!e.clicked && button.states.pressed);
    (void)button.OnPointer(Pointer(PointerPhase::Cancel, true, 1));

    // Desabilitado: nada acontece e hover/pressed sao limpos.
    button.states.hovered = true;
    button.states.disabled = true;
    e = button.OnPointer(Pointer(PointerPhase::Down, true));
    CHECK(!e.pressStarted && !button.states.pressed && !button.states.hovered);
    CHECK(!button.ActivateByKey().activated);
}

void TestButtonHoldAndKeyboard() {
    Button button;
    button.border = {{0, 0}, {100, 28}};
    (void)button.OnPointer(Pointer(PointerPhase::Down, true));
    button.Advance(0.2f);
    CHECK(!button.IsHeld());
    button.Advance(0.1f);
    CHECK(button.IsHeld());
    (void)button.OnPointer(Pointer(PointerPhase::Up, true));
    CHECK(!button.IsHeld());

    // Enter/Espaco so ativam com foco.
    CHECK(!button.ActivateByKey().activated);
    button.states.focused = true;
    CHECK(button.ActivateByKey().activated);
    button.states.disabled = true;
    CHECK(!button.ActivateByKey().activated);
}

void TestButtonGeometryAndPressOffset() {
    // Rotulo sintetico de 30 px de largura; padding 4 em cima e embaixo, 8 nas laterais: 46 x (lineHeight + 8).
    const auto layout = SyntheticText::Layout("abc", 20.0f);
    const Vec2 size = PreferredButtonSize(layout, {4, 8, 4, 8});
    CHECK(Near(size.x, 46.0f));
    CHECK(Near(size.y, layout.lineHeight + 8.0f));

    Button button;
    button.border = {{0, 0}, {100, 28}};
    ButtonStyle style;
    CHECK(Near(ButtonPressOffset(button, style).y, 0.0f));
    (void)button.OnPointer(Pointer(PointerPhase::Down, true));
    CHECK(Near(ButtonPressOffset(button, style).y, 1.0f));
    button.states.disabled = true;
    CHECK(Near(ButtonPressOffset(button, style).y, 0.0f));
}

// Campo de texto: padding real, placeholder, caret no pen origin, selecao atras do texto, rolagem e clique.
// Texto sintetico: 10 px por caractere; "abc" mede 30 px.
void TestTextFieldLayoutAndItems() {
    TextField field;
    field.border = {{0, 0}, {100, 30}};
    field.padding = {2, 10, 2, 10};
    field.Edit().InsertText("abc");
    const Rect content = field.ContentRect();
    CHECK(SameRect(content, {{10, 2}, {80, 26}}));

    const TextFieldStyle style;
    const auto textLayout = SyntheticText::Layout("abc", 20.0f);
    const auto emptyLayout = SyntheticText::Layout("", 20.0f);
    const std::uint32_t clip = 3;

    // Sem foco e com texto: fundo e tres glifos; o primeiro glifo comeca no padding esquerdo.
    auto items = BuildTextFieldItems(field, style, textLayout, emptyLayout, content, TextTarget{}, clip);
    CHECK(items.size() == 4);
    if (items.size() == 4) {
        CHECK(Near(items[1].layout.resolvedPosition.x - textLayout.quads[0].bounds.position.x, content.position.x));
        CHECK(items[1].clipIndex == clip);
        CHECK(items[1].tintPremultipliedLinear.x == style.text.x);
    }

    // Vazio sem foco: so o fundo e o placeholder. O placeholder usa a cor de placeholder.
    TextField empty;
    empty.border = field.border;
    empty.padding = field.padding;
    empty.placeholder = "nome";
    const auto placeholderLayout = SyntheticText::Layout("nome", 20.0f);
    items = BuildTextFieldItems(empty, style, emptyLayout, placeholderLayout, content, TextTarget{}, clip);
    CHECK(items.size() == 5);
    if (items.size() == 5) CHECK(items[1].tintPremultipliedLinear.x == style.placeholder.x);

    // Com texto, o placeholder some.
    empty.Edit().InsertText("abc");
    items = BuildTextFieldItems(empty, style, textLayout, placeholderLayout, content, TextTarget{}, clip);
    CHECK(items.size() == 4);

    // Foco com caret no fim: o ultimo item e o caret, no pen origin depois de "abc" (30 px) dentro do padding.
    empty.SetFocused(true);
    items = BuildTextFieldItems(empty, style, textLayout, placeholderLayout, content, TextTarget{}, clip);
    CHECK(!items.empty());
    if (!items.empty()) {
        const Rendering::UIRenderItem& caret = items.back();
        CHECK(Near(caret.layout.resolvedPosition.x, content.position.x + 30.0f));
        CHECK(Near(caret.layout.resolvedSize.x, style.caretWidth));
    }

    // Selecao de tudo: retangulo de 30 px logo depois do fundo, atras do texto.
    empty.Edit().SelectAll();
    items = BuildTextFieldItems(empty, style, textLayout, placeholderLayout, content, TextTarget{}, clip);
    CHECK(items.size() == 6);
    if (items.size() == 6) {
        CHECK(Near(items[1].layout.resolvedSize.x, 30.0f));
        CHECK(Near(items[1].layout.resolvedPosition.x, content.position.x));
    }
}

void TestTextFieldScrollAndClick() {
    TextField field;
    field.border = {{0, 0}, {40, 30}};
    field.padding = {0, 10, 0, 10};
    field.Edit().InsertText(std::string(20, 'x'));
    field.SetFocused(true);
    const Vec2 contentSize{20.0f, 30.0f};
    const auto longLayout = SyntheticText::Layout(std::string(20, 'x'), 20.0f); // 200 px

    // Caret no fim: a rolagem leva o caret (200 + 1.5) para dentro da janela de 20 px.
    field.Edit().MoveEnd();
    field.KeepCaretVisible(longLayout, contentSize, TextFieldStyle{});
    CHECK(Near(field.ScrollX(), 181.5f));

    // Caret no inicio: a rolagem volta a zero.
    field.Edit().MoveHome();
    field.KeepCaretVisible(longLayout, contentSize, TextFieldStyle{});
    CHECK(Near(field.ScrollX(), 0.0f));

    // Sem foco, a rolagem e zero.
    field.Edit().MoveEnd();
    field.KeepCaretVisible(longLayout, contentSize, TextFieldStyle{});
    field.SetFocused(false);
    CHECK(Near(field.ScrollX(), 0.0f));

    // Clique em "abc" a 14 px do inicio do texto: o caret vai para o offset 1 ("a|bc").
    TextField clicked;
    clicked.border = field.border;
    clicked.padding = field.padding;
    clicked.Edit().InsertText("abc");
    const auto abc = SyntheticText::Layout("abc", 20.0f);
    clicked.PointerDown({14.0f, 15.0f}, {20.0f, 30.0f}, abc, TextFieldStyle{}, false);
    CHECK(clicked.Edit().Caret() == 1);
    CHECK(clicked.states.focused);
}

// Campo de uma linha com texto longo: a rolagem segue o caret (End, Home), o texto nao quebra e, ao perder o
// foco, volta a mostrar o inicio. Texto sintetico de 10 px por caractere: 20 caracteres medem 200 px.
void TestTextFieldScrollTransitions() {
    TextField field;
    field.border = {{0, 0}, {40, 30}};
    field.padding = {0, 10, 0, 10};
    const std::string text(20, 'x');
    field.Edit().InsertText(text);
    const Rect content = field.ContentRect();
    const TextFieldStyle style;
    auto longLayout = SyntheticText::Layout(text, 20.0f);
    const auto emptyLayout = SyntheticText::Layout("", 20.0f);
    longLayout.devicePixelScale = 0.0f;   // Sem alinhamento a pixel: o teste mede a aritmetica de rolagem (fracionaria).
    // Origem do texto na tela: x do primeiro glifo menos o bearing dele. Sem foco e sem rolagem, e o padding.
    auto textOriginX = [&]() {
        const auto items = BuildTextFieldItems(field, style, longLayout, emptyLayout, content, TextTarget{}, 0);
        return items.size() > 1 ? items[1].layout.resolvedPosition.x - longLayout.quads[0].bounds.position.x : 0.0f;
    };

    // Sem foco: o texto comeca no padding, sem rolagem.
    field.KeepCaretVisible(longLayout, content.size, style);
    CHECK(Near(textOriginX(), content.position.x));

    // Foco com caret no fim: rola 181.5 px (200 + caret - 20 de janela); o primeiro glifo sai pela esquerda.
    field.SetFocused(true);
    field.Edit().MoveEnd();
    field.KeepCaretVisible(longLayout, content.size, style);
    CHECK(Near(field.ScrollX(), 181.5f));
    CHECK(Near(textOriginX(), content.position.x - 181.5f));

    // Home: a rolagem volta ao inicio e o texto volta a origem.
    field.Edit().MoveHome();
    field.KeepCaretVisible(longLayout, content.size, style);
    CHECK(Near(field.ScrollX(), 0.0f));
    CHECK(Near(textOriginX(), content.position.x));

    // Perder o foco com o caret no fim: a rolagem zera e o texto mostra o inicio.
    field.Edit().MoveEnd();
    field.KeepCaretVisible(longLayout, content.size, style);
    field.SetFocused(false);
    CHECK(Near(field.ScrollX(), 0.0f));
    CHECK(Near(textOriginX(), content.position.x));

    // Ganhar o foco de novo mantem o caret no fim; a rolagem volta ao fim quando o caret for seguido.
    field.SetFocused(true);
    CHECK(Near(field.ScrollX(), 0.0f));
    field.KeepCaretVisible(longLayout, content.size, style);
    CHECK(Near(field.ScrollX(), 181.5f));
    CHECK(field.Edit().Caret() == text.size());
}

// Campo multilinha: a rolagem vertical segue o caret, subir e descer mantem a coluna e so as linhas visiveis geram
// itens. Texto sintetico de 10 px por caractere e fonte de 20 px: "abcdef\nxy\nabcdef" tem 3 linhas.
void TestTextFieldMultilineVerticalMotion() {
    const TextFieldStyle style;
    TextField field(TextFieldOptions{.multiline = true});
    field.fontSize = 20.0f;
    const std::string text = "abcdef\nxy\nabcdef";
    field.Edit().InsertText(text);
    field.SetFocused(true);

    // Area util com duas linhas de altura e 86 px de largura (a quebra fica em 80 px).
    const float lineHeight = SyntheticText::Layout("x", 20.0f).lineHeight;
    field.border = {{0, 0}, {86.0f, 2.0f * lineHeight}};
    const Rect content = field.ContentRect();
    const Text::TextLayoutConfig config = MakeTextFieldLayoutConfig(field, content.size, style);
    const auto layout = SyntheticText::Layout(text, config.fontSize, config.maxWidth, config.wrap);
    CHECK(layout.lines.size() == 3);

    // Caret no fim (linha de baixo): a rolagem mostra a linha do caret na ultima posicao da janela.
    field.KeepCaretVisible(layout, content.size, style);
    CHECK(Near(field.ScrollY(), lineHeight));

    // Caret na coluna 5 da primeira linha ("abcde|f"): a rolagem volta ao topo.
    field.Edit().MoveCaretTo(5);
    field.KeepCaretVisible(layout, content.size, style);
    CHECK(Near(field.ScrollY(), 0.0f));

    // Descer: a linha "xy" e curta, o caret vai para o fim dela (ou para a quebra). Descer de novo volta a
    // coluna 5 da ultima linha, offset 15.
    CHECK(field.MoveDown(layout));
    CHECK(field.Edit().Caret() >= 7 && field.Edit().Caret() <= 9);
    CHECK(field.MoveDown(layout));
    CHECK(field.Edit().Caret() == 15);
    field.KeepCaretVisible(layout, content.size, style);
    CHECK(Near(field.ScrollY(), lineHeight));

    // Subir duas vezes volta a coluna 5 da primeira linha.
    CHECK(field.MoveUp(layout));
    CHECK(field.MoveUp(layout));
    CHECK(field.Edit().Caret() == 5);

    // Mover o caret para o inicio redefine a coluna: descer cai na coluna 0 da linha "xy", offset 7.
    field.Edit().MoveHome();
    CHECK(field.MoveDown(layout));
    CHECK(field.Edit().Caret() == 7);

    // Subir a partir da primeira linha nao muda o caret.
    field.Edit().MoveHome();
    CHECK(!field.MoveUp(layout));

    // Rolado para as duas ultimas linhas: so elas geram glifos (fundo, trilho, polegar, glifos e caret).
    field.Edit().MoveEnd();
    field.KeepCaretVisible(layout, content.size, style);
    const auto emptyLayout = SyntheticText::Layout("", 20.0f);
    const auto items = BuildTextFieldItems(field, style, layout, emptyLayout, content, TextTarget{}, 0);
    const std::size_t visibleQuads = layout.lines[1].quadCount + layout.lines[2].quadCount;
    CHECK(items.size() == visibleQuads + 4);

    // De uma linha, MoveUp e MoveDown nao fazem nada.
    TextField single;
    CHECK(!single.MoveDown(layout));
}

// Barra de rolagem do campo multilinha: o polegar e proporcional a parte visivel (no minimo 16 px) e fica no
// fim do trilho quando rolado ao fim. Roda e arrasto do polegar rolam; clicar na barra nao move o caret.
void TestTextFieldScrollBarThumb() {
    const TextFieldStyle style;
    TextField field(TextFieldOptions{.multiline = true});
    field.fontSize = 20.0f;
    const std::string text = "a\nb\nc\nd";
    field.Edit().InsertText(text);
    field.SetFocused(true);

    // Linhas de 20 px (multiplicador 50 sobre a linha de 0,4 px do texto sintetico). A area util tem duas linhas
    // de altura e o texto (quatro linhas) passa dela.
    constexpr float kLineSpacing = 50.0f;
    const float lineHeight = SyntheticText::Layout("x", 20.0f, 0.0f, Text::TextWrap::None, kLineSpacing).lineHeight;
    field.border = {{0, 0}, {86.0f, 2.0f * lineHeight}};
    const Rect content = field.ContentRect();
    const Text::TextLayoutConfig config = MakeTextFieldLayoutConfig(field, content.size, style);
    const auto layout = SyntheticText::Layout(text, config.fontSize, config.maxWidth, config.wrap, kLineSpacing);
    CHECK(layout.lines.size() == 4);

    // Caret no fim: a rolagem mostra as duas ultimas linhas e a barra aparece.
    field.KeepCaretVisible(layout, content.size, style);
    CHECK(Near(field.ScrollY(), 2.0f * lineHeight));
    CHECK(field.ScrollBarVisible());

    // Trilho na borda direita, com a altura da area util. Duas de quatro linhas visiveis: metade do trilho,
    // no minimo 16 px, e no fim do trilho porque a rolagem esta no fim.
    const Rect track = field.ScrollBarTrack();
    CHECK(Near(track.position.x, 86.0f - style.scrollBarWidth));
    CHECK(Near(track.size.y, 2.0f * lineHeight));
    const Rect thumb = field.ScrollThumb();
    CHECK(Near(thumb.size.y, std::max(16.0f, 0.5f * track.size.y)));
    CHECK(Near(thumb.position.y + thumb.size.y, track.size.y));

    // Roda para cima: uma linha. O polegar sobe.
    CHECK(field.Wheel(-lineHeight));
    CHECK(Near(field.ScrollY(), lineHeight));
    CHECK(field.ScrollThumb().position.y < thumb.position.y);

    // Clique no polegar nao move o caret (offset 7, fim do texto); arrastar para baixo rola para baixo.
    const std::size_t caretBefore = field.Edit().Caret();
    const Rect shownThumb = field.ScrollThumb();
    const Vec2 onThumb{track.position.x + 1.0f, shownThumb.position.y + 0.5f * shownThumb.size.y};
    field.PointerDown(onThumb, content.size, layout, style);
    CHECK(field.Edit().Caret() == caretBefore);
    CHECK(field.PointerMove({onThumb.x, onThumb.y + 2.0f}));
    CHECK(field.ScrollY() > lineHeight);
    field.PointerUp();
    CHECK(!field.PointerMove({onThumb.x, onThumb.y + 50.0f})); // solto: arrastar nao rola mais
}

// Campo multilinha: a quebra usa a largura util menos a faixa da barra. Com 56 px de conteudo e faixa de 6 px, a
// quebra tem 50 px (5 caracteres de 10 px, com fonte de 20 px). Palavra maior que a linha quebra entre clusters.
void TestTextFieldMultilineGeometry() {
    const TextFieldStyle style;
    const Vec2 contentSize{56.0f, 100.0f};

    TextField multi(TextFieldOptions{.multiline = true});
    multi.fontSize = 20.0f;
    const Text::TextLayoutConfig config = MakeTextFieldLayoutConfig(multi, contentSize, style);
    CHECK(config.wrap == Text::TextWrap::Word);
    CHECK(Near(config.maxWidth, 50.0f));

    // "abc " fica na primeira linha (o espaco pendura); "def " e "ghij" vem depois.
    const auto words = SyntheticText::Layout("abc def ghij", config.fontSize, config.maxWidth, config.wrap);
    CHECK(words.lines.size() == 3);
    if (words.lines.size() == 3) {
        CHECK(words.lines[1].byteBegin == 4);
        CHECK(words.lines[2].byteBegin == 8);
        CHECK(Near(words.size.y, 3.0f * words.lineHeight));
    }

    // Palavra de 12 caracteres: 5, 5 e 2 caracteres por linha.
    const auto longWord = SyntheticText::Layout("abcdefghijkl", config.fontSize, config.maxWidth, config.wrap);
    CHECK(longWord.lines.size() == 3);
    if (longWord.lines.size() == 3) {
        CHECK(longWord.lines[1].byteBegin == 5);
        CHECK(longWord.lines[2].byteBegin == 10);
    }

    // Quebra forcada de linha: o campo multilinha aceita '\n'; o de uma linha descarta.
    multi.Edit().InsertText("a\nb");
    CHECK(multi.Edit().Text() == "a\nb");

    // De uma linha: sem quebra e sem limite de largura; o texto nao ganha quebra de linha.
    TextField single;
    const Text::TextLayoutConfig singleConfig = MakeTextFieldLayoutConfig(single, contentSize, style);
    CHECK(singleConfig.wrap == Text::TextWrap::None);
    CHECK(Near(singleConfig.maxWidth, 0.0f));
    CHECK(!single.Multiline());
    single.Edit().InsertText("a\nb");
    CHECK(single.Edit().Text() == "ab");
}

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

// Area de clique igual ao desenho: o botao so aciona dentro do fundo, cantos arredondados incluidos.
void TestButtonClickAreaFollowsDrawing() {
    Button button;
    button.border = {{0, 0}, {100, 28}};
    button.cornerRadius = 8.0f;
    (void)button.OnPointer(PointerAt(PointerPhase::Down, {50, 14}, true));
    CHECK(button.OnPointer(PointerAt(PointerPhase::Up, {50, 14}, true)).clicked);

    // Canto da caixa fora do arco (raio 8): nao inicia pressao nem gera Click.
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Down, {1, 1}, true)).pressStarted);
    CHECK(!button.states.pressed);
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Up, {1, 1}, true)).clicked);

    // Ponto fora da caixa (o chamador informa inside falso): nada.
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Down, {150, 14}, false)).pressStarted);

    // Hover: entrar so no canto de fora nao acende o destaque; entrar no meio acende.
    (void)button.OnPointer(PointerAt(PointerPhase::Enter, {1, 1}, true));
    CHECK(!button.states.hovered);
    (void)button.OnPointer(PointerAt(PointerPhase::Enter, {50, 14}, true));
    CHECK(button.states.hovered);
}

// Area desenhada: pilula (raio = metade do menor lado), cantos de fora do arco e borda direita meio-aberta.
void TestDrawnAreaHelpers() {
    const Rect pill{{0, 0}, {40, 28}};
    CHECK(ContainsDrawn(pill, 14, {20, 14}));
    CHECK(ContainsDrawn(pill, 14, {1, 14}));     // Ponta da pilula, dentro do arco.
    CHECK(!ContainsDrawn(pill, 14, {1, 1}));     // Canto: fora do arco.
    CHECK(ContainsDrawn(pill, 0, {1, 1}));       // Sem raio, o canto conta.
    CHECK(!ContainsDrawn(pill, 14, {40, 14}));   // Borda direita e meio-aberta.
    CHECK(!ContainsDrawn(pill, 14, {-1, 14}));
    CHECK(!ContainsDrawn(pill, 14, {std::numeric_limits<float>::quiet_NaN(), 14}));
    CHECK(ContainsDrawn(pill, 100, {20, 14}));   // Raio grande demais vira pilula, nao passa do meio.
    CHECK(!ContainsDrawn(pill, 100, {1, 1}));

    PointerEvent enter;
    enter.phase = PointerPhase::Enter;
    enter.inside = true;
    enter.position = {1, 1};
    const PointerEvent outside = WithinDrawn(enter, pill, 14);
    CHECK(outside.phase == PointerPhase::Leave && !outside.inside);
    enter.position = {20, 14};
    const PointerEvent within = WithinDrawn(enter, pill, 14);
    CHECK(within.phase == PointerPhase::Enter && within.inside);
}

// Altura padrao 28 px a 100 por cento: minimo do elemento para botao, caixa de selecao e chave.
void TestControlDefaultHeight() {
    CHECK(Near(Button{}.minSize.y, kStandardControlHeight));
    CHECK(Near(Checkbox{}.minSize.y, 28.0f));
    CHECK(Near(Toggle{}.minSize.y, 28.0f));
    CHECK(Near(kStandardControlHeight, 28.0f));
}

// Caixa de selecao: Click inverte uma vez, arrastar para fora nao muda, desabilitado nao muda, Espaco com foco.
void TestCheckboxToggleRules() {
    Checkbox box;
    box.border = {{0, 0}, {200, 30}};
    CheckboxStyle style;

    (void)box.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true));
    CheckboxEvents e = box.OnPointer(PointerAt(PointerPhase::Up, {5, 5}, true));
    CHECK(e.clicked && e.valueChanged && box.checked);

    // Arrastar para fora e soltar: nada muda.
    (void)box.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true));
    (void)box.OnPointer(PointerAt(PointerPhase::Leave, {-5, 5}, false));
    e = box.OnPointer(PointerAt(PointerPhase::Up, {-5, 5}, false));
    CHECK(!e.clicked && !e.valueChanged && box.checked);

    // Espaco so inverte com foco.
    CHECK(!box.ToggleByKey().valueChanged);
    box.states.focused = true;
    CHECK(box.ToggleByKey().valueChanged && !box.checked);

    // Desabilitado: nao muda o valor.
    box.states.disabled = true;
    CHECK(!box.OnPointer(PointerAt(PointerPhase::Down, {5, 5}, true)).valueChanged);
    CHECK(!box.ToggleByKey().valueChanged && !box.checked);

    // Caixa: fixa a esquerda, centralizada na vertical (30 - 18) / 2 = 6. Rotulo recua 18 + 8 = 26.
    const Rect boxRect = CheckboxBox(box, style);
    CHECK(SameRect(boxRect, {{0, 6}, {18, 18}}));
    CHECK(Near(CheckboxLabelPadding(style).left, 26.0f));
}

// Marca e foco: sem foco e desmarcada, um item; marcada, mais dois segmentos da marca; focada, mais o contorno.
void TestCheckboxItems() {
    Checkbox box;
    box.border = {{0, 0}, {200, 30}};
    const CheckboxStyle style;
    CHECK(BuildCheckboxItems(box, style, 0.0f, 0).size() == 1);
    box.checked = true;
    CHECK(BuildCheckboxItems(box, style, 0.0f, 0).size() == 3);
    box.checked = false;
    box.states.focused = true;
    CHECK(BuildCheckboxItems(box, style, 0.0f, 0).size() == 5);
}

// Slider: limites, NaN, minimo igual ao maximo, inteiro, degrau, passos e teclas de limite.
void TestSliderValueRules() {
    Slider slider;
    slider.range = {0.0f, 10.0f, 0.0f, false};
    CHECK(slider.SetValue(15.0f));
    CHECK(Near(slider.Value(), 10.0f));
    CHECK(!slider.SetValue(std::numeric_limits<float>::quiet_NaN()));
    CHECK(Near(slider.Value(), 10.0f));
    CHECK(Near(slider.Ratio(), 1.0f));

    // Minimo igual ao maximo: valido e fixo.
    Slider fixed;
    fixed.range = {5.0f, 5.0f, 0.0f, false};
    CHECK(!fixed.SetValue(3.0f));
    CHECK(Near(fixed.Value(), 5.0f));
    CHECK(Near(fixed.Ratio(), 0.0f));

    // Inteiro: 3.6 vira 4.
    Slider whole;
    whole.range = {0.0f, 10.0f, 0.0f, true};
    (void)whole.SetValue(3.6f);
    CHECK(Near(whole.Value(), 4.0f));

    // Degrau de 0.25: 0.6 vai para 0.5 (2.4 passos arredondados para 2).
    Slider stepped;
    stepped.range = {0.0f, 1.0f, 0.25f, false};
    (void)stepped.SetValue(0.6f);
    CHECK(Near(stepped.Value(), 0.5f));

    // Sem degrau, um passo e 1% do intervalo. Dez passos a partir de zero levam a 0.1 e nao passam do maximo.
    Slider keys;
    keys.range = {0.0f, 1.0f, 0.0f, false};
    for (int i = 0; i < 10; ++i) (void)keys.StepBy(1);
    CHECK(Near(keys.Value(), 0.1f));
    CHECK(keys.StepBy(-20) && Near(keys.Value(), 0.0f));
    CHECK(keys.SetToMaximum() && Near(keys.Value(), 1.0f));
    CHECK(!keys.StepBy(5));
    CHECK(keys.SetToMinimum() && Near(keys.Value(), 0.0f));
}

// Slider com o mouse: a posicao do ponteiro mapeia para o valor pela trilha, descontando metade do polegar.
void TestSliderPointerMapping() {
    Slider slider;
    slider.range = {0.0f, 1.0f, 0.0f, false};
    slider.border = {{0, 0}, {114, 30}}; // Trilha de 114 px, polegar de 14: curso de 100 px.

    // Centro do polegar em 7 + 100 * ratio. Clique em x = 57 vale 0.5.
    SliderEvents e = slider.OnPointer(PointerAt(PointerPhase::Down, {57.0f, 15.0f}, true));
    CHECK(e.pressStarted && e.valueChanged);
    CHECK(Near(slider.Value(), 0.5f));

    // Arrasto com o botao: x = 107 vale 1.0; x = 0 fica no minimo.
    e = slider.OnPointer(PointerAt(PointerPhase::Move, {107.0f, 15.0f}, true));
    CHECK(e.valueChanged && Near(slider.Value(), 1.0f));
    e = slider.OnPointer(PointerAt(PointerPhase::Move, {0.0f, 15.0f}, true));
    CHECK(Near(slider.Value(), 0.0f));

    // Soltar nao muda o valor; movimento sem botao nao arrasta.
    (void)slider.OnPointer(PointerAt(PointerPhase::Up, {0.0f, 15.0f}, true));
    e = slider.OnPointer(PointerAt(PointerPhase::Move, {80.0f, 15.0f}, true));
    CHECK(!e.valueChanged && Near(slider.Value(), 0.0f));
}

// Rotulo: alinhamento dentro da area util e recorte. Texto sintetico de 3 caracteres = 30 px de largura.
void TestLabelAlignment() {
    const std::string text = "abc";
    auto layout = SyntheticText::Layout(text, 20.0f);
    layout.devicePixelScale = 0.0f;   // Sem alinhamento a pixel: o teste mede a aritmetica do alinhamento.
    CHECK(Near(layout.size.x, 30.0f));
    const float lineHeight = layout.lineHeight;
    const Rect content{{0, 0}, {100, 40}};
    const std::uint32_t clip = 7;

    struct Case {
        HorizontalAlign horizontal;
        VerticalAlign vertical;
        Vec2 expectedOrigin;
    };
    // Largura livre 70, altura livre 40 - lineHeight.
    const Case cases[] = {
        {HorizontalAlign::Left, VerticalAlign::Top, {0.0f, 0.0f}},
        {HorizontalAlign::Center, VerticalAlign::Middle, {35.0f, (40.0f - lineHeight) * 0.5f}},
        {HorizontalAlign::Right, VerticalAlign::Bottom, {70.0f, 40.0f - lineHeight}},
    };
    for (const Case& c : cases) {
        Label label;
        label.text = text;
        label.horizontal = c.horizontal;
        label.vertical = c.vertical;
        const auto items = BuildLabelItems(label, layout, content, TextTarget{}, clip);
        CHECK(!items.empty());
        if (items.empty()) continue;
        const Vec2 origin = items[0].layout.resolvedPosition - layout.quads[0].bounds.position;
        CHECK(Near(origin.x, c.expectedOrigin.x));
        CHECK(Near(origin.y, c.expectedOrigin.y));
        CHECK(items[0].clipIndex == clip);
    }

    // Texto de 20 caracteres (200 px) em area de 100 px, centralizado: sobra -50 dos dois lados.
    // O texto nao empurra o layout; o recorte corta a parte de fora.
    const auto wide = SyntheticText::Layout(std::string(20, 'x'), 20.0f);
    Label centered;
    centered.text = std::string(20, 'x');
    centered.horizontal = HorizontalAlign::Center;
    const auto wideItems = BuildLabelItems(centered, wide, content, TextTarget{}, clip);
    CHECK(!wideItems.empty());
    if (!wideItems.empty()) {
        const Vec2 origin = wideItems[0].layout.resolvedPosition - wide.quads[0].bounds.position;
        CHECK(Near(origin.x, -50.0f));
    }

    // Area nao finita: nenhum item.
    Label broken;
    broken.text = text;
    CHECK(BuildLabelItems(broken, layout, {{0, 0}, {std::numeric_limits<float>::quiet_NaN(), 40}},
                          TextTarget{}, clip)
              .empty());
}

void TestTreeHandlesAndDestroy() {
    ElementTree tree;
    const UIHandle root = tree.Create<Box>(InvalidUIHandle);
    const UIHandle child = tree.Create<Box>(root);
    const UIHandle grandchild = tree.Create<Box>(child);
    CHECK(root.IsValid() && child.IsValid() && grandchild.IsValid());
    CHECK(tree.LiveCount() == 3);
    CHECK(tree.Get(child)->parent == root);
    CHECK(tree.Get(root)->children.size() == 1);

    // Destruir o filho leva o neto junto e tira o filho da lista do pai.
    tree.Destroy(child);
    CHECK(tree.Get(child) == nullptr);
    CHECK(tree.Get(grandchild) == nullptr);
    CHECK(tree.Get(root)->children.empty());
    CHECK(tree.LiveCount() == 1);

    // O indice reaproveitado tem outra geracao: o handle antigo continua invalido.
    const UIHandle reused = tree.Create<Box>(root);
    CHECK(reused.index == child.index);
    CHECK(reused.generation != child.generation);
    CHECK(tree.Get(child) == nullptr);
    CHECK(tree.Get(reused) != nullptr);

    // Destruir duas vezes nao faz nada.
    tree.Destroy(child);
    CHECK(tree.LiveCount() == 2);
}

void TestPaintOrderAndBringToFront() {
    ElementTree tree;
    const UIHandle root = tree.Create<Box>(InvalidUIHandle);
    const UIHandle a = tree.Create<Box>(root);
    const UIHandle b = tree.Create<Box>(root);
    const UIHandle c = tree.Create<Box>(root);
    const UIHandle aChild = tree.Create<Box>(a);

    std::vector<UIHandle> order;
    tree.PaintOrder(root, order);
    CHECK(order.size() == 5);
    CHECK(order[0] == root && order[1] == a && order[2] == aChild && order[3] == b && order[4] == c);

    // Trazer A para frente: ele vai para o fim da lista de irmaos, e o filho dele pinta logo depois.
    tree.BringToFront(a);
    order.clear();
    tree.PaintOrder(root, order);
    CHECK(order.size() == 5);
    CHECK(order[0] == root && order[1] == b && order[2] == c && order[3] == a && order[4] == aChild);
}

void TestArrangeUsesParentContentRect() {
    // Raiz 200x100 com padding 10: area util (10,10) 180x80. Filho esticado ocupa exatamente a area util.
    ElementTree tree;
    const UIHandle root = tree.Create<Box>(InvalidUIHandle);
    Element* rootElement = tree.Get(root);
    rootElement->transform.anchorMax = {1.0f, 1.0f};
    rootElement->transform.offsetMax = {0.0f, 0.0f};
    rootElement->padding = {10, 10, 10, 10};
    const UIHandle child = tree.Create<Box>(root);
    tree.Get(child)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(child)->transform.offsetMax = {0.0f, 0.0f};

    tree.Arrange(root, {{0, 0}, {200, 100}});
    CHECK(SameRect(tree.Get(root)->border, {{0, 0}, {200, 100}}));
    CHECK(SameRect(tree.Get(child)->border, {{10, 10}, {180, 80}}));
}

} // namespace

int main() {
    TestShrinkRect();
    TestClampSize();
    TestArrangeAnchorsAndOffsets();
    TestArrangeMarginAndMinMax();
    TestTreeHandlesAndDestroy();
    TestPaintOrderAndBringToFront();
    TestArrangeUsesParentContentRect();
    TestLabelAlignment();
    TestButtonClickRules();
    TestButtonHoldAndKeyboard();
    TestButtonGeometryAndPressOffset();
    TestButtonClickAreaFollowsDrawing();
    TestDrawnAreaHelpers();
    TestControlDefaultHeight();
    TestTextFieldLayoutAndItems();
    TestTextFieldScrollAndClick();
    TestTextFieldScrollTransitions();
    TestTextFieldMultilineVerticalMotion();
    TestTextFieldScrollBarThumb();
    TestTextFieldMultilineGeometry();
    TestCheckboxToggleRules();
    TestCheckboxItems();
    TestSliderValueRules();
    TestSliderPointerMapping();

    std::printf("elements: %d verificacoes, %d falhas\n", run, failed);
    return failed == 0 ? 0 : 1;
}
