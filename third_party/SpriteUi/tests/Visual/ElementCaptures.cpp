// Capturas dos elementos novos (Elements/) com a fonte real do projeto (Fonts/). Cada cena monta uma
// ElementTree, arranja, gera os itens de cada elemento e os desenha pelo renderizador de software do harness.
//
// Uso: spriteui_element_captures <pasta de saida>. Grava um PNG por cena e confere pixels; se algum
// pixel nao bater, o programa termina com erro. A fonte e obrigatoria: sem ela o texto vira caixas.

#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/MenuBar.hpp"
#include "SpriteUi/Widgets/Menu.hpp"
#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Elements/Window.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/Label.hpp"
#include "SpriteUi/Elements/Graph.hpp"
#include "SpriteUi/Elements/Image.hpp"
#include "SpriteUi/Elements/ListBox.hpp"
#include "SpriteUi/Elements/RadioButton.hpp"
#include "SpriteUi/Elements/Toggle.hpp"
#include "SpriteUi/Elements/TabBar.hpp"
#include "SpriteUi/Elements/TextField.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "Visual/LogoPixels.inc"
#include "Visual/VisualHarness.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <filesystem>
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
namespace Elm = ToolEngine::SpriteUi::Elements;
namespace Txt = ToolEngine::SpriteUi::Text;
namespace R = ToolEngine::SpriteUi::Rendering;
namespace W = ToolEngine::SpriteUi::Widgets;

// Slice de textura do atlas da fonte. O slice 0 e o branco do harness.
constexpr std::uint32_t kFontSlice = 1;
int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "[FAIL] %s\n", what);
    }
}

// Atlas de 0x20 a 0xFF (ASCII e Latin-1), rasterizado em 32 px para tamanhos de 12 a 24 px.
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

// Amostra o alfa do atlas: a cobertura do glifo vira o alfa do texto, como no shader de texto de alfa.
TextureFunction AtlasSampler(const Txt::TruetypeAtlas& atlas) {
    return [&atlas](std::uint32_t slice, SU::Vec2 uv) -> SU::Vec4 {
        if (slice != kFontSlice) return {1.0f, 1.0f, 1.0f, 1.0f};
        const int x = static_cast<int>(uv.x * static_cast<float>(atlas.width));
        const int y = static_cast<int>(uv.y * static_cast<float>(atlas.height));
        const int cx = std::clamp(x, 0, static_cast<int>(atlas.width) - 1);
        const int cy = std::clamp(y, 0, static_cast<int>(atlas.height) - 1);
        const float alpha = static_cast<float>(atlas.pixels[static_cast<std::size_t>(cy) * atlas.width + cx]) / 255.0f;
        return {alpha, alpha, alpha, alpha};
    };
}

// Layout real de um rotulo: shaping com o atlas do projeto e uma linha, sem quebra.
Txt::TextLayoutResult LayoutLabelText(const Elm::Label& label, const Txt::TruetypeAtlas& atlas) {
    const auto glyphs = Txt::ShapeTruetype(label.text, atlas);
    return Txt::LayoutText(label.text, glyphs, atlas.metrics, Elm::MakeLabelLayoutConfig(label));
}

SU::Vec4 Linear(SU::Vec4 srgb) {
    return R::SrgbStraightToLinearPremultiplied(srgb);
}

// Cena 1: rotulos alinhados em tres posicoes e um rotulo maior que a caixa, recortado.
Image LabelScene(const Txt::TruetypeAtlas& atlas) {
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    tree.Get(root)->padding = {12, 12, 12, 12};

    struct Row {
        const char* text;
        Elm::HorizontalAlign horizontal;
        float width;
    };
    const Row rows[] = {
        {"Esquerda: texto alinhado a esquerda", Elm::HorizontalAlign::Left, 300.0f},
        {"Centro: texto centralizado", Elm::HorizontalAlign::Center, 300.0f},
        {"Direita: texto alinhado a direita", Elm::HorizontalAlign::Right, 300.0f},
        {"Este texto e maior que a caixa e sai recortado", Elm::HorizontalAlign::Left, 200.0f},
    };

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    scene.Rect({{20, 20}, {340, 230}}, {0.13f, 0.14f, 0.17f, 1.0f});

    // Cada linha: uma celula (caixa de fundo) e, como filho dela, o rotulo que preenche a celula.
    std::vector<SU::UIHandle> cells;
    std::vector<SU::UIHandle> labels;
    for (std::size_t i = 0; i < std::size(rows); ++i) {
        const float top = 32.0f + static_cast<float>(i) * 54.0f;
        const SU::UIHandle cell = tree.Create<Elm::Element>(root);
        tree.Get(cell)->transform.anchorMax = {0.0f, 0.0f};
        tree.Get(cell)->transform.offsetMin = {32.0f, top};
        tree.Get(cell)->transform.offsetMax = {32.0f + rows[i].width, top + 44.0f};
        cells.push_back(cell);

        const SU::UIHandle labelHandle = tree.Create<Elm::Label>(cell);
        Elm::Label* label = tree.GetAs<Elm::Label>(labelHandle);
        label->transform.anchorMax = {1.0f, 1.0f};
        label->transform.offsetMax = {0.0f, 0.0f};
        label->padding = {0, 8, 0, 8};
        label->text = rows[i].text;
        label->fontSize = 16.0f;
        label->horizontal = rows[i].horizontal;
        label->vertical = Elm::VerticalAlign::Middle;
        label->color = Linear({0.92f, 0.92f, 0.94f, 1.0f});
        labels.push_back(labelHandle);
    }

    tree.Arrange(root, {{0, 0}, {400, 320}});

    std::uint64_t paintOrder = 10000;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        scene.Rect(tree.Get(cells[i])->border, {0.18f, 0.2f, 0.25f, 1.0f});

        Elm::Label* label = tree.GetAs<Elm::Label>(labels[i]);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        Elm::TextTarget target;
        target.textureSlice = kFontSlice;
        target.paintOrder = paintOrder;
        paintOrder += 1000;
        const auto items = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
    }

    Image image = scene.Render(400, 320, 1.0f, AtlasSampler(atlas));
    // Os rotulos sao texto real: ha muitos pixels claros sobre o fundo escuro da caixa.
    Expect(CountPixelsNot(image, {0.18f, 0.2f, 0.25f, 1.0f}, 24) > 800, "rotulos desenham texto real");
    return image;
}

