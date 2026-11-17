// Captura de um layout com tres janelas encaixadas (docking), com a fonte real do projeto (Fonts/).
// Monta a arvore com ApplyDropResult (A a esquerda; B a direita de A; C abaixo de B), arranja e desenha cada
// folha como uma janela: corpo arredondado, barra de titulo e linhas de texto.
//
// Uso: spriteui_docking_capture <arquivo.png de saida>. Confere pixels e falha se o layout nao bater.
// Exemplo: a janela Cena fica no canto superior direito e Propriedades abaixo dela, sem sobreposicao.

#include "SpriteUi/Elements/Label.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/DockOps.hpp"
#include "SpriteUi/Widgets/Docking.hpp"
#include "Visual/VisualHarness.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef SPRITEUI_FONT_PATH
#error "SPRITEUI_FONT_PATH must be defined by CMake"
#endif

namespace {

using namespace SpriteUiTests::Visual;
namespace SU = ToolEngine::SpriteUi;
namespace R = ToolEngine::SpriteUi::Rendering;
namespace Txt = ToolEngine::SpriteUi::Text;
namespace W = ToolEngine::SpriteUi::Widgets;
namespace Elm = ToolEngine::SpriteUi::Elements;

constexpr std::uint32_t kFontSlice = 1;
constexpr int kWidth = 900;
constexpr int kHeight = 560;
constexpr SU::Vec4 kBackground{0.075f, 0.085f, 0.105f, 1.0f};
int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "[FAIL] %s\n", what);
    }
}

// Atlas de 0x20 a 0xFF com a fonte do projeto, como nas capturas de elementos.
Txt::TruetypeAtlas LoadProjectFontAtlas() {
    std::ifstream file(SPRITEUI_FONT_PATH, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    Txt::TruetypeAtlasConfig config;
    config.rasterPixelSize = 32.0f;
    config.firstCodepoint = 0x20;
    config.lastCodepoint = 0xFF;
    Txt::TruetypeAtlas atlas;
    if (!Txt::BuildTruetypeAtlas(bytes, config, atlas)) {
        std::fprintf(stderr, "[FAIL] nao foi possivel montar o atlas de %s\n", SPRITEUI_FONT_PATH);
        std::exit(1);
    }
    return atlas;
}

// Amostra o alfa do atlas com filtro bilinear, como a GPU faz. Sem ele, o glifo de 32 px reduzido para 14 px
// sai serrilhado e ilegivel.
TextureFunction AtlasSampler(const Txt::TruetypeAtlas& atlas) {
    return [&atlas](std::uint32_t slice, SU::Vec2 uv) -> SU::Vec4 {
        if (slice != kFontSlice) return {1.0f, 1.0f, 1.0f, 1.0f};
        const float fx = std::clamp(uv.x * static_cast<float>(atlas.width) - 0.5f, 0.0f,
                                    static_cast<float>(atlas.width) - 1.0f);
        const float fy = std::clamp(uv.y * static_cast<float>(atlas.height) - 0.5f, 0.0f,
                                    static_cast<float>(atlas.height) - 1.0f);
        const auto x0 = static_cast<std::size_t>(fx);
        const auto y0 = static_cast<std::size_t>(fy);
        const std::size_t x1 = std::min<std::size_t>(x0 + 1, atlas.width - 1);
        const std::size_t y1 = std::min<std::size_t>(y0 + 1, atlas.height - 1);
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);
        const auto texel = [&](std::size_t x, std::size_t y) {
            return static_cast<float>(atlas.pixels[y * atlas.width + x]) / 255.0f;
        };
        const float top = texel(x0, y0) * (1.0f - tx) + texel(x1, y0) * tx;
        const float bottom = texel(x0, y1) * (1.0f - tx) + texel(x1, y1) * tx;
        const float alpha = top * (1.0f - ty) + bottom * ty;
        return {alpha, alpha, alpha, alpha};
    };
}

SU::Vec4 Linear(SU::Vec4 srgb) {
    return R::SrgbStraightToLinearPremultiplied(srgb);
}

// Rotulo de texto na caixa dada (absoluta, a partir da raiz em (0, 0)).
SU::UIHandle AddLabel(Elm::ElementTree& tree, SU::UIHandle root, const std::string& text, SU::Rect box,
                      float fontSize, SU::Vec4 srgb) {
    const SU::UIHandle handle = tree.Create<Elm::Label>(root);
    Elm::Label* label = tree.GetAs<Elm::Label>(handle);
    label->transform.offsetMin = box.position;
    label->transform.offsetMax = box.position + box.size;
    label->text = text;
    label->fontSize = fontSize;
    label->horizontal = Elm::HorizontalAlign::Left;
    label->vertical = Elm::VerticalAlign::Middle;
    label->color = Linear(srgb);
    return handle;
}

