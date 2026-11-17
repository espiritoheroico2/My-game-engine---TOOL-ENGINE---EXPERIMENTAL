// Primitivas de desenho (Primitives.hpp): retangulo solido, linha girada e retangulo de cantos arredondados.
// As geometrias sao conferidas pela mesma transformacao que o RenderExtractor aplica. Sem janela.
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Elements/Paint.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
namespace R = ToolEngine::SpriteUi::Rendering;

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

constexpr UIHandle kOwner{5, 1};
constexpr Vec4 kColor{0.2f, 0.4f, 0.6f, 1.0f};

// Ponto da forma em coordenadas da tela, pela transformacao do quad unitario.
Vec2 ToScreen(const R::UIRenderItem& item, Vec2 unitPoint) {
    const auto affine = BuildUnitQuadTransform(item.layout, item.transform);
    if (!affine) return {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    return affine->TransformPoint(unitPoint);
}

float Distance(Vec2 a, Vec2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void TestSolidRectKeepsGeometryAndColor() {
    const R::UIRenderItem item =
        R::MakeSolidRect(kOwner, {{3.0f, 4.0f}, {50.0f, 20.0f}}, kColor, R::NoClipIndex, 0.25f, 7);
    CHECK(item.layout.resolvedPosition.x == 3.0f && item.layout.resolvedPosition.y == 4.0f);
    CHECK(item.layout.resolvedSize.x == 50.0f && item.layout.resolvedSize.y == 20.0f);
    CHECK(item.tintPremultipliedLinear.x == kColor.x && item.tintPremultipliedLinear.w == kColor.w);
    CHECK(item.textureSlice == R::WhiteTextureSlice);
    CHECK(item.paintOrder == 7);
    CHECK(Near(ToScreen(item, {1.0f, 1.0f}).x, 53.0f));
    CHECK(Near(ToScreen(item, {1.0f, 1.0f}).y, 24.0f));
}

void TestLineEndpointsMatchFromAndTo() {
    const auto line = R::MakeLine(kOwner, {10.0f, 20.0f}, {110.0f, 70.0f}, 4.0f, kColor);
    CHECK(line.has_value());
    if (!line) return;
    const Vec2 start = ToScreen(*line, {0.0f, 0.5f});
    const Vec2 end = ToScreen(*line, {1.0f, 0.5f});
    CHECK(Near(start.x, 10.0f) && Near(start.y, 20.0f));
    CHECK(Near(end.x, 110.0f) && Near(end.y, 70.0f));
}

void TestLineHasRequestedThickness() {
    // Vertical de (0, 0) a (0, 50) com espessura 6: os dois lados do retangulo distam 6 px e o comprimento
    // e 50.
    const auto line = R::MakeLine(kOwner, {0.0f, 0.0f}, {0.0f, 50.0f}, 6.0f, kColor);
    CHECK(line.has_value());
    if (!line) return;
    CHECK(Near(Distance(ToScreen(*line, {0.0f, 0.0f}), ToScreen(*line, {0.0f, 1.0f})), 6.0f));
    CHECK(Near(Distance(ToScreen(*line, {0.0f, 0.5f}), ToScreen(*line, {1.0f, 0.5f})), 50.0f));
}

void TestLineRejectsDegenerateInput() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!R::MakeLine(kOwner, {5.0f, 5.0f}, {5.0f, 5.0f}, 2.0f, kColor).has_value()); // ponto so
    CHECK(!R::MakeLine(kOwner, {0.0f, 0.0f}, {9.0f, 9.0f}, 0.0f, kColor).has_value()); // espessura zero
    CHECK(!R::MakeLine(kOwner, {0.0f, 0.0f}, {9.0f, 9.0f}, -1.0f, kColor).has_value());
    CHECK(!R::MakeLine(kOwner, {nan, 0.0f}, {9.0f, 9.0f}, 2.0f, kColor).has_value());
}

