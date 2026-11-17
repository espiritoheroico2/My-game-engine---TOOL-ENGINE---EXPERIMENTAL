// Seletor de cor RGBA (ColorPicker.hpp): campos numericos, texto invalido, passos, amostra e reverter, e a geometria
// das areas e dos marcadores do desenho. Sem janela.
#include "SpriteUi/Widgets/ColorPicker.hpp"

#include <cmath>
#include <cstdio>

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

bool Near(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

void TestInitialColorAndSample() {
    ColorPickerControl picker(Vec4{1.0f, 0.0f, 0.5f, 1.0f});
    CHECK(picker.Channel(ColorChannel::Red) == 255);
    CHECK(picker.Channel(ColorChannel::Green) == 0);
    CHECK(picker.Channel(ColorChannel::Blue) == 128);
    CHECK(picker.ChannelText(ColorChannel::Blue) == "128");
    CHECK(Near(picker.Sample().z, 0.5f, 1.0e-6f)); // amostra sem arredondar; so a exibicao usa 128
    CHECK(!picker.Changed());
}

void TestSetChannelClampsAndReportsChange() {
    ColorPickerControl picker;
    CHECK(!picker.SetChannel(ColorChannel::Red, 300)); // padrao ja e 255: 300 prende nele, sem mudanca
    CHECK(!picker.SetChannel(ColorChannel::Red, 999));
    CHECK(picker.SetChannel(ColorChannel::Red, -40));
    CHECK(picker.Channel(ColorChannel::Red) == 0);
    CHECK(!picker.SetChannel(ColorChannel::Red, 0));
}

void TestTextAcceptsDigitsAndRejectsGarbage() {
    ColorPickerControl picker;
    CHECK(picker.SetChannelText(ColorChannel::Green, "77"));
    CHECK(picker.Channel(ColorChannel::Green) == 77);
    CHECK(picker.SetChannelText(ColorChannel::Green, "  12  "));
    CHECK(picker.Channel(ColorChannel::Green) == 12);
    CHECK(picker.SetChannelText(ColorChannel::Green, "9999999999999999999")); // prende em 255
    CHECK(picker.Channel(ColorChannel::Green) == 255);

    CHECK(!picker.SetChannelText(ColorChannel::Green, "12a"));
    CHECK(!picker.SetChannelText(ColorChannel::Green, ""));
    CHECK(!picker.SetChannelText(ColorChannel::Green, "-"));
    CHECK(!picker.SetChannelText(ColorChannel::Green, "1 2"));
    CHECK(picker.Channel(ColorChannel::Green) == 255); // texto invalido nao muda o campo
}

void TestTextNegativeClampsToZero() {
    ColorPickerControl picker;
    CHECK(picker.SetChannelText(ColorChannel::Alpha, "-12"));
    CHECK(picker.Channel(ColorChannel::Alpha) == 0);
}

void TestStepMovesAndClamps() {
    ColorPickerControl picker(Vec4{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(picker.Step(ColorChannel::Red, 1));
    CHECK(picker.Channel(ColorChannel::Red) == 1);
    CHECK(picker.Step(ColorChannel::Red, -10)); // 1 - 10 prende em 0
    CHECK(picker.Channel(ColorChannel::Red) == 0);
    CHECK(!picker.Step(ColorChannel::Red, -1));
    CHECK(picker.Step(ColorChannel::Red, 300)); // 0 + 300 prende em 255
    CHECK(picker.Channel(ColorChannel::Red) == 255);
}

void TestSetColorRoundsAndIgnoresNonFinite() {
    ColorPickerControl picker;
    CHECK(picker.SetColor(Vec4{0.5f, 0.25f, 2.0f, -1.0f})); // fora de 0..1 e prendido
    CHECK(picker.Channel(ColorChannel::Red) == 128);
    CHECK(picker.Channel(ColorChannel::Green) == 64);
    CHECK(picker.Channel(ColorChannel::Blue) == 255);
    CHECK(picker.Channel(ColorChannel::Alpha) == 0);
    CHECK(!picker.SetColor(Vec4{NAN, 0.0f, 0.0f, 0.0f}));
    CHECK(picker.Channel(ColorChannel::Red) == 128);
}

void TestDisabledIgnoresUserInputButNotProgram() {
    ColorPickerControl picker;
    picker.disabled = true;
    CHECK(!picker.SetChannelText(ColorChannel::Red, "10"));
    CHECK(!picker.Step(ColorChannel::Red, -5));
    CHECK(picker.Channel(ColorChannel::Red) == 255);
    CHECK(picker.SetColor(Vec4{0.0f, 0.0f, 0.0f, 1.0f})); // programa continua valendo
    CHECK(picker.Channel(ColorChannel::Red) == 0);
}

void TestOriginalAndRevert() {
    ColorPickerControl picker(Vec4{0.2f, 0.4f, 0.6f, 1.0f});
    CHECK(Near(picker.Original().x, 0.2f, 0.01f));
    CHECK(picker.SetChannelText(ColorChannel::Red, "0"));
    CHECK(picker.Changed());
    CHECK(Near(picker.Original().x, 0.2f, 0.01f)); // original nao muda com a edicao
    CHECK(picker.Revert());
    CHECK(!picker.Changed());
    CHECK(picker.Channel(ColorChannel::Red) == 51);
    CHECK(!picker.Revert());
}

void TestDragKeepsFullPrecisionUntilDisplay() {
    ColorPickerControl picker(Vec4{1.0f, 0.0f, 0.0f, 1.0f});
    CHECK(picker.DragSaturationValue(0.31234f, 1.0f)); // HSV: o arraste nao arredonda para 8 bits
    CHECK(Near(picker.Sample().y, 0.68766f, 1.0e-5f));
    CHECK(picker.Channel(ColorChannel::Green) == 175); // so a exibicao arredonda
}

void TestHueDragKeepsSaturationAndValue() {
    ColorPickerControl picker(Vec4{1.0f, 0.0f, 0.0f, 1.0f});
    CHECK(picker.DragHue(1.0f / 3.0f)); // verde puro
    CHECK(Near(picker.Sample().y, 1.0f, 1.0e-5f));
    CHECK(Near(picker.Sample().x, 0.0f, 1.0e-5f));
}

void TestHexAcceptsSixAndEightDigitsInAnyCase() {
    ColorPickerControl picker;
    CHECK(picker.SetHex("#ff8000"));
    CHECK(picker.Channel(ColorChannel::Red) == 255);
    CHECK(picker.Channel(ColorChannel::Green) == 128);
    CHECK(picker.Channel(ColorChannel::Alpha) == 255); // sem alfa no texto: opaco
    CHECK(picker.SetHex("#00FF0080"));
    CHECK(picker.Channel(ColorChannel::Green) == 255);
    CHECK(picker.Channel(ColorChannel::Alpha) == 128);
}

void TestInvalidHexIsRejectedAndKeepsColor() {
    ColorPickerControl picker(Vec4{0.0f, 0.0f, 1.0f, 1.0f});
    const char* invalid[] = {"#ff80", "ff8000", "#gg0000", "#ff800000ab", "", "#ff8000 ", "#ff8000ff00"};
    for (const char* text : invalid) {
        CHECK(!picker.SetHex(text));
        CHECK(picker.Channel(ColorChannel::Blue) == 255);
        CHECK(picker.Channel(ColorChannel::Red) == 0);
    }
}

void TestHexTextIsUpperCaseAndRoundTrips() {
    ColorPickerControl picker;
    CHECK(picker.SetHex("#ff8000cc"));
    CHECK(picker.HexText() == "#FF8000CC");
}

void TestAlphaIsClampedToUnitRange() {
    ColorPickerControl picker;
    CHECK(picker.SetAlpha(0.5f));
    CHECK(picker.SetAlpha(2.0f));
    CHECK(Near(picker.Sample().w, 1.0f, 1.0e-6f));
    CHECK(picker.SetAlpha(-1.0f));
    CHECK(Near(picker.Sample().w, 0.0f, 1.0e-6f));
    CHECK(!picker.SetAlpha(NAN));
    CHECK(Near(picker.Sample().w, 0.0f, 1.0e-6f));
}


void TestLayoutStacksAreasInOrderInsideBounds() {
    const Rect bounds{{10.0f, 20.0f}, {240.0f, 300.0f}};
    const ColorPickerLayout layout = LayoutColorPicker(bounds);

    CHECK(Near(layout.saturationValue.position.x, 10.0f));
    CHECK(Near(layout.saturationValue.position.y, 20.0f));
    CHECK(Near(layout.saturationValue.size.x, 240.0f));
    // A area 2D e o que sobra: 300 menos 2 faixas, 2 linhas de campos e 4 vaos.
    CHECK(Near(layout.saturationValue.size.y, 300.0f - (2 * 14.0f + 2 * 24.0f + 4 * 8.0f)));
    CHECK(layout.hue.position.y > layout.saturationValue.Max().y);
    CHECK(layout.alpha.position.y > layout.hue.Max().y);
    CHECK(layout.sampleOriginal.position.y > layout.alpha.Max().y);
    CHECK(Near(layout.channelFields[0].position.y, layout.sampleOriginal.position.y + 24.0f + 8.0f));
    CHECK(Near(layout.channelFields[3].Max().x, 10.0f + 240.0f));
    CHECK(Near(layout.channelFields[3].Max().y, 20.0f + 300.0f));
    CHECK(Near(layout.sampleCurrent.position.x, layout.sampleOriginal.Max().x));
    CHECK(layout.hexField.position.x > layout.sampleCurrent.Max().x);
    CHECK(Near(layout.hexField.Max().x, 10.0f + 240.0f));
}

void TestLayoutSmallBoundsNeverGoesNegative() {
    const ColorPickerLayout layout = LayoutColorPicker(Rect{{0.0f, 0.0f}, {10.0f, 10.0f}});
    CHECK(layout.saturationValue.size.y == 0.0f);
    CHECK(layout.channelFields[0].size.x == 0.0f);
    CHECK(layout.hexField.size.x >= 0.0f);
}

void TestMarkerCentersFollowValues() {
    const ColorPickerLayout layout = LayoutColorPicker(Rect{{0.0f, 0.0f}, {200.0f, 260.0f}});
    ColorPickerControl white(Vec4{1.0f, 1.0f, 1.0f, 1.0f}); // S 0, V 1: canto superior esquerdo.
    const Vec2 topLeft = SaturationValueMarkerCenter(white, layout);
    CHECK(Near(topLeft.x, layout.saturationValue.position.x));
    CHECK(Near(topLeft.y, layout.saturationValue.position.y));

    ColorPickerControl red(Vec4{1.0f, 0.0f, 0.0f, 1.0f}); // S 1, V 1: canto superior direito.
    const Vec2 topRight = SaturationValueMarkerCenter(red, layout);
    CHECK(Near(topRight.x, layout.saturationValue.Max().x));
    CHECK(Near(topRight.y, layout.saturationValue.position.y));

    const Vec2 hue = HueMarkerCenter(red, layout);
    CHECK(Near(hue.x, layout.hue.position.x)); // Vermelho puro: matiz 0.
    const Vec2 alpha = AlphaMarkerCenter(ColorPickerControl(Vec4{1.0f, 0.0f, 0.0f, 0.5f}), layout);
    CHECK(Near(alpha.x, layout.alpha.position.x + 0.5f * layout.alpha.size.x));
}

void TestBuildItemsCoverEveryArea() {
    const ColorPickerLayout layout = LayoutColorPicker(Rect{{0.0f, 0.0f}, {240.0f, 300.0f}});
    ColorPickerControl picker(Vec4{0.2f, 0.4f, 0.8f, 0.6f});
    const auto items = BuildColorPickerItems(picker, layout, UIHandle{}, ColorPickerStyle{}, 0.0f, 0);
    CHECK(items.size() > 32 * 24); // A area 2D, mais faixas, amostras, campos e marcadores.
    bool anyNonFinite = false;
    for (const auto& item : items) {
        if (!IsFinite(item.layout.resolvedPosition) || !IsFinite(item.layout.resolvedSize)) anyNonFinite = true;
    }
    CHECK(!anyNonFinite);
}

void TestItemsAreDeterministic() {
    const ColorPickerLayout layout = LayoutColorPicker(Rect{{0.0f, 0.0f}, {240.0f, 300.0f}});
    ColorPickerControl picker(Vec4{0.5f, 0.1f, 0.9f, 1.0f});
    const auto first = BuildColorPickerItems(picker, layout, UIHandle{}, ColorPickerStyle{}, 0.0f, 0);
    const auto second = BuildColorPickerItems(picker, layout, UIHandle{}, ColorPickerStyle{}, 0.0f, 0);
    CHECK(first.size() == second.size());
}

} // namespace

int main() {
    TestDragKeepsFullPrecisionUntilDisplay();
    TestHueDragKeepsSaturationAndValue();
    TestHexAcceptsSixAndEightDigitsInAnyCase();
    TestInvalidHexIsRejectedAndKeepsColor();
    TestHexTextIsUpperCaseAndRoundTrips();
    TestAlphaIsClampedToUnitRange();
    TestInitialColorAndSample();
    TestSetChannelClampsAndReportsChange();
    TestTextAcceptsDigitsAndRejectsGarbage();
    TestTextNegativeClampsToZero();
    TestStepMovesAndClamps();
    TestSetColorRoundsAndIgnoresNonFinite();
    TestDisabledIgnoresUserInputButNotProgram();
    TestOriginalAndRevert();
    TestLayoutStacksAreasInOrderInsideBounds();
    TestLayoutSmallBoundsNeverGoesNegative();
    TestMarkerCentersFollowValues();
    TestBuildItemsCoverEveryArea();
    TestItemsAreDeterministic();

    std::printf("color picker: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