// Cena 2: botoes nos estados normal, hover, pressionado com foco, focado e desabilitado, e um botao de largura
// fixa com rotulo maior que ele (o rotulo sai recortado).
Image ButtonScene(const Txt::TruetypeAtlas& atlas) {
    struct Spec {
        const char* text;
        float fixedWidth; // 0 = tamanho Auto.
        bool hovered;
        bool pressed;
        bool focused;
        bool disabled;
    };
    const Spec specs[] = {
        {"Normal", 0.0f, false, false, false, false},
        {"Hover", 0.0f, true, false, false, false},
        {"Pressionado", 0.0f, true, true, true, false},
        {"Focado", 0.0f, false, false, true, false},
        {"Desabilitado", 0.0f, false, false, false, true},
        {"Rotulo muito longo para largura fixa", 130.0f, false, false, false, false},
    };
    constexpr float kColumnX[] = {24.0f, 190.0f, 356.0f};
    constexpr float kRowY[] = {24.0f, 84.0f};
    constexpr float kHeight = 36.0f;

    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    Elm::ButtonStyle style;
    Elm::TextTarget labelTarget;
    labelTarget.textureSlice = kFontSlice;

    struct Placed {
        SU::UIHandle button;
        SU::UIHandle label;
    };
    std::vector<Placed> placed;
    for (std::size_t i = 0; i < std::size(specs); ++i) {
        const Spec& spec = specs[i];
        const SU::UIHandle buttonHandle = tree.Create<Elm::Button>(root);
        Elm::Button* button = tree.GetAs<Elm::Button>(buttonHandle);
        const float x = kColumnX[i % 3];
        const float y = kRowY[i / 3];
        const float width = spec.fixedWidth > 0.0f ? spec.fixedWidth : 150.0f;
        button->transform.anchorMax = {0.0f, 0.0f};
        button->transform.offsetMin = {x, y};
        button->transform.offsetMax = {x + width, y + kHeight};
        button->states.hovered = spec.hovered;
        button->states.pressed = spec.pressed;
        button->states.focused = spec.focused;
        button->states.disabled = spec.disabled;

        const SU::UIHandle labelHandle = tree.Create<Elm::Label>(buttonHandle);
        Elm::Label* label = tree.GetAs<Elm::Label>(labelHandle);
        label->transform.anchorMax = {1.0f, 1.0f};
        label->transform.offsetMax = {0.0f, 0.0f};
        label->padding = {0, 8, 0, 8};
        label->text = spec.text;
        label->horizontal = Elm::HorizontalAlign::Center;
        label->vertical = Elm::VerticalAlign::Middle;
        label->color = Linear(spec.disabled ? SU::Vec4{0.8f, 0.8f, 0.82f, 1.0f} : SU::Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        placed.push_back({buttonHandle, labelHandle});
    }

    tree.Arrange(root, {{0, 0}, {520, 150}});

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paintBase = 0;
    for (const Placed& item : placed) {
        const Elm::Button* button = tree.GetAs<Elm::Button>(item.button);
        const auto background = Elm::BuildButtonBackground(*button, style, 0.0f, paintBase);
        scene.Items().insert(scene.Items().end(), background.begin(), background.end());

        Elm::Label* label = tree.GetAs<Elm::Label>(item.label);
        const SU::Vec2 press = Elm::ButtonPressOffset(*button, style);
        const SU::Rect content{label->ContentRect().position + press, label->ContentRect().size};
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        labelTarget.paintOrder = paintBase + 50;
        const auto text = Elm::BuildLabelItems(*label, layout, content, labelTarget, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
        paintBase += 100;
    }

    Image image = scene.Render(520, 150, 1.0f, AtlasSampler(atlas));
    // Anel de foco (amarelo-quente) na borda superior do botao focado: vermelho acima do azul.
    // O botao "Focado" e o quarto item: coluna 0, linha 1. Pixel logo abaixo da borda superior.
    const auto ring = image.Srgb8(static_cast<int>(kColumnX[0] + 75.0f), static_cast<int>(kRowY[1]) + 1);
    Expect(ring[0] > ring[2] + 40, "botao focado mostra anel de foco");
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 2000, "botoes desenham fundo e texto");
    return image;
}

// Layout real de um texto simples (sem quebra), no tamanho dado.
// Layout do texto de um campo, com a configuracao do campo (quebra quando multilinha).
Txt::TextLayoutResult LayoutFieldText(const Elm::TextField& field, const Txt::TruetypeAtlas& atlas,
                                      const Elm::TextFieldStyle& style, SU::Vec2 contentSize) {
    const std::string& text = field.Edit().Text();
    return Txt::LayoutText(text, Txt::ShapeTruetype(text, atlas), atlas.metrics,
                           Elm::MakeTextFieldLayoutConfig(field, contentSize, style));
}

Txt::TextLayoutResult LayoutPlainText(const std::string& text, const Txt::TruetypeAtlas& atlas, float fontSize) {
    Txt::TextLayoutConfig config;
    config.fontSize = fontSize;
    return Txt::LayoutText(text, Txt::ShapeTruetype(text, atlas), atlas.metrics, config);
}

// Campo de texto de uma linha: um item por estado (vazio, com texto, selecao, rolado).
struct FieldSpec {
    const char* text;
    const char* placeholder;
    bool focused;
    bool selectAll;
    bool caretAtEnd;
};

// Desenha os campos empilhados. Cada campo: caixa de 300x30, padding 8 nas laterais, fonte de 16 px.
Image TextFieldScene(const Txt::TruetypeAtlas& atlas, const std::vector<FieldSpec>& specs) {
    constexpr float kLeft = 20.0f;
    constexpr float kWidth = 300.0f;
    constexpr float kHeight = 30.0f;
    constexpr float kGap = 14.0f;
    constexpr float kPadding = 8.0f;
    const Elm::TextFieldStyle style;

    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    std::vector<SU::UIHandle> fields;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const SU::UIHandle handle = tree.Create<Elm::TextField>(root);
        Elm::TextField* field = tree.GetAs<Elm::TextField>(handle);
        const float top = 20.0f + static_cast<float>(i) * (kHeight + kGap);
        field->transform.anchorMax = {0.0f, 0.0f};
        field->transform.offsetMin = {kLeft, top};
        field->transform.offsetMax = {kLeft + kWidth, top + kHeight};
        field->padding = {0, kPadding, 0, kPadding};
        field->placeholder = specs[i].placeholder;
        field->Edit().InsertText(specs[i].text);
        fields.push_back(handle);
    }
    tree.Arrange(root, {{0, 0}, {340, 200}});

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paintOrder = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        Elm::TextField* field = tree.GetAs<Elm::TextField>(fields[i]);
        const SU::Rect content = field->ContentRect();
        const auto textLayout = LayoutPlainText(field->Edit().Text(), atlas, 16.0f);
        const auto placeholderLayout = LayoutPlainText(field->placeholder, atlas, 16.0f);

        field->SetFocused(specs[i].focused);
        if (specs[i].selectAll) field->Edit().SelectAll();
        if (specs[i].caretAtEnd) field->Edit().MoveEnd();
        field->KeepCaretVisible(textLayout, content.size, style);

        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        Elm::TextTarget target;
        target.textureSlice = kFontSlice;
        target.paintOrder = paintOrder;
        paintOrder += 1000;
        const auto items = Elm::BuildTextFieldItems(*field, style, textLayout, placeholderLayout, content,
                                                    target, clip);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
    }

    Image image = scene.Render(340, 200, 1.0f, AtlasSampler(atlas));
    if (specs.size() == 1 && specs[0].selectAll) {
        // Selecao: pixels azulados na area do texto (fundo cinza sem selecao nao e azulado).
        std::size_t bluish = 0;
        for (int y = 22; y < 48; ++y) {
            for (int x = 28; x < 200; ++x) {
                const auto px = image.Srgb8(x, y);
                bluish += px[2] > px[0] + 12 ? 1u : 0u;
            }
        }
        Expect(bluish > 300, "selecao aparece atras do texto");
    }
    return image;
}

