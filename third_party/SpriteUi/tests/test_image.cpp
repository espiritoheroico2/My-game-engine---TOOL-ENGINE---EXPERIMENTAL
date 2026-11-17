// Imagem (Image) e botao de imagem (ImageButton): regiao UV limitada, modos de ajuste (esticar, conter, preencher),
// marcador de erro sem textura, nine-slice e estados do botao. Valores calculados a mao nos comentarios.
#include "SpriteUi/Elements/Image.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

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

bool SameRect(Rect a, Rect b) {
    return Near(a.position.x, b.position.x) && Near(a.position.y, b.position.y) && Near(a.size.x, b.size.x) &&
           Near(a.size.y, b.size.y);
}

bool SameUv(const std::array<float, 4>& uv, float u, float v, float w, float h) {
    return Near(uv[0], u) && Near(uv[1], v) && Near(uv[2], w) && Near(uv[3], h);
}

PointerEvent PointerAt(PointerPhase phase, Vec2 position, bool inside) {
    PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

const Rect kBox{{0.0f, 0.0f}, {200.0f, 100.0f}};  // Area de 2:1.

// Regiao UV fora de [0, 1] e limitada: u = 0 (era -0,5), v = 0,2, largura 1 (era 2, cabe de u 0), altura 0,8.
void TestUvRegionIsClamped() {
    ImageSource source;
    source.uv[0] = -0.5f;
    source.uv[1] = 0.2f;
    source.uv[2] = 2.0f;
    source.uv[3] = 2.0f;
    CHECK(SameUv(ImageVisibleUv(kBox, source), 0.0f, 0.2f, 1.0f, 0.8f));
    source.uv[0] = std::numeric_limits<float>::quiet_NaN();
    source.uv[1] = 0.0f;
    source.uv[2] = 1.0f;
    source.uv[3] = 1.0f;
    CHECK(SameUv(ImageVisibleUv(kBox, source), 0.0f, 0.0f, 1.0f, 1.0f));
}

// Esticar ocupa a area toda, mesmo com a proporcao da textura diferente.
void TestStretchFillsBox() {
    CHECK(SameRect(ImageDrawRect(kBox, {400.0f, 400.0f}, ImageFit::Stretch), kBox));
}

// Conter, textura quadrada (100 x 100) em 200 x 100: escala min(2, 1) = 1; tamanho 100 x 100 centrado em x 50.
void TestContainKeepsRatioAndCenters() {
    CHECK(SameRect(ImageDrawRect(kBox, {100.0f, 100.0f}, ImageFit::Contain), {{50.0f, 0.0f}, {100.0f, 100.0f}}));
    // Textura 400 x 100 (proporcao 4): escala min(0,5; 1) = 0,5; tamanho 200 x 50, centrado em y 25.
    CHECK(SameRect(ImageDrawRect(kBox, {400.0f, 100.0f}, ImageFit::Contain), {{0.0f, 25.0f}, {200.0f, 50.0f}}));
}

// Preencher corta sem distorcer. Textura 100 x 100 (1:1) em area 2:1: sobra 1/2 da altura; corta 1/4 de
// cada lado: v = 0,25 e altura 0,5. Textura 400 x 100 (4:1): corta nos lados: u = 0,25 e largura 0,5.
void TestFillCropsCentered() {
    ImageSource source;
    source.fit = ImageFit::Fill;
    source.textureSize = {100.0f, 100.0f};
    CHECK(SameUv(ImageVisibleUv(kBox, source), 0.0f, 0.25f, 1.0f, 0.5f));
    source.textureSize = {400.0f, 100.0f};
    CHECK(SameUv(ImageVisibleUv(kBox, source), 0.25f, 0.0f, 0.5f, 1.0f));
    CHECK(SameRect(ImageDrawRect(kBox, {400.0f, 100.0f}, ImageFit::Fill), kBox));
}

// Imagem comum gera um item com o slice e a regiao UV da fonte.
void TestImageBuildsOneItem() {
    const ImageStyle style;
    ImageSource source;
    source.textureSlice = 7;
    const auto items = BuildImageSourceItems(InvalidUIHandle, kBox, source, {1, 1, 1, 1}, style, 0, 0.0f, 0);
    CHECK(items.size() == 1);
    CHECK(items[0].textureSlice == 7);
    CHECK(Near(items[0].uvRect[2], 1.0f) && Near(items[0].uvRect[3], 1.0f));
}

// Textura ausente: fundo magenta e um X (dois tracos). Nenhuma imagem comum e desenhada.
void TestMissingTextureShowsErrorMarker() {
    const ImageStyle style;
    ImageSource source;
    source.loaded = false;
    const auto items = BuildImageSourceItems(InvalidUIHandle, kBox, source, {1, 1, 1, 1}, style, 0, 0.0f, 0);
    CHECK(items.size() == 3);
    CHECK(items[0].tintPremultipliedLinear.x == style.missingFill.x);
}

// Nine-slice: 3 x 3 regioes. Com a textura 60 x 60 e bordas de 10 texels, todas as nove regioes existem.
void TestNineSliceBuildsNineRegions() {
    const ImageStyle style;
    ImageSource source;
    source.nineSlice = true;
    source.textureSize = {60.0f, 60.0f};
    source.borders = {10.0f, 10.0f, 10.0f, 10.0f};
    const auto items = BuildImageSourceItems(InvalidUIHandle, kBox, source, {1, 1, 1, 1}, style, 0, 0.0f, 0);
    CHECK(items.size() == 9);
}

// Botao: clique completo (Down e Up dentro) dispara; soltar fora nao; desabilitado nao reage.
void TestImageButtonClick() {
    ImageButton button;
    button.border = kBox;
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Down, {50.0f, 50.0f}, true)));
    CHECK(button.OnPointer(PointerAt(PointerPhase::Up, {50.0f, 50.0f}, true)));

    (void)button.OnPointer(PointerAt(PointerPhase::Down, {50.0f, 50.0f}, true));
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Up, {500.0f, 50.0f}, false)));

    button.states.disabled = true;
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Down, {50.0f, 50.0f}, true)));
    CHECK(!button.OnPointer(PointerAt(PointerPhase::Up, {50.0f, 50.0f}, true)));
}