void TestRoundedRectClampsRadius() {
    const Rect rect{{0.0f, 0.0f}, {100.0f, 40.0f}};
    const auto normal = R::MakeRoundedRect(kOwner, rect, 10.0f, kColor);
    CHECK(normal.has_value());
    if (normal) {
        CHECK((normal->flags & R::FlagRoundedPanel) != 0u);
        CHECK(Near(normal->cornerRadius, 10.0f));
    }

    const auto huge = R::MakeRoundedRect(kOwner, rect, 999.0f, kColor);
    CHECK(huge.has_value() && Near(huge->cornerRadius, 20.0f)); // metade do menor lado (40)

    const auto negative = R::MakeRoundedRect(kOwner, rect, -5.0f, kColor);
    CHECK(negative.has_value() && negative->cornerRadius == 0.0f);

    const auto notFinite = R::MakeRoundedRect(kOwner, rect, std::numeric_limits<float>::quiet_NaN(), kColor);
    CHECK(notFinite.has_value() && notFinite->cornerRadius == 0.0f);

    CHECK(!R::MakeRoundedRect(kOwner, {{0.0f, 0.0f}, {0.0f, 40.0f}}, 4.0f, kColor).has_value());
}

// AppendBox com raio: o contorno e a caixa externa; por dentro, a caixa do fundo recuada pela largura. Os raios
// sao prendidos a metade do menor lado de cada caixa; raio zero, negativo ou nao finito deixa a caixa reta.
void TestBoxRadiusIsClampedPerBox() {
    namespace E = ToolEngine::SpriteUi::Elements;
    std::vector<R::UIRenderItem> items;
    std::uint64_t paint = 0;
    const Rect box{{0.0f, 0.0f}, {100.0f, 40.0f}};
    E::AppendBox(kOwner, box, 999.0f, 2.0f, kColor, kColor, R::NoClipIndex, 0.0f, paint, items);
    CHECK(items.size() == 2);
    if (items.size() == 2) {
        CHECK(Near(items[0].cornerRadius, 20.0f)); // contorno: metade de 40
        CHECK(Near(items[1].cornerRadius, 18.0f)); // fundo 96 x 36: metade de 36
        CHECK(Near(items[1].layout.resolvedSize.x, 96.0f) && Near(items[1].layout.resolvedSize.y, 36.0f));
        CHECK(Near(items[1].layout.resolvedPosition.x, 2.0f) && Near(items[1].layout.resolvedPosition.y, 2.0f));
    }

    items.clear();
    E::AppendBox(kOwner, box, 6.0f, 1.0f, kColor, kColor, R::NoClipIndex, 0.0f, paint, items);
    CHECK(items.size() == 2 && Near(items[1].cornerRadius, 5.0f)); // raio interno = raio - largura
}

void TestBoxWithoutRadiusStaysSquare() {
    namespace E = ToolEngine::SpriteUi::Elements;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float radii[] = {0.0f, -4.0f, nan};
    for (float radius : radii) {
        std::vector<R::UIRenderItem> items;
        std::uint64_t paint = 0;
        E::AppendBox(kOwner, {{0.0f, 0.0f}, {60.0f, 30.0f}}, radius, 1.0f, kColor, kColor, R::NoClipIndex, 0.0f,
                     paint, items);
        CHECK(items.size() == 5); // fundo e as quatro faixas do contorno
        for (const R::UIRenderItem& item : items) CHECK((item.flags & R::FlagRoundedPanel) == 0u);
    }
}

} // namespace

int main() {
    TestSolidRectKeepsGeometryAndColor();
    TestLineEndpointsMatchFromAndTo();
    TestLineHasRequestedThickness();
    TestLineRejectsDegenerateInput();
    TestRoundedRectClampsRadius();
    TestBoxRadiusIsClampedPerBox();
    TestBoxWithoutRadiusStaysSquare();

    std::printf("Primitives: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