// Cena 6: caixas de selecao (normal, marcada, focada, desabilitada) e controles deslizantes (float, inteiro
// e de minimo igual ao maximo), com o valor escrito ao lado.
Image ControlsScene(const Txt::TruetypeAtlas& atlas) {
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    const Elm::CheckboxStyle boxStyle;
    struct BoxSpec {
        const char* text;
        bool checked;
        bool focused;
        bool disabled;
    };
    const BoxSpec boxes[] = {
        {"Aceitar os termos", false, false, false},
        {"Lembrar de mim", true, false, false},
        {"Notificacoes por e-mail", false, true, false},
        {"Indisponivel", true, false, true},
    };
    std::vector<SU::UIHandle> boxHandles;
    std::vector<SU::UIHandle> boxLabels;
    for (std::size_t i = 0; i < std::size(boxes); ++i) {
        const float top = 16.0f + static_cast<float>(i) * 34.0f;
        const SU::UIHandle boxHandle = tree.Create<Elm::Checkbox>(root);
        Elm::Checkbox* box = tree.GetAs<Elm::Checkbox>(boxHandle);
        box->transform.anchorMax = {0.0f, 0.0f};
        box->transform.offsetMin = {20.0f, top};
        box->transform.offsetMax = {240.0f, top + 30.0f};
        box->checked = boxes[i].checked;
        box->states.focused = boxes[i].focused;
        box->states.disabled = boxes[i].disabled;
        const SU::UIHandle labelHandle = tree.Create<Elm::Label>(boxHandle);
        Elm::Label* label = tree.GetAs<Elm::Label>(labelHandle);
        label->transform.anchorMax = {1.0f, 1.0f};
        label->transform.offsetMax = {0.0f, 0.0f};
        label->padding = Elm::CheckboxLabelPadding(boxStyle);
        label->text = boxes[i].text;
        label->color = Linear(boxes[i].disabled ? SU::Vec4{0.5f, 0.5f, 0.52f, 1.0f} : SU::Vec4{0.92f, 0.92f, 0.94f, 1.0f});
        boxHandles.push_back(boxHandle);
        boxLabels.push_back(labelHandle);
    }

    struct SliderSpec {
        float minimum;
        float maximum;
        float step;
        bool integer;
        float value;
    };
    const SliderSpec sliders[] = {
        {0.0f, 1.0f, 0.0f, false, 0.3f},
        {0.0f, 10.0f, 0.0f, true, 3.0f},
        {5.0f, 5.0f, 0.0f, false, 5.0f},
    };
    const Elm::SliderStyle sliderStyle;
    std::vector<SU::UIHandle> sliderHandles;
    std::vector<SU::UIHandle> valueLabels;
    for (std::size_t i = 0; i < std::size(sliders); ++i) {
        const float top = 164.0f + static_cast<float>(i) * 34.0f;
        const SU::UIHandle sliderHandle = tree.Create<Elm::Slider>(root);
        Elm::Slider* slider = tree.GetAs<Elm::Slider>(sliderHandle);
        slider->transform.anchorMax = {0.0f, 0.0f};
        slider->transform.offsetMin = {20.0f, top};
        slider->transform.offsetMax = {220.0f, top + 24.0f};
        slider->range = {sliders[i].minimum, sliders[i].maximum, sliders[i].step, sliders[i].integer};
        slider->SetValue(sliders[i].value);
        sliderHandles.push_back(sliderHandle);

        const SU::UIHandle valueHandle = tree.Create<Elm::Label>(root);
        Elm::Label* valueLabel = tree.GetAs<Elm::Label>(valueHandle);
        valueLabel->transform.anchorMax = {0.0f, 0.0f};
        valueLabel->transform.offsetMin = {232.0f, top};
        valueLabel->transform.offsetMax = {300.0f, top + 24.0f};
        valueLabel->horizontal = Elm::HorizontalAlign::Left;
        valueLabel->color = Linear({0.75f, 0.78f, 0.84f, 1.0f});
        valueLabels.push_back(valueHandle);
    }

    tree.Arrange(root, {{0, 0}, {360, 268}});

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;

    for (std::size_t i = 0; i < boxHandles.size(); ++i) {
        const Elm::Checkbox* box = tree.GetAs<Elm::Checkbox>(boxHandles[i]);
        const auto items = Elm::BuildCheckboxItems(*box, boxStyle, 0.0f, paint);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
        paint += 100;

        Elm::Label* label = tree.GetAs<Elm::Label>(boxLabels[i]);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    for (std::size_t i = 0; i < sliderHandles.size(); ++i) {
        const Elm::Slider* slider = tree.GetAs<Elm::Slider>(sliderHandles[i]);
        const auto items = Elm::BuildSliderItems(*slider, sliderStyle, 0.0f, paint);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
        paint += 100;

        Elm::Label* valueLabel = tree.GetAs<Elm::Label>(valueLabels[i]);
        valueLabel->text = Elm::SliderValueText(*slider);
        const SU::Rect content = valueLabel->ContentRect();
        const auto layout = LayoutLabelText(*valueLabel, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*valueLabel, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(360, 268, 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 2500, "caixas e controles desenham");
    return image;
}

// Cena 7: o conjunto inteiro (rotulo, botoes, campo, caixa e deslizante) em uma largura. Os elementos esticam
// com a janela; o texto que nao cabe e recortado, e o alinhamento se mantem. Devolve a largura do campo para o
// chamador conferir a geometria.
Image CompositionScene(const Txt::TruetypeAtlas& atlas, float width, float& fieldWidth) {
    constexpr float kHeight = 240.0f;
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    // Estica o elemento entre as bordas esquerda e direita (20 px de margem), na faixa vertical dada.
    auto place = [&](SU::UIHandle handle, float top, float height) {
        Elm::Element* element = tree.Get(handle);
        element->transform.anchorMax = {1.0f, 0.0f};
        element->transform.offsetMin = {20.0f, top};
        element->transform.offsetMax = {-20.0f, top + height};
    };
    auto fill = [](Elm::Element* element) {
        element->transform.anchorMax = {1.0f, 1.0f};
        element->transform.offsetMax = {0.0f, 0.0f};
    };

    const SU::UIHandle titleHandle = tree.Create<Elm::Label>(root);
    place(titleHandle, 14.0f, 22.0f);
    Elm::Label* title = tree.GetAs<Elm::Label>(titleHandle);
    title->text = "Conjunto de elementos";
    title->fontSize = 16.0f;
    title->color = Linear({0.92f, 0.92f, 0.94f, 1.0f});

    const Elm::ButtonStyle buttonStyle;
    const SU::UIHandle wideButtonHandle = tree.Create<Elm::Button>(root);
    place(wideButtonHandle, 46.0f, 36.0f);
    const SU::UIHandle wideLabel = tree.Create<Elm::Label>(wideButtonHandle);
    fill(tree.Get(wideLabel));
    tree.GetAs<Elm::Label>(wideLabel)->text = "Confirmar e continuar com a compra";
    tree.GetAs<Elm::Label>(wideLabel)->horizontal = Elm::HorizontalAlign::Center;
    tree.GetAs<Elm::Label>(wideLabel)->color = Linear({1.0f, 1.0f, 1.0f, 1.0f});

    const SU::UIHandle fieldHandle = tree.Create<Elm::TextField>(root);
    place(fieldHandle, 98.0f, 30.0f);
    Elm::TextField* field = tree.GetAs<Elm::TextField>(fieldHandle);
    field->padding = {0, 8, 0, 8};
    field->placeholder = "Digite seu nome";
    field->Edit().InsertText("Maria Aparecida dos Santos Oliveira");
    field->SetFocused(true);

    const Elm::CheckboxStyle boxStyle;
    const SU::UIHandle checkHandle = tree.Create<Elm::Checkbox>(root);
    place(checkHandle, 140.0f, 26.0f);
    tree.GetAs<Elm::Checkbox>(checkHandle)->checked = true;
    const SU::UIHandle checkLabel = tree.Create<Elm::Label>(checkHandle);
    fill(tree.Get(checkLabel));
    tree.GetAs<Elm::Label>(checkLabel)->padding = Elm::CheckboxLabelPadding(boxStyle);
    tree.GetAs<Elm::Label>(checkLabel)->text = "Aceito receber novidades por e-mail neste endereco";
    tree.GetAs<Elm::Label>(checkLabel)->color = Linear({0.92f, 0.92f, 0.94f, 1.0f});

    const Elm::SliderStyle sliderStyle;
    const SU::UIHandle sliderHandle = tree.Create<Elm::Slider>(root);
    place(sliderHandle, 190.0f, 24.0f);
    tree.GetAs<Elm::Slider>(sliderHandle)->range = {0.0f, 100.0f, 0.0f, true};
    tree.GetAs<Elm::Slider>(sliderHandle)->SetValue(64.0f);

    tree.Arrange(root, {{0, 0}, {width, kHeight}});
    fieldWidth = tree.Get(fieldHandle)->border.size.x;

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;

    auto addLabel = [&](SU::UIHandle handle) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto items = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
    };

    addLabel(titleHandle);

    const auto buttonItems = Elm::BuildButtonBackground(*tree.GetAs<Elm::Button>(wideButtonHandle), buttonStyle, 0.0f, paint);
    scene.Items().insert(scene.Items().end(), buttonItems.begin(), buttonItems.end());
    paint += 100;
    addLabel(wideLabel);

    const auto fieldPlaceholder = LayoutPlainText(field->placeholder, atlas, 16.0f);
    const auto fieldText = LayoutPlainText(field->Edit().Text(), atlas, 16.0f);
    field->KeepCaretVisible(fieldText, field->ContentRect().size, Elm::TextFieldStyle{});
    const std::uint32_t fieldClip = scene.AddClip({field->ContentRect().position.x, field->ContentRect().position.y,
                                                   field->ContentRect().position.x + field->ContentRect().size.x,
                                                   field->ContentRect().position.y + field->ContentRect().size.y});
    target.paintOrder = paint;
    paint += 100;
    const auto fieldItems = Elm::BuildTextFieldItems(*field, Elm::TextFieldStyle{}, fieldText, fieldPlaceholder,
                                                     field->ContentRect(), target, fieldClip);
    scene.Items().insert(scene.Items().end(), fieldItems.begin(), fieldItems.end());

    const auto checkItems = Elm::BuildCheckboxItems(*tree.GetAs<Elm::Checkbox>(checkHandle), boxStyle, 0.0f, paint);
    scene.Items().insert(scene.Items().end(), checkItems.begin(), checkItems.end());
    paint += 100;
    addLabel(checkLabel);

    const auto sliderItems = Elm::BuildSliderItems(*tree.GetAs<Elm::Slider>(sliderHandle), sliderStyle, 0.0f, paint);
    scene.Items().insert(scene.Items().end(), sliderItems.begin(), sliderItems.end());

    return scene.Render(static_cast<int>(width), static_cast<int>(kHeight), 1.0f, AtlasSampler(atlas));
}

// Campo multilinha com texto que passa da altura: caret no inicio (rolagem no topo) ou no fim (rolagem no fim).
Image MultilineScene(const Txt::TruetypeAtlas& atlas, bool caretAtEnd) {
    constexpr float kWidth = 340.0f;
    constexpr float kHeight = 140.0f;
    const char* const text = "Campo multilinha: o texto quebra por palavras, e uma palavra muito longa como "
                             "pneumoultramicroscopicossilicovulcanoconiose quebra entre as letras. Quando o texto "
                             "passa da altura da caixa, a barra de rolagem acompanha o caret.";
    const Elm::TextFieldStyle style;

    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle handle = tree.Create<Elm::TextField>(root, Elm::TextFieldOptions{.multiline = true});
    Elm::TextField* field = tree.GetAs<Elm::TextField>(handle);
    field->transform.anchorMax = {0.0f, 0.0f};
    field->transform.offsetMin = {20.0f, 20.0f};
    field->transform.offsetMax = {320.0f, 120.0f};
    field->padding = {4.0f, 8.0f, 4.0f, 8.0f};
    field->fontSize = 16.0f;
    field->Edit().InsertText(text);
    tree.Arrange(root, {{0, 0}, {kWidth, kHeight}});

    const SU::Rect content = field->ContentRect();
    const auto layout = LayoutFieldText(*field, atlas, style, content.size);
    field->SetFocused(true);
    if (caretAtEnd) {
        field->Edit().MoveEnd();
    } else {
        field->Edit().MoveHome();
    }
    field->KeepCaretVisible(layout, content.size, style);
    Expect(field->ScrollBarVisible(), "campo multilinha mostra a barra de rolagem");
    Expect((field->ScrollY() > 0.0f) == caretAtEnd, "rolagem vertical segue o caret");

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                              content.position.x + content.size.x,
                                              content.position.y + content.size.y});
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    const auto items = Elm::BuildTextFieldItems(*field, style, layout, LayoutPlainText("", atlas, 16.0f), content,
                                                target, clip);
    scene.Items().insert(scene.Items().end(), items.begin(), items.end());
    return scene.Render(static_cast<int>(kWidth), static_cast<int>(kHeight), 1.0f, AtlasSampler(atlas));
}


