// Benchmark de quadro interativo: 20 janelas com 10 botoes cada (200 botoes), rotulo com fonte TrueType real.
//
// O que mede (um quadro do host, na ordem em que o host chama a biblioteca):
//   1. layout   : LayoutTree::Layout da arvore inteira
//   2. texto    : para cada rotulo, shaping + LayoutText + GenerateTextRenderItems
//   3. extracao : LayoutSceneBridge::Extract (caixas de acerto e itens de desenho)
//   4. montagem : RenderExtractor::Build com os itens de caixa e de texto
//   5. entrada  : UIEventRouter::Process com o ponteiro percorrendo a tela
// Informativo: nao tem limite de aprovacao. Compile em Debug-otimizado para o numero de referencia.
//
// Exemplo de uso: spriteui_frame_benchmark [quadros]   (padrao: 300 quadros, 20 de aquecimento)

#include "SpriteUi/SpriteUi.hpp"
#include "SpriteUi/Text/TextLayoutCache.hpp"
#include "SpriteUi/Text/TextShaper.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef SPRITEUI_BENCH_FONT_PATH
#define SPRITEUI_BENCH_FONT_PATH "Fonts/Xmind/static/Xmind-Regular.ttf"
#endif

namespace {

using Clock = std::chrono::steady_clock;
using namespace ToolEngine::SpriteUi;

constexpr int kWindowCount = 20;
constexpr int kButtonsPerWindow = 10;
constexpr int kWarmupFrames = 20;

struct PhaseTotals {
    double layout = 0.0;
    double text = 0.0;
    double extract = 0.0;
    double build = 0.0;
    double input = 0.0;
};

double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

std::vector<std::uint8_t> ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Configuracao de texto dos rotulos. Entra na chave do cache: mudar aqui gera outra entrada, nao um erro.
Text::TextLayoutConfig LabelConfig() {
    Text::TextLayoutConfig config;
    config.fontSize = 14.0f;
    config.wrap = Text::TextWrap::None;
    return config;
}

struct Scene {
    LayoutTree tree;
    LayoutSceneBridge bridge;
    // Layout de texto por chave: os 200 rotulos com texto fixo fazem o shaping e a quebra uma vez, e cada quadro so
    // consulta o cache (ver Text/TextLayoutCache.hpp).
    Text::TextLayoutCache layoutCache{512};
    std::vector<UIHandle> buttons;
    std::vector<UIHandle> labels;
    std::vector<std::string> labelText;
    std::vector<Text::TextLayoutKey> labelKeys;   // montadas uma vez, fora do laço quente
};

void BuildScene(Scene& scene) {
    FlexItem root;
    root.width = {SizeMode::Fixed, 1280.0f};
    root.height = {SizeMode::Fixed, 720.0f};
    root.direction = LayoutDirection::Row;
    root.wrap = FlexWrap::Wrap;
    root.gap = 6.0f;
    const UIHandle rootId = scene.tree.Create({}, root);

    for (int w = 0; w < kWindowCount; ++w) {
        FlexItem win;
        win.width = {SizeMode::Fixed, 300.0f};
        win.height = {SizeMode::Fixed, 300.0f};
        win.direction = LayoutDirection::Column;
        win.gap = 2.0f;
        win.padding = {30.0f, 8.0f, 8.0f, 8.0f};
        const UIHandle winId = scene.tree.Create(rootId, win);
        ElementVisual wv;
        wv.clipOverflow = true;
        wv.interactable = true;
        scene.bridge.SetVisual(winId, wv);

        for (int b = 0; b < kButtonsPerWindow; ++b) {
            FlexItem btn;
            btn.width = {SizeMode::Fixed, 284.0f};
            btn.height = {SizeMode::Fixed, 28.0f};
            const UIHandle btnId = scene.tree.Create(winId, btn);
            ElementVisual bv;
            bv.focusable = true;
            bv.tintPremultipliedLinear = {0.05f, 0.2f, 0.55f, 1.0f};
            scene.bridge.SetVisual(btnId, bv);
            scene.buttons.push_back(btnId);

            FlexItem lab;
            lab.width = {SizeMode::Fixed, 270.0f};
            lab.height = {SizeMode::Fixed, 20.0f};
            const UIHandle labId = scene.tree.Create(btnId, lab);
            ElementVisual lv;
            lv.interactable = false;
            scene.bridge.SetVisual(labId, lv);
            scene.labels.push_back(labId);
            scene.labelText.push_back("Botao " + std::to_string(w * kButtonsPerWindow + b));
            scene.labelKeys.push_back(Text::MakeTextLayoutKey(scene.labelText.back(), LabelConfig(), 1u));
        }
    }
}

// Quadro do host. Preenche os tempos de cada fase em totals (acumula) e devolve o numero de instancias.
std::size_t RunFrame(Scene& scene, const Text::ITextShaper& shaper, const Text::TruetypeAtlas& atlas,
                     Input::UIEventRouter& router, std::uint64_t frameId, PhaseTotals& totals,
                     bool record) {
    const Text::TextLayoutConfig config = LabelConfig();

    auto t0 = Clock::now();
    scene.tree.Layout({{0.0f, 0.0f}, {1280.0f, 720.0f}});
    auto t1 = Clock::now();

    std::vector<Rendering::UIRenderItem> textItems;
    textItems.reserve(scene.labels.size() * 16);
    for (std::size_t i = 0; i < scene.labels.size(); ++i) {
        const LayoutElement* lab = scene.tree.Get(scene.labels[i]);
        if (!lab) continue;
        // Shaping e LayoutText so rodam no primeiro quadro (ou quando a chave muda); os demais consultam o cache.
        const Text::TextLayoutResult& layout = scene.layoutCache.GetOrLayout(scene.labelKeys[i], [&] {
            const auto glyphs = shaper.Shape(scene.labelText[i], atlas);
            return Text::LayoutText(scene.labelText[i], glyphs, atlas.metrics, config);
        });
        UILayoutRect box;
        box.resolvedPosition = lab->node.rect.position;
        box.resolvedSize = lab->node.rect.size;
        auto items = Text::GenerateTextRenderItems(layout, box, {1.0f, 1.0f, 1.0f, 1.0f}, 1u,
                                                   Rendering::NoClipIndex, 0.5f, 1000u + i * 64u,
                                                   scene.labels[i]);
        textItems.insert(textItems.end(), items.begin(), items.end());
    }
    auto t2 = Clock::now();

    auto extraction = scene.bridge.Extract(scene.tree);
    auto t3 = Clock::now();

    std::vector<Rendering::UIRenderItem> all = std::move(extraction.renderItems);
    all.insert(all.end(), textItems.begin(), textItems.end());
    Rendering::RenderExtractor extractor;
    const auto packet = extractor.Build(frameId, all, nullptr, extraction.clipHierarchy.effectiveRects);
    auto t4 = Clock::now();

    // Ponteiro andando sobre a grade de botoes: move em todo quadro, com um press e release a cada 60 quadros.
    Input::UIInputSnapshot input;
    const float phase = static_cast<float>(frameId % 600) * 2.0f;
    input.pointerPositionLogical = {20.0f + phase, 40.0f + static_cast<float>(frameId % 300)};
    input.pointerDeltaLogical = {2.0f, 1.0f};
    input.frameId = frameId;
    input.windowFocused = true;
    input.primaryDown = true;
    const auto events = router.Process(input, extraction.hitTargets);
    auto t5 = Clock::now();

    if (record) {
        totals.layout += Ms(t0, t1);
        totals.text += Ms(t1, t2);
        totals.extract += Ms(t2, t3);
        totals.build += Ms(t3, t4);
        totals.input += Ms(t4, t5);
    }
    (void)events;
    return packet.instances.size();
}

} // namespace

