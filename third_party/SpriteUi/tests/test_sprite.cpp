// Sprite (Sprite.hpp): formato de sprite com atlas, UV, tamanho em pixels, ancoras e alfa.
// Confere se a rotacao gira em volta de anchors[0] (um canto fica no lugar ao girar 90 graus) e se o
// item chega ao RenderExtractor com a textura e o alfa certos. Sem janela.
#include "SpriteUi/Core/Transform2D.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/Sprite.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>
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

constexpr UIHandle kOwner{7, 1};

// Ponto local do sprite (em pixels) levado para a tela pela transformacao do item.
Vec2 ToScreen(const R::UIRenderItem& item, Vec2 localPixel) {
    const auto affine = BuildUnitQuadTransform(item.layout, item.transform);
    const Vec2 unit{localPixel.x / item.layout.resolvedSize.x, localPixel.y / item.layout.resolvedSize.y};
    return {affine->m00 * unit.x + affine->m01 * unit.y + affine->tx,
            affine->m10 * unit.x + affine->m11 * unit.y + affine->ty};
}

R::UISprite MakeCoin() {
    R::UISprite sprite;
    sprite.atlasId = 3;
    sprite.uvRect = {0.25f, 0.5f, 0.125f, 0.25f};
    sprite.sizePixels = {20.0f, 10.0f};
    sprite.anchors = {{0.0f, 0.0f}};
    sprite.alpha = 0.5f;
    return sprite;
}

void TestSpriteFormatIsValidated() {
    R::UISprite sprite = MakeCoin();
    CHECK(sprite.IsValid());

    R::UISprite noSize = sprite;
    noSize.sizePixels = {0.0f, 10.0f};
    CHECK(!noSize.IsValid());

    R::UISprite badAnchor = sprite;
    badAnchor.anchors = {{std::nanf(""), 0.0f}};
    CHECK(!badAnchor.IsValid());

    R::UISprite emptyUv = sprite;
    emptyUv.uvRect[2] = 0.0f;
    CHECK(!emptyUv.IsValid());

    CHECK(!R::MakeSpriteItem(kOwner, noSize, {0.0f, 0.0f}).has_value());
}

void TestRotationKeepsMainAnchorInPlace() {
    // Ancora no canto superior esquerdo: ao girar 90 graus, o canto continua no mesmo ponto da tela.
    R::UISprite sprite = MakeCoin();
    const Vec2 target{100.0f, 50.0f};
    const auto item = R::MakeSpriteItem(kOwner, sprite, target, std::numbers::pi_v<float> * 0.5f);
    CHECK(item.has_value());
    if (!item) return;

    const Vec2 corner = ToScreen(*item, {0.0f, 0.0f});
    CHECK(Near(corner.x, target.x) && Near(corner.y, target.y));

    // O canto oposto (20, 10) foi para o lado: a rotacao de 90 graus troca a largura pela altura.
    const Vec2 opposite = ToScreen(*item, {20.0f, 10.0f});
    CHECK(Near(opposite.x, target.x - 10.0f) && Near(opposite.y, target.y + 20.0f));
}

void TestMainAnchorIsFirstAndCenterIsDefault() {
    R::UISprite withSecond = MakeCoin();
    withSecond.anchors = {{0.0f, 0.0f}, {10.0f, 5.0f}};
    CHECK(Near(withSecond.MainAnchor().x, 0.0f) && Near(withSecond.MainAnchor().y, 0.0f));

    R::UISprite noAnchors = MakeCoin();
    noAnchors.anchors.clear();
    CHECK(Near(noAnchors.MainAnchor().x, 10.0f) && Near(noAnchors.MainAnchor().y, 5.0f));
}

void TestItemReachesExtractorWithAtlasAndAlpha() {
    const R::UISprite sprite = MakeCoin();
    const auto item = R::MakeSpriteItem(kOwner, sprite, {0.0f, 0.0f});
    CHECK(item.has_value());
    if (!item) return;

    R::ExtractionStats stats;
    const std::vector<R::UIRenderItem> items{*item};
    const R::FramePacket packet = R::RenderExtractor{}.Build(1, items, &stats);
    CHECK(packet.instances.size() == 1);
    CHECK(stats.batchCount == 1);
    if (packet.instances.size() != 1) return;

    const R::SpriteInstanceGPU& instance = packet.instances.front();
    CHECK(instance.resourceIds[0] == 3);
    CHECK(Near(instance.uvRect[0], 0.25f) && Near(instance.uvRect[3], 0.25f));
    CHECK(Near(instance.tint[3], 0.5f));
}

} // namespace

int main() {
    TestSpriteFormatIsValidated();
    TestRotationKeepsMainAnchorInPlace();
    TestMainAnchorIsFirstAndCenterIsDefault();
    TestItemReachesExtractorWithAtlasAndAlpha();

    std::printf("Sprite: %d checks, %d falhas\n", run, failed);
    return failed == 0 ? 0 : 1;
}