// Rotulo de texto com a caixa dada, filho da raiz. A caixa e absoluta: a raiz comeca em (0, 0).
SU::UIHandle AddTextLabel(Elm::ElementTree& tree, SU::UIHandle root, const char* text, SU::Rect box) {
    const SU::UIHandle handle = tree.Create<Elm::Label>(root);
    Elm::Label* label = tree.GetAs<Elm::Label>(handle);
    label->text = text;
    label->color = Linear({0.9f, 0.92f, 0.95f, 1.0f});
    label->border = box;
    return handle;
}

// Cena: grupo de radio (a opcao 2 escolhida, a 2 com foco) e quatro chaves Toggle: desligada, ligada, com foco
// e desabilitada. Os rotulos sao elementos de texto posicionados nas faixas que o grupo e a chave devolvem.
Image RadioToggleScene(const Txt::TruetypeAtlas& atlas) {
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    const SU::UIHandle groupHandle = tree.Create<Elm::RadioGroup>(root);
    Elm::RadioGroup* group = tree.GetAs<Elm::RadioGroup>(groupHandle);
    group->transform.anchorMax = {0.0f, 0.0f};
    group->transform.offsetMin = {16.0f, 16.0f};
    group->transform.offsetMax = {196.0f, 16.0f + 3.0f * 26.0f};
    const char* optionLabels[] = {"Pequeno", "Medio", "Grande"};
    for (std::size_t i = 0; i < std::size(optionLabels); ++i) group->AddOption(i + 1, optionLabels[i]);
    group->Select(2);
    group->focusedIndex = 1;

    struct ToggleSpec {
        const char* text;
        bool on;
        bool focused;
        bool disabled;
    };
    const ToggleSpec toggles[] = {
        {"Desligado", false, false, false},
        {"Ligado", true, false, false},
        {"Com foco", false, true, false},
        {"Indisponivel", true, false, true},
    };
    const Elm::ToggleStyle toggleStyle;
    std::vector<SU::UIHandle> toggleHandles;
    for (std::size_t i = 0; i < std::size(toggles); ++i) {
        const float top = 16.0f + 40.0f * static_cast<float>(i);
        const SU::UIHandle handle = tree.Create<Elm::Toggle>(root);
        Elm::Toggle* toggle = tree.GetAs<Elm::Toggle>(handle);
        toggle->transform.anchorMax = {0.0f, 0.0f};
        toggle->transform.offsetMin = {230.0f, top};
        toggle->transform.offsetMax = {270.0f, top + 30.0f};
        toggle->on = toggles[i].on;
        toggle->states.focused = toggles[i].focused;
        toggle->states.disabled = toggles[i].disabled;
        toggleHandles.push_back(handle);
    }

    tree.Arrange(root, {{0, 0}, {400, 200}});

    // Rotulos: calculados antes de criar qualquer elemento novo (a criacao pode mover a memoria da arvore).
    std::vector<SU::Rect> rowRects;
    std::vector<SU::Rect> toggleLabelRects;
    const Elm::RadioStyle radioStyle;
    group = tree.GetAs<Elm::RadioGroup>(groupHandle);
    for (std::size_t i = 0; i < group->options.size(); ++i) {
        rowRects.push_back(Elm::RadioLabelRect(*group, radioStyle, i));
    }
    for (std::size_t i = 0; i < std::size(toggles); ++i) {
        const float top = 16.0f + 40.0f * static_cast<float>(i);
        toggleLabelRects.push_back({{280.0f, top}, {116.0f, 30.0f}});
    }
    std::vector<SU::UIHandle> labelHandles;
    for (std::size_t i = 0; i < rowRects.size(); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, optionLabels[i], rowRects[i]));
    }
    for (std::size_t i = 0; i < std::size(toggles); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, toggles[i].text, toggleLabelRects[i]));
    }

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;

    group = tree.GetAs<Elm::RadioGroup>(groupHandle);
    const auto radioItems = Elm::BuildRadioGroupItems(*group, radioStyle, 0.0f, paint);
    scene.Items().insert(scene.Items().end(), radioItems.begin(), radioItems.end());
    paint += 100;
    for (const SU::UIHandle handle : toggleHandles) {
        const auto items = Elm::BuildToggleItems(*tree.GetAs<Elm::Toggle>(handle), toggleStyle, 0.0f, paint);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
        paint += 100;
    }
    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(400, 200, 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 1500, "radio e chave desenham");
    return image;
}

// Cena: lista de doze itens com um intervalo (Shift), um item extra (Ctrl), cursor com foco e rolagem ate o
// cursor. Os rotulos sao elementos de texto nas faixas que a lista devolve; o recorte corta o que rola para fora.
Image ListBoxScene(const Txt::TruetypeAtlas& atlas) {
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle listHandle = tree.Create<Elm::ListBox>(root);
    Elm::ListBox* list = tree.GetAs<Elm::ListBox>(listHandle);
    list->transform.anchorMax = {0.0f, 0.0f};
    list->transform.offsetMin = {10.0f, 10.0f};
    list->transform.offsetMax = {210.0f, 130.0f};
    tree.Arrange(root, {{0, 0}, {220, 140}});

    list = tree.GetAs<Elm::ListBox>(listHandle);
    list->UpdateViewport();
    std::vector<Elm::ListBoxItemId> ids;
    std::vector<std::string> names;
    for (Elm::ListBoxItemId id = 1; id <= 12; ++id) {
        ids.push_back(id);
        names.push_back("Item " + std::string(id < 10 ? "0" : "") + std::to_string(id));
    }
    list->SetItems(ids);
    list->states.focused = true;
    (void)list->Click(4, {});
    (void)list->Click(7, {false, true});
    (void)list->Click(10, {true, false});

    std::vector<SU::Rect> rowRects;
    for (std::size_t i = 0; i < names.size(); ++i) {
        SU::Rect row = list->RowRect(i);
        row.position.x += 6.0f;   // Folga entre a borda da lista e o texto.
        row.size.x -= 6.0f;
        rowRects.push_back(row);
    }
    const SU::Rect area = list->border;
    std::vector<SU::UIHandle> labelHandles;
    for (std::size_t i = 0; i < names.size(); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, names[i].c_str(), rowRects[i]));
    }

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    const std::uint32_t listClip = scene.AddClip({area.position.x, area.position.y, area.position.x + area.size.x,
                                                  area.position.y + area.size.y});
    list = tree.GetAs<Elm::ListBox>(listHandle);
    const auto listItems = Elm::BuildListBoxItems(*list, Elm::ListBoxStyle{}, listClip, 0.0f, paint);
    scene.Items().insert(scene.Items().end(), listItems.begin(), listItems.end());
    paint += 1000;

    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, listClip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(220, 140, 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 1500, "lista desenha linhas e rolagem");
    return image;
}