// Enter ou Espaco ativam so com foco e habilitado.
void TestImageButtonKeyActivatesWithFocus() {
    ImageButton button;
    CHECK(!button.KeyActivate());
    button.states.focused = true;
    CHECK(button.KeyActivate());
    button.states.disabled = true;
    CHECK(!button.KeyActivate());
}

// Cor do botao: pressionado escurece para 0,7; desabilitado fica em 0,4; normal fica em 1.
void TestImageButtonTintFollowsState() {
    const ImageStyle style;
    ImageButton button;
    button.border = kBox;
    button.source.textureSlice = 3;
    auto items = BuildImageButtonItems(button, style, 0, 0.0f, 0);
    CHECK(items.size() == 1 && Near(items[0].tintPremultipliedLinear.x, 1.0f));

    (void)button.OnPointer(PointerAt(PointerPhase::Down, {50.0f, 50.0f}, true));
    items = BuildImageButtonItems(button, style, 0, 0.0f, 0);
    CHECK(Near(items[0].tintPremultipliedLinear.x, 0.7f));
    (void)button.OnPointer(PointerAt(PointerPhase::Up, {50.0f, 50.0f}, true));

    button.states.disabled = true;
    items = BuildImageButtonItems(button, style, 0, 0.0f, 0);
    CHECK(Near(items[0].tintPremultipliedLinear.x, 0.4f));
}

// Foco sem desabilitar: a imagem ganha o contorno (4 traços). 1 + 4 = 5 itens.
void TestImageButtonFocusRing() {
    const ImageStyle style;
    ImageButton button;
    button.border = kBox;
    button.states.focused = true;
    CHECK(BuildImageButtonItems(button, style, 0, 0.0f, 0).size() == 5);
}

} // namespace

int main() {
    TestUvRegionIsClamped();
    TestStretchFillsBox();
    TestContainKeepsRatioAndCenters();
    TestFillCropsCentered();
    TestImageBuildsOneItem();
    TestMissingTextureShowsErrorMarker();
    TestNineSliceBuildsNineRegions();
    TestImageButtonClick();
    TestImageButtonKeyActivatesWithFocus();
    TestImageButtonTintFollowsState();
    TestImageButtonFocusRing();

    std::printf("image: %d checks, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}