// Desenha uma folha do layout como janela: corpo, barra de titulo, titulo e linhas de texto (rotulos).
void DrawWindow(Scene& scene, Elm::ElementTree& tree, SU::UIHandle root, const SU::Rect& bounds,
                const std::string& title, const std::vector<std::string>& lines,
                std::vector<SU::UIHandle>& labels) {
    const SU::Rect body{{bounds.position.x + 1.0f, bounds.position.y + 1.0f},
                        {bounds.size.x - 2.0f, bounds.size.y - 2.0f}};
    scene.Rect(body, {0.13f, 0.14f, 0.18f, 1.0f}, R::WhiteTextureSlice, R::FlagRoundedPanel, 8.0f);
    const SU::Rect bar{body.position, {body.size.x, 30.0f}};
    scene.Rect(bar, {0.18f, 0.20f, 0.27f, 1.0f});
    labels.push_back(AddLabel(tree, root, title, {{bar.position.x + 12.0f, bar.position.y}, {body.size.x - 24.0f, 30.0f}},
                              15.0f, {0.94f, 0.95f, 0.97f, 1.0f}));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = bar.position.y + 40.0f + 26.0f * static_cast<float>(i);
        labels.push_back(AddLabel(tree, root, lines[i], {{body.position.x + 14.0f, y}, {body.size.x - 28.0f, 22.0f}},
                                  14.0f, {0.78f, 0.81f, 0.86f, 1.0f}));
    }
}

// Layout real de um rotulo: shaping com o atlas do projeto e uma linha, sem quebra.
Txt::TextLayoutResult LayoutLabelText(const Elm::Label& label, const Txt::TruetypeAtlas& atlas) {
    const auto glyphs = Txt::ShapeTruetype(label.text, atlas);
    return Txt::LayoutText(label.text, glyphs, atlas.metrics, Elm::MakeLabelLayoutConfig(label));
}

// Janela da folha que contem o conteudo, ou um retangulo vazio se nao existe.
SU::Rect LeafBounds(W::UIDockNode& root, SU::UIHandle content) {
    const W::UIDockNode* leaf = W::FindLeafContaining(root, content);
    return leaf ? leaf->bounds : SU::Rect{};
}

Image DockingScene(const Txt::TruetypeAtlas& atlas) {
    const SU::UIHandle hierarchy{1, 1};
    const SU::UIHandle scene3d{2, 1};
    const SU::UIHandle properties{3, 1};

    W::UIDockNode root(hierarchy);
    // Encaixe real pelas acoes do controlador: B a direita de A, depois C abaixo de B.
    Expect(W::ApplyDropResult(root, scene3d, W::DockDropResult{W::DockAction::Dock, W::DockSlot::Right, hierarchy},
                              0.32f),
           "encaixa Cena a direita");
    Expect(W::ApplyDropResult(root, properties,
                              W::DockDropResult{W::DockAction::Dock, W::DockSlot::Bottom, scene3d}, 0.55f),
           "encaixa Propriedades abaixo de Cena");
    const SU::Rect screen{{0.0f, 0.0f}, {static_cast<float>(kWidth), static_cast<float>(kHeight)}};
    root.Layout(screen);

    const SU::Rect hierarchyBox = LeafBounds(root, hierarchy);
    const SU::Rect sceneBox = LeafBounds(root, scene3d);
    const SU::Rect propertiesBox = LeafBounds(root, properties);
    Expect(hierarchyBox.size.x > 0.0f && sceneBox.size.x > 0.0f && propertiesBox.size.y > 0.0f,
           "as tres folhas tem area");
    Expect(sceneBox.position.x >= hierarchyBox.position.x + hierarchyBox.size.x - 0.5f,
           "Cena fica a direita de Hierarquia");
    Expect(propertiesBox.position.y >= sceneBox.position.y + sceneBox.size.y - 0.5f,
           "Propriedades fica abaixo de Cena");

    Elm::ElementTree tree;
    const SU::UIHandle textRoot = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(textRoot)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(textRoot)->transform.offsetMax = {0.0f, 0.0f};
    tree.Arrange(textRoot, screen);

    Scene scene(kBackground);
    std::vector<SU::UIHandle> labels;
    DrawWindow(scene, tree, textRoot, hierarchyBox, "Hierarquia",
               {"Camera principal", "Terreno", "Personagem", "Luz do sol"}, labels);
    DrawWindow(scene, tree, textRoot, sceneBox, "Cena", {"Vista: perspectiva", "Grade: ligada", "Zoom: 100%"}, labels);
    DrawWindow(scene, tree, textRoot, propertiesBox, "Propriedades", {"Posicao: 0, 1, 0", "Escala: 1, 1, 1"}, labels);
    tree.Arrange(textRoot, screen);

    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    std::uint64_t paint = 100;
    for (const SU::UIHandle handle : labels) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const auto layout = LayoutLabelText(*label, atlas);
        target.paintOrder = paint;
        paint += 100;
        const auto items = Elm::BuildLabelItems(*label, layout, label->ContentRect(), target, R::NoClipIndex);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
    }

    Image image = scene.Render(kWidth, kHeight, 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, kBackground) > 150000, "as tres janelas cobrem a tela");
    return image;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "uso: %s <arquivo.png>\n", argv[0]);
        return 2;
    }

    const Txt::TruetypeAtlas atlas = LoadProjectFontAtlas();
    const Image image = DockingScene(atlas);

    std::ofstream file(argv[1], std::ios::binary);
    const auto png = EncodePng(image);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        std::fprintf(stderr, "[FAIL] nao foi possivel gravar %s\n", argv[1]);
        return 1;
    }
    std::printf("docking captura: %s (%d falhas)\n", argv[1], failures);
    return failures == 0 ? 0 : 1;
}