SU::Rect Inset(SU::Rect rect, float left) {
    rect.position.x += left;
    rect.size.x -= left;
    return rect;
}

SU::Rect Intersect(SU::Rect a, SU::Rect b) {
    const float left = std::max(a.position.x, b.position.x);
    const float top = std::max(a.position.y, b.position.y);
    const float right = std::min(a.position.x + a.size.x, b.position.x + b.size.x);
    const float bottom = std::min(a.position.y + a.size.y, b.position.y + b.size.y);
    return {{left, top}, {std::max(0.0f, right - left), std::max(0.0f, bottom - top)}};
}

// Cena: barra de abas com a aba ativa em destaque e o painel abaixo. A aba ativa chega por setas, como no
// uso real, e a faixa rola para mantê-la inteira. Os nomes das abas sao rotulos recortados pela faixa.
Image TabBarScene(const Txt::TruetypeAtlas& atlas, const std::vector<std::string>& labels, std::size_t active,
                  float width) {
    const float height = 200.0f;
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle tabsHandle = tree.Create<Elm::TabBar>(root);
    Elm::TabBar* tabs = tree.GetAs<Elm::TabBar>(tabsHandle);
    tabs->transform.anchorMax = {0.0f, 0.0f};
    tabs->transform.offsetMin = {20.0f, 20.0f};
    tabs->transform.offsetMax = {width - 20.0f, 54.0f};
    tree.Arrange(root, {{0, 0}, {width, height}});

    tabs = tree.GetAs<Elm::TabBar>(tabsHandle);
    tabs->control.labels = labels;
    tabs->control.activeIndex = 0;
    tabs->states.focused = true;
    tabs->panelHeight = 122.0f;
    for (std::size_t i = 0; i < active; ++i) (void)tabs->KeyPressed(Elm::TabKey::Right);

    const Elm::TabBarStyle style;
    const SU::Rect strip = tabs->border;
    const SU::Rect panel = tabs->PanelArea();
    const std::string title = "Conteudo de " + labels[tabs->control.activeIndex];
    std::vector<SU::UIHandle> labelHandles;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, labels[i].c_str(), Inset(Elm::TabRect(*tabs, i), 8.0f)));
    }
    const SU::UIHandle titleHandle = AddTextLabel(tree, root, title.c_str(),
                                                  {{panel.position.x + 12.0f, panel.position.y + 12.0f},
                                                   {panel.size.x - 24.0f, 24.0f}});
    labelHandles.push_back(titleHandle);

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    scene.Rect(panel, {0.12f, 0.13f, 0.16f, 1.0f});
    const std::uint32_t stripClip = scene.AddClip({strip.position.x, strip.position.y,
                                                   strip.position.x + strip.size.x, strip.position.y + strip.size.y});
    const auto stripItems = Elm::BuildTabBarItems(*tabs, style, stripClip, 0.1f, 0);
    scene.Items().insert(scene.Items().end(), stripItems.begin(), stripItems.end());

    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    std::uint64_t paint = 5000;
    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        // Abas recortam pela faixa; o titulo do painel recorta so pela sua caixa.
        const SU::Rect region = handle == titleHandle ? content : Intersect(content, strip);
        const std::uint32_t clip = scene.AddClip({region.position.x, region.position.y,
                                                  region.position.x + region.size.x,
                                                  region.position.y + region.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(static_cast<int>(width), static_cast<int>(height), 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 3000, "barra de abas desenha abas e painel");
    return image;
}

// Cena: caixa de combinacao com 'Verde' escolhida e o destaque em 'Azul'. O botao fica em buttonTop: com 20 a
// lista abre abaixo; com 150 nao cabe abaixo e abre acima. Os textos sao rotulos nas faixas que o elemento devolve.
Image DropdownScene(const Txt::TruetypeAtlas& atlas, float buttonTop) {
    const SU::Rect window{{0, 0}, {260, 200}};
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle comboHandle = tree.Create<Elm::Dropdown>(root);
    Elm::Dropdown* combo = tree.GetAs<Elm::Dropdown>(comboHandle);
    combo->transform.anchorMax = {0.0f, 0.0f};
    combo->transform.offsetMin = {20.0f, buttonTop};
    combo->transform.offsetMax = {220.0f, buttonTop + 30.0f};
    tree.Arrange(root, window);

    combo = tree.GetAs<Elm::Dropdown>(comboHandle);
    combo->control.options = {"Vermelho", "Verde", "Azul", "Amarelo", "Roxo"};
    combo->control.selectedIndex = 1;
    combo->states.focused = true;
    (void)combo->KeyPressed(Elm::DropdownKey::Down, window);  // Abre com o destaque em 'Verde'.
    (void)combo->KeyPressed(Elm::DropdownKey::Down, window);  // Destaque em 'Azul'.

    const Elm::DropdownStyle style;
    const SU::Rect popup = combo->Popup();
    std::vector<SU::UIHandle> labelHandles;
    labelHandles.push_back(AddTextLabel(tree, root, combo->control.options[1].c_str(),
                                        Inset(Elm::DropdownValueRect(*combo, style), 8.0f)));
    for (std::size_t i = 0; i < combo->control.options.size(); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, combo->control.options[i].c_str(),
                                            Inset(Elm::DropdownRowRect(*combo, i), 8.0f)));
    }

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    const std::uint32_t popupClip = scene.AddClip({popup.position.x, popup.position.y,
                                                   popup.position.x + popup.size.x, popup.position.y + popup.size.y});
    const auto buttonItems = Elm::BuildDropdownButtonItems(*combo, style, 0.1f, 0);
    scene.Items().insert(scene.Items().end(), buttonItems.begin(), buttonItems.end());
    const auto popupItems = Elm::BuildDropdownPopupItems(*combo, style, popupClip, 0.2f, 1000);
    scene.Items().insert(scene.Items().end(), popupItems.begin(), popupItems.end());

    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    std::uint64_t paint = 5000;
    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(260, 200, 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 2000, "caixa e lista desenham");
    return image;
}