int main(int argc, char** argv) {
    int frames = 300;
    if (argc > 1) frames = std::max(1, std::atoi(argv[1]));

    const std::vector<std::uint8_t> font = ReadFile(SPRITEUI_BENCH_FONT_PATH);
    Text::TruetypeAtlas atlas;
    if (font.empty() || !Text::BuildTruetypeAtlas(font, Text::TruetypeAtlasConfig{}, atlas)) {
        std::fprintf(stderr, "Fonte nao carregada: %s\n", SPRITEUI_BENCH_FONT_PATH);
        return 1;
    }

    Scene scene;
    BuildScene(scene);
    Text::CodepointShaper shaper;
    Input::UIEventRouter router;

    std::uint64_t submitted = 0;
    PhaseTotals scratch;
    for (int i = 0; i < kWarmupFrames; ++i) {
        submitted += RunFrame(scene, shaper, atlas, router, static_cast<std::uint64_t>(i + 1), scratch, false);
    }
    PhaseTotals totals;
    const auto start = Clock::now();
    for (int i = 0; i < frames; ++i) {
        submitted += RunFrame(scene, shaper, atlas, router, static_cast<std::uint64_t>(kWarmupFrames + i + 1),
                              totals, true);
    }
    const double total = Ms(start, Clock::now());

    const double n = static_cast<double>(frames);
    std::printf("SpriteUI benchmark de quadro: %d janelas, %d botoes, %d quadros (fonte: %s)\n", kWindowCount,
                kWindowCount * kButtonsPerWindow, frames, SPRITEUI_BENCH_FONT_PATH);
    std::printf("  layout   %8.4f ms/quadro\n", totals.layout / n);
    std::printf("  texto    %8.4f ms/quadro\n", totals.text / n);
    std::printf("  extracao %8.4f ms/quadro\n", totals.extract / n);
    std::printf("  montagem %8.4f ms/quadro\n", totals.build / n);
    std::printf("  entrada  %8.4f ms/quadro\n", totals.input / n);
    std::printf("  TOTAL    %8.4f ms/quadro (%.1f quadros/s) [sinal de controle %llu]\n", total / n,
                n * 1000.0 / total, static_cast<unsigned long long>(submitted));
    return 0;
}