// Cena: janela com barra de titulo e botoes, e um painel rolado dentro dela. Seis linhas de texto no conteudo
// de 300 px; a rolagem de 60 vale para todas, e o recorte do painel corta o que sai da area.
Image WindowPanelScene(const Txt::TruetypeAtlas& atlas) {
    const float width = 280.0f;
    const float height = 200.0f;
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle winHandle = tree.Create<Elm::Window>(root);
    Elm::Window* win = tree.GetAs<Elm::Window>(winHandle);
    win->transform.offsetMin = {16.0f, 16.0f};
    win->transform.offsetMax = {264.0f, 184.0f};
    win->title = "Propriedades";
    win->padding.top = win->titleBarHeight;
    win->padding.left = 8.0f;
    win->padding.right = 8.0f;
    win->padding.bottom = 8.0f;
    const SU::UIHandle panelHandle = tree.Create<Elm::Panel>(winHandle);
    tree.Get(panelHandle)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(panelHandle)->transform.offsetMax = {0.0f, 0.0f};
    std::vector<SU::UIHandle> rows;
    for (std::size_t i = 0; i < 6; ++i) {
        const SU::UIHandle row = tree.Create<Elm::Label>(panelHandle);
        Elm::Label* label = tree.GetAs<Elm::Label>(row);
        const float top = 8.0f + 36.0f * static_cast<float>(i);
        label->transform.anchorMax = {1.0f, 0.0f};
        label->transform.offsetMin = {8.0f, top};
        label->transform.offsetMax = {-8.0f, top + 28.0f};
        label->text = "Propriedade " + std::to_string(i + 1);
        label->fontSize = 16.0f;
        label->horizontal = Elm::HorizontalAlign::Left;
        label->vertical = Elm::VerticalAlign::Middle;
        label->color = Linear({0.92f, 0.92f, 0.94f, 1.0f});
        rows.push_back(row);
    }
    tree.Arrange(root, {{0, 0}, {width, height}});

    Elm::Panel* panel = tree.GetAs<Elm::Panel>(panelHandle);
    panel->contentSize = {200.0f, 300.0f};
    panel->ScrollBy({0.0f, 60.0f});
    panel->ArrangeChildren(tree);
    win = tree.GetAs<Elm::Window>(winHandle);
    panel = tree.GetAs<Elm::Panel>(panelHandle);

    const SU::Rect area = panel->ContentRect();
    const SU::Rect titleBox{{win->border.position.x + 10.0f, win->border.position.y},
                            {win->border.size.x - 80.0f, win->titleBarHeight}};
    const SU::UIHandle titleHandle = AddTextLabel(tree, winHandle, "Propriedades", titleBox);

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    const auto windowItems = Elm::BuildWindowItems(*win, Elm::WindowStyle{}, 0.0f, 0);
    scene.Items().insert(scene.Items().end(), windowItems.begin(), windowItems.end());
    const auto panelItems = Elm::BuildPanelItems(*panel, Elm::PanelStyle{}, 0.1f, 1000);
    scene.Items().insert(scene.Items().end(), panelItems.begin(), panelItems.end());

    const std::uint32_t areaClip = scene.AddClip({area.position.x, area.position.y, area.position.x + area.size.x,
                                                  area.position.y + area.size.y});
    std::vector<SU::UIHandle> labelHandles;
    std::uint64_t paint = 2000;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Elm::Element* row = tree.Get(rows[i]);
        if (!panel->ChildVisible(*row)) continue;
        const SU::Rect box = row->border;
        const SU::Vec4 band = i % 2 == 0 ? SU::Vec4{0.16f, 0.17f, 0.21f, 1.0f} : SU::Vec4{0.13f, 0.14f, 0.18f, 1.0f};
        scene.Items().push_back(R::MakeSolidRect(rows[i], box, Linear(band), areaClip, 0.2f, paint++));
        labelHandles.push_back(rows[i]);
    }

    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    labelHandles.push_back(titleHandle);
    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const SU::Rect region = handle == titleHandle ? content : Intersect(content, area);
        const std::uint32_t clip = scene.AddClip({region.position.x, region.position.y,
                                                  region.position.x + region.size.x,
                                                  region.position.y + region.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(static_cast<int>(width), static_cast<int>(height), 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 4000, "janela e painel desenham");
    return image;
}

Elm::PointerEvent PointerAt(Elm::PointerPhase phase, SU::Vec2 position, bool inside) {
    Elm::PointerEvent event;
    event.phase = phase;
    event.position = position;
    event.inside = inside;
    event.pointerId = 1;
    return event;
}

W::MenuItem MenuAction(const char* label, const char* shortcut = "") {
    W::MenuItem item;
    item.label = label;
    item.shortcut = shortcut;
    return item;
}

W::MenuItem MenuSeparator() {
    W::MenuItem item;
    item.kind = W::MenuItemKind::Separator;
    return item;
}

W::MenuItem MenuSubmenu(const char* label, std::vector<W::MenuItem> children) {
    W::MenuItem item;
    item.label = label;
    item.kind = W::MenuItemKind::Submenu;
    item.children = std::move(children);
    return item;
}

// Cena: barra de menus com Arquivo aberto e o submenu Exportar aberto ao lado, depois do atraso (0,25 s). Os
// rotulos ficam a esquerda de cada linha e os atalhos alinhados a direita, em coluna propria.
Image MenuBarScene(const Txt::TruetypeAtlas& atlas, float cornerRadius = 0.0f) {
    const float width = 480.0f;
    const float height = 220.0f;
    const SU::Rect canvas{{0.0f, 0.0f}, {width, height}};
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};
    const SU::UIHandle barHandle = tree.Create<Elm::MenuBar>(root);
    Elm::MenuBar* bar = tree.GetAs<Elm::MenuBar>(barHandle);
    bar->transform.anchorMax = {0.0f, 0.0f};
    bar->transform.offsetMax = {width, 26.0f};
    tree.Arrange(root, canvas);

    bar = tree.GetAs<Elm::MenuBar>(barHandle);
    std::vector<W::MenuBarEntry> menus{
        {"Arquivo",
         {MenuAction("Novo", "Ctrl+N"), MenuAction("Salvar", "Ctrl+S"), MenuSeparator(),
          MenuSubmenu("Exportar", {MenuAction("PNG"), MenuAction("JPG", "Ctrl+J")})},
         false},
        {"Editar", {MenuAction("Desfazer", "Ctrl+Z"), MenuAction("Refazer", "Ctrl+Y")}, false},
        {"Ajuda", {MenuAction("Sobre")}, false},
        {"Exibir", {MenuAction("Paineis", "Ctrl+P")}, false},
    };
    bar->control.SetMenus(menus);
    (void)bar->OnPointer(PointerAt(Elm::PointerPhase::Down, {40.0f, 13.0f}, true), canvas);
    (void)bar->OnPointer(PointerAt(Elm::PointerPhase::Up, {40.0f, 13.0f}, true), canvas);
    (void)bar->KeyPressed(W::MenuKey::Down, canvas);
    (void)bar->KeyPressed(W::MenuKey::Down, canvas);   // Destaque em Exportar (submenu).
    (void)bar->Tick(0.25f, canvas);                          // Passou do atraso: o submenu abre.

    std::vector<SU::UIHandle> labelHandles;
    for (std::size_t i = 0; i < menus.size(); ++i) {
        labelHandles.push_back(AddTextLabel(tree, root, menus[i].label.c_str(), Inset(bar->HeaderRect(i), 10.0f)));
    }
    for (std::size_t d = 0; d < bar->control.Dropdown().Depth(); ++d) {
        const auto& items = bar->control.Dropdown().Items(d);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].kind == W::MenuItemKind::Separator) continue;
            const SU::Rect row = bar->RowRect(d, i);
            labelHandles.push_back(AddTextLabel(tree, root, items[i].label.c_str(), Inset(row, 10.0f)));
            if (!items[i].shortcut.empty()) {
                const SU::UIHandle shortcut = AddTextLabel(tree, root, items[i].shortcut.c_str(),
                                                           {{row.position.x + row.size.x - 80.0f, row.position.y},
                                                            {70.0f, row.size.y}});
                tree.GetAs<Elm::Label>(shortcut)->horizontal = Elm::HorizontalAlign::Right;
                labelHandles.push_back(shortcut);
            }
        }
    }

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    bar = tree.GetAs<Elm::MenuBar>(barHandle);
    const SU::Rect strip = bar->border;
    const std::uint32_t stripClip = scene.AddClip({strip.position.x, strip.position.y,
                                                   strip.position.x + strip.size.x, strip.position.y + strip.size.y});
    Elm::MenuBarStyle barStyle;
    barStyle.cornerRadius = cornerRadius;
    const auto barItems = Elm::BuildMenuBarItems(*bar, barStyle, stripClip, 0.0f, 0);
    scene.Items().insert(scene.Items().end(), barItems.begin(), barItems.end());

    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    std::uint64_t paint = 5000;
    for (const SU::UIHandle handle : labelHandles) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(static_cast<int>(width), static_cast<int>(height), 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 5000, "barra de menus desenha menus");
    return image;
}

// Textura do logo (fotos/LOGO.png, 64 x 64): slice 2 do harness. Outros slices sao branco.
constexpr std::uint32_t kLogoSlice = 2;

TextureFunction LogoSampler() {
    return [](std::uint32_t slice, SU::Vec2 uv) -> SU::Vec4 {
        if (slice != kLogoSlice) return {1.0f, 1.0f, 1.0f, 1.0f};
        const int x = std::clamp(static_cast<int>(uv.x * static_cast<float>(kLogoWidth)), 0, kLogoWidth - 1);
        const int y = std::clamp(static_cast<int>(uv.y * static_cast<float>(kLogoHeight)), 0, kLogoHeight - 1);
        const std::size_t i = (static_cast<std::size_t>(y) * kLogoWidth + static_cast<std::size_t>(x)) * 4u;
        return Linear({kLogoRgba[i] / 255.0f, kLogoRgba[i + 1] / 255.0f, kLogoRgba[i + 2] / 255.0f,
                       kLogoRgba[i + 3] / 255.0f});
    };
}

// Cena: o logo real nos tres modos de ajuste (esticar distorce; conter centraliza; preencher corta), uma
// textura ausente com o marcador de erro, e um botao de imagem pressionado (cor escurecida).
Image ImageScene() {
    const float width = 320.0f;
    const float height = 200.0f;
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    struct Box {
        float x, y, w, h;
    };
    const Box boxes[] = {{20, 20, 130, 70}, {170, 20, 130, 70}, {20, 100, 130, 70}, {170, 100, 60, 70}, {240, 100, 60, 70}};
    std::vector<SU::UIHandle> handles;
    for (std::size_t i = 0; i < std::size(boxes); ++i) {
        const bool isButton = i == 4;
        const SU::UIHandle handle = isButton ? tree.Create<Elm::ImageButton>(root) : tree.Create<Elm::Image>(root);
        tree.Get(handle)->transform.anchorMax = {0.0f, 0.0f};
        tree.Get(handle)->transform.offsetMin = {boxes[i].x, boxes[i].y};
        tree.Get(handle)->transform.offsetMax = {boxes[i].x + boxes[i].w, boxes[i].y + boxes[i].h};
        handles.push_back(handle);
    }
    tree.Arrange(root, {{0, 0}, {width, height}});

    Elm::ImageSource logo;
    logo.textureSlice = kLogoSlice;
    logo.textureSize = {static_cast<float>(kLogoWidth), static_cast<float>(kLogoHeight)};
    const Elm::ImageStyle style;
    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        std::vector<R::UIRenderItem> items;
        if (i < 3) {
            Elm::Image* image = tree.GetAs<Elm::Image>(handles[i]);
            image->source = logo;
            image->source.fit = i == 0 ? Elm::ImageFit::Stretch : (i == 1 ? Elm::ImageFit::Contain : Elm::ImageFit::Fill);
            items = Elm::BuildImageItems(*image, style, R::NoClipIndex, 0.0f, paint);
        } else if (i == 3) {
            Elm::Image* missing = tree.GetAs<Elm::Image>(handles[i]);
            missing->source = logo;
            missing->source.loaded = false;
            items = Elm::BuildImageItems(*missing, style, R::NoClipIndex, 0.0f, paint);
        } else {
            Elm::ImageButton* button = tree.GetAs<Elm::ImageButton>(handles[i]);
            button->source = logo;
            button->source.fit = Elm::ImageFit::Contain;
            (void)button->OnPointer(PointerAt(Elm::PointerPhase::Down, {270.0f, 130.0f}, true));  // Pressionado.
            items = Elm::BuildImageButtonItems(*button, style, R::NoClipIndex, 0.0f, paint);
        }
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
        paint += 100;
    }

    Image image = scene.Render(static_cast<int>(width), static_cast<int>(height), 1.0f, LogoSampler());
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 8000, "imagens desenham o logo e o marcador");
    return image;
}

// Grafico de linha em cima e histograma embaixo, com a mesma serie de 120 amostras. Dois NaN no meio nao aparecem.
Image GraphScene(const Txt::TruetypeAtlas& atlas) {
    const float width = 360.0f;
    const float height = 260.0f;
    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    const SU::UIHandle ids[] = {tree.Create<Elm::Graph>(root), tree.Create<Elm::Graph>(root)};
    const float tops[] = {20.0f, 140.0f};
    for (std::size_t i = 0; i < 2; ++i) {
        tree.Get(ids[i])->transform.anchorMax = {0.0f, 0.0f};
        tree.Get(ids[i])->transform.offsetMin = {20.0f, tops[i]};
        tree.Get(ids[i])->transform.offsetMax = {340.0f, tops[i] + 110.0f};
    }
    tree.Arrange(root, {{0, 0}, {width, height}});

    Elm::RingHistory samples(120);
    for (int i = 0; i < 120; ++i) {
        const bool gap = i == 50 || i == 51;
        samples.Push(gap ? std::numeric_limits<float>::quiet_NaN() : 0.5f + 0.4f * std::sin(0.15f * static_cast<float>(i)));
    }
    const Elm::GraphKind kinds[] = {Elm::GraphKind::Line, Elm::GraphKind::Histogram};
    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    std::uint64_t paint = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        Elm::Graph* graph = tree.GetAs<Elm::Graph>(ids[i]);
        graph->kind = kinds[i];
        graph->history = samples;
        const auto items = Elm::BuildGraphItems(*graph, Elm::GraphStyle{}, R::NoClipIndex, 0.0f, paint);
        scene.Items().insert(scene.Items().end(), items.begin(), items.end());
        paint += 1000;
    }

    Image image = scene.Render(static_cast<int>(width), static_cast<int>(height), 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 4000, "graficos desenham linha e barras");
    return image;
}

// Cena: seletor de cor com a cor atual diferente da original (amostra dupla), campos HEX e RGBA com texto real.
Image ColorPickerScene(const Txt::TruetypeAtlas& atlas) {
    constexpr float kWidth = 280.0f;
    constexpr float kHeight = 360.0f;
    const W::ColorPickerStyle style;
    const W::ColorPickerLayout layout = W::LayoutColorPicker(SU::Rect{{20.0f, 20.0f}, {240.0f, 320.0f}}, style);

    W::ColorPickerControl picker(SU::Vec4{0.2f, 0.5f, 0.9f, 0.7f});
    static_cast<void>(picker.SetColor(SU::Vec4{0.95f, 0.45f, 0.1f, 0.4f}));  // Resultado nao usado: o teste so mede o efeito no estado.

    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    tree.Get(root)->transform.anchorMax = {1.0f, 1.0f};
    tree.Get(root)->transform.offsetMax = {0.0f, 0.0f};

    // Um rotulo por campo, posicionado no retangulo que o layout devolve.
    struct Caption {
        SU::Rect box;
        std::string text;
    };
    std::vector<Caption> captions{{layout.hexField, picker.HexText()}};
    const char* channelNames = "RGBA";
    for (std::size_t i = 0; i < layout.channelFields.size(); ++i) {
        const auto channel = static_cast<W::ColorChannel>(i);
        captions.push_back({layout.channelFields[i], std::string(1, channelNames[i]) + " " + picker.ChannelText(channel)});
    }
    std::vector<SU::UIHandle> labels;
    for (const Caption& caption : captions) {
        const SU::UIHandle handle = tree.Create<Elm::Label>(root);
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        label->transform.anchorMax = {0.0f, 0.0f};
        label->transform.offsetMin = caption.box.position;
        label->transform.offsetMax = caption.box.Max();
        label->padding = {0, 8, 0, 8};
        label->text = caption.text;
        label->fontSize = 14.0f;
        label->horizontal = Elm::HorizontalAlign::Left;
        label->vertical = Elm::VerticalAlign::Middle;
        label->color = Linear({0.92f, 0.92f, 0.94f, 1.0f});
        labels.push_back(handle);
    }
    tree.Arrange(root, {{0, 0}, {kWidth, kHeight}});

    Scene scene(SU::Vec4{0.075f, 0.085f, 0.105f, 1.0f});
    const auto items = W::BuildColorPickerItems(picker, layout, root, style, 0.0f, 0);
    scene.Items().insert(scene.Items().end(), items.begin(), items.end());

    std::uint64_t paintOrder = 10000;
    for (const SU::UIHandle handle : labels) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto textLayout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        Elm::TextTarget target;
        target.textureSlice = kFontSlice;
        target.paintOrder = paintOrder;
        paintOrder += 1000;
        const auto text = Elm::BuildLabelItems(*label, textLayout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(static_cast<int>(kWidth), static_cast<int>(kHeight), 1.0f, AtlasSampler(atlas));
    Expect(CountPixelsNot(image, {0.075f, 0.085f, 0.105f, 1.0f}, 24) > 20000, "seletor de cor desenha as areas");
    return image;
}

// Cena: janelas e botoes com raio 0, 6 e 12 lado a lado. Cada coluna tem uma janela com titulo e, abaixo, um
// botao com o mesmo raio. O raio chega pelo estilo da janela e pelo campo do botao, e passa pela primitiva.
Image RadiusWindowsScene(const Txt::TruetypeAtlas& atlas) {
    constexpr float kRadii[] = {0.0f, 6.0f, 12.0f};
    constexpr float kWidth = 520.0f;
    constexpr float kHeight = 200.0f;
    constexpr float kColumnStride = 168.0f;
    const SU::Vec4 background{0.075f, 0.085f, 0.105f, 1.0f};

    Elm::ElementTree tree;
    const SU::UIHandle root = tree.Create<Elm::Element>(SU::InvalidUIHandle);
    std::vector<SU::UIHandle> windows;
    std::vector<SU::UIHandle> buttons;
    std::vector<SU::UIHandle> labels;
    for (std::size_t i = 0; i < std::size(kRadii); ++i) {
        const float x = 24.0f + kColumnStride * static_cast<float>(i);
        const SU::UIHandle winHandle = tree.Create<Elm::Window>(root);
        Elm::Window* win = tree.GetAs<Elm::Window>(winHandle);
        win->border = {{x, 24.0f}, {144.0f, 120.0f}};
        windows.push_back(winHandle);
        labels.push_back(AddTextLabel(tree, root, "Janela", {{x + 10.0f, 24.0f}, {124.0f, 30.0f}}));

        const SU::UIHandle buttonHandle = tree.Create<Elm::Button>(root);
        Elm::Button* button = tree.GetAs<Elm::Button>(buttonHandle);
        button->border = {{x + 12.0f, 160.0f}, {120.0f, 32.0f}};
        button->cornerRadius = kRadii[i];
        buttons.push_back(buttonHandle);

        const SU::UIHandle labelHandle = AddTextLabel(tree, root, "Iniciar", button->border);
        Elm::Label* label = tree.GetAs<Elm::Label>(labelHandle);
        label->horizontal = Elm::HorizontalAlign::Center;
        label->vertical = Elm::VerticalAlign::Middle;
        labels.push_back(labelHandle);
    }

    Scene scene(background);
    Elm::TextTarget target;
    target.textureSlice = kFontSlice;
    for (std::size_t i = 0; i < std::size(kRadii); ++i) {
        Elm::WindowStyle windowStyle;
        windowStyle.cornerRadius = kRadii[i];
        const auto windowItems = Elm::BuildWindowItems(*tree.GetAs<Elm::Window>(windows[i]), windowStyle, 0.0f,
                                                       100 * i);
        scene.Items().insert(scene.Items().end(), windowItems.begin(), windowItems.end());
        const auto buttonItems = Elm::BuildButtonBackground(*tree.GetAs<Elm::Button>(buttons[i]), Elm::ButtonStyle{},
                                                            0.0f, 1000 + 100 * i);
        scene.Items().insert(scene.Items().end(), buttonItems.begin(), buttonItems.end());
    }
    std::uint64_t paint = 3000;
    for (const SU::UIHandle handle : labels) {
        Elm::Label* label = tree.GetAs<Elm::Label>(handle);
        const SU::Rect content = label->ContentRect();
        const auto layout = LayoutLabelText(*label, atlas);
        const std::uint32_t clip = scene.AddClip({content.position.x, content.position.y,
                                                  content.position.x + content.size.x,
                                                  content.position.y + content.size.y});
        target.paintOrder = paint;
        paint += 100;
        const auto text = Elm::BuildLabelItems(*label, layout, content, target, clip);
        scene.Items().insert(scene.Items().end(), text.begin(), text.end());
    }

    Image image = scene.Render(static_cast<int>(kWidth), static_cast<int>(kHeight), 1.0f, AtlasSampler(atlas));
    // O canto de cima-esquerda cai fora do raio: com raio 12 mostra o fundo da cena, com raio 0 mostra a moldura.
    const auto squareWindow = image.Srgb8(24, 24);
    const auto roundWindow = image.Srgb8(static_cast<int>(24.0f + 2.0f * kColumnStride), 24);
    Expect(std::abs(squareWindow[0] - roundWindow[0]) + std::abs(squareWindow[2] - roundWindow[2]) > 40,
           "janela com raio 12 tem canto recortado");
    const auto squareButton = image.Srgb8(36, 160);
    const auto roundButton = image.Srgb8(static_cast<int>(12.0f + 24.0f + 2.0f * kColumnStride), 160);
    Expect(squareButton[2] > roundButton[2] + 40, "botao com raio 12 tem canto recortado");
    return image;
}

bool WritePng(const Image& image, const std::filesystem::path& path) {
    const auto bytes = EncodePng(image);
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path outDir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("element-captures");
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    const Txt::TruetypeAtlas atlas = LoadProjectFontAtlas();

    float wideField = 0.0f;
    float narrowField = 0.0f;
    const Image wide = CompositionScene(atlas, 360.0f, wideField);
    const Image narrow = CompositionScene(atlas, 240.0f, narrowField);
    // A largura do campo segue a janela (360 - 40 e 240 - 40); o texto nao empurra a caixa.
    Expect(wideField == 320.0f && narrowField == 200.0f, "campo acompanha a largura da janela");

    const std::vector<std::pair<const char*, Image>> scenes{
        {"elements-1-rotulos.png", LabelScene(atlas)},
        {"elements-2-botoes.png", ButtonScene(atlas)},
        {"elements-3-campo-vazio-e-texto.png",
         TextFieldScene(atlas, {{"", "Digite seu nome", false, false, false},
                                {"Maria Silva", "", true, false, true}})},
        {"elements-4-campo-selecao.png", TextFieldScene(atlas, {{"Maria Silva", "", true, true, false}})},
        {"elements-5-campo-rolado.png",
         TextFieldScene(atlas, {{"Texto muito longo que nao cabe no campo de uma linha", "", true, false, true}})},
        {"elements-6-caixa-e-deslizante.png", ControlsScene(atlas)},
        {"elements-7-conjunto-largura-360.png", wide},
        {"elements-8-conjunto-largura-240.png", narrow},
        {"elem-01-radio-toggle.png", RadioToggleScene(atlas)},
        {"elem-02-lista.png", ListBoxScene(atlas)},
        {"elem-03-dropdown.png", DropdownScene(atlas, 20.0f)},
        {"elem-03b-dropdown-acima.png", DropdownScene(atlas, 150.0f)},
        {"elem-06-barra-de-menus.png", MenuBarScene(atlas)},
        {"raio-01.png", RadiusWindowsScene(atlas)},
        {"raio-02.png", MenuBarScene(atlas, 8.0f)},
        {"elem-07-imagem.png", ImageScene()},
        {"elem-08-grafico.png", GraphScene(atlas)},
        {"janela-07a-seletor-cor.png", ColorPickerScene(atlas)},
        {"elem-04-abas.png", TabBarScene(atlas, {"Geral", "Video", "Audio", "Controles", "Sobre"}, 1, 280.0f)},
        {"elem-05-janela-painel.png", WindowPanelScene(atlas)},
        {"elem-04b-abas-rolagem.png",
         TabBarScene(atlas,
                     {"Aba 1", "Aba 2", "Aba 3", "Aba 4", "Aba 5", "Aba 6", "Aba 7", "Aba 8", "Aba 9", "Aba 10",
                      "Aba 11", "Aba 12"},
                     9, 260.0f)},
        {"texto-01-linha-unica-caret-fim.png",
         TextFieldScene(atlas, {{"Texto muito longo que nao cabe no campo de uma linha", "", true, false, true}})},
        {"texto-02-linha-unica-sem-foco.png",
         TextFieldScene(atlas, {{"Texto muito longo que nao cabe no campo de uma linha", "", false, false, false}})},
        {"texto-03-multilinha-rolagem-topo.png", MultilineScene(atlas, false)},
        {"texto-04-multilinha-rolagem-fim.png", MultilineScene(atlas, true)},
    };
    for (const auto& [name, image] : scenes) {
        const std::filesystem::path path = outDir / name;
        if (!WritePng(image, path)) {
            ++failures;
            std::fprintf(stderr, "[FAIL] nao gravou %s\n", path.string().c_str());
            continue;
        }
        std::printf("%s\n", path.string().c_str());
    }
    std::printf("element captures: %zu cenas, %d falhas\n", scenes.size(), failures);
    return failures == 0 ? 0 : 1;
}
