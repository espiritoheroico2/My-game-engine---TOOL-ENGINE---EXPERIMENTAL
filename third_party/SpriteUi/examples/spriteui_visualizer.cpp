#include "SpriteUi/SpriteUi.hpp"
#include "BitmapFont.hpp"
#include "BitmapImage.hpp"
#include "Visual/VisualHarness.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace SU = ToolEngine::SpriteUi;
namespace Visual = SpriteUiTests::Visual;
namespace Input = ToolEngine::SpriteUi::Input;
namespace Rendering = ToolEngine::SpriteUi::Rendering;
namespace Animation = ToolEngine::SpriteUi::Animation;

struct Palette {
    SU::Vec4 background;
    SU::Vec4 panel;
    SU::Vec4 surface;
    SU::Vec4 accent;
    SU::Vec4 text;
    SU::Vec4 muted;
};

constexpr std::array<Palette, 3> Palettes{{
    {{0.035f, 0.045f, 0.065f, 1.0f}, {0.075f, 0.090f, 0.125f, 1.0f},
     {0.105f, 0.125f, 0.165f, 1.0f}, {0.30f, 0.67f, 1.0f, 1.0f},
     {0.91f, 0.94f, 0.98f, 1.0f}, {0.48f, 0.55f, 0.65f, 1.0f}},
    {{0.055f, 0.045f, 0.070f, 1.0f}, {0.115f, 0.085f, 0.145f, 1.0f},
     {0.160f, 0.120f, 0.205f, 1.0f}, {0.78f, 0.43f, 0.96f, 1.0f},
     {0.96f, 0.92f, 0.99f, 1.0f}, {0.62f, 0.53f, 0.70f, 1.0f}},
    {{0.035f, 0.060f, 0.055f, 1.0f}, {0.070f, 0.120f, 0.100f, 1.0f},
     {0.095f, 0.165f, 0.135f, 1.0f}, {0.30f, 0.82f, 0.57f, 1.0f},
     {0.91f, 0.97f, 0.93f, 1.0f}, {0.49f, 0.66f, 0.57f, 1.0f}}
}};

constexpr SU::UIHandle PaletteButton{1, 1};
constexpr SU::UIHandle ToggleButton{2, 1};
constexpr SU::UIHandle SliderTarget{3, 1};
constexpr SU::UIHandle OverviewTab{4, 1};
constexpr SU::UIHandle StyleTab{5, 1};
constexpr SU::UIHandle LayoutTab{6, 1};
constexpr SU::UIHandle FadeButton{7, 1};
constexpr SU::UIHandle FloatingActionButton{8, 1};
constexpr SU::UIHandle TextFieldTarget{9, 1};
constexpr std::uint32_t GradientTextureSlice = 2;
constexpr std::uint32_t LogoTextureSlice = 3;
constexpr std::size_t TextFieldMaxBytes = 128;

enum class TextStyle : std::uint8_t {
    Title,
    Subtitle,
    Body,
    Label,
    Caption,
    Metric,
    Footer
};

struct TextStyleSpec {
    float fontSize;
    float marginAfter;
};

struct TextFieldState {
    std::string value = "god only knows";
    SU::Widgets::UITextInputComponent editor;

    TextFieldState() {
        editor.cursorCluster = static_cast<std::uint32_t>(value.size());
    }
};

SU::Rect TextFieldRect() {
    return {{24.0f, 104.0f}, {157.0f, 34.0f}};
}

bool IsUtf8Continuation(char value) {
    return (static_cast<unsigned char>(value) & 0xC0u) == 0x80u;
}

std::size_t PreviousCodePointStart(std::string_view value, std::size_t offset) {
    if (offset == 0 || offset > value.size()) return offset;
    std::size_t start = offset - 1;
    while (start > 0 && IsUtf8Continuation(value[start])) --start;
    return start;
}

std::size_t NextCodePointEnd(std::string_view value, std::size_t offset) {
    if (offset >= value.size()) return value.size();
    std::size_t end = offset + 1;
    while (end < value.size() && IsUtf8Continuation(value[end])) ++end;
    return end;
}

std::size_t Utf8PrefixLength(std::string_view value, std::size_t maxBytes) {
    std::size_t length = std::min(value.size(), maxBytes);
    if (length < value.size()) {
        while (length > 0 && IsUtf8Continuation(value[length])) --length;
    }
    return length;
}

void InsertCommittedText(TextFieldState& field, std::string_view text) {
    if (field.value.size() >= TextFieldMaxBytes) return;
    const std::size_t cursor = std::min<std::size_t>(field.editor.cursorCluster, field.value.size());
    const std::size_t available = TextFieldMaxBytes - field.value.size();
    const std::size_t insertLength = Utf8PrefixLength(text, available);
    if (insertLength == 0) return;
    field.value.insert(cursor, text.substr(0, insertLength));
    field.editor.MoveCursor(static_cast<std::uint32_t>(cursor + insertLength));
}

void DeletePreviousCodePoint(TextFieldState& field) {
    const std::size_t cursor = std::min<std::size_t>(field.editor.cursorCluster, field.value.size());
    if (cursor == 0) return;
    const std::size_t previous = PreviousCodePointStart(field.value, cursor);
    field.value.erase(previous, cursor - previous);
    field.editor.MoveCursor(static_cast<std::uint32_t>(previous));
}

void DeleteNextCodePoint(TextFieldState& field) {
    const std::size_t cursor = std::min<std::size_t>(field.editor.cursorCluster, field.value.size());
    if (cursor >= field.value.size()) return;
    field.value.erase(cursor, NextCodePointEnd(field.value, cursor) - cursor);
    field.editor.MoveCursor(static_cast<std::uint32_t>(cursor));
}

std::string ReadTextFieldValue(const TextFieldState& field) {
    return field.value;
}

TextStyleSpec GetTextStyle(TextStyle style) {
    switch (style) {
    case TextStyle::Title: return {22.0f, 8.0f};
    case TextStyle::Subtitle: return {16.0f, 8.0f};
    case TextStyle::Body: return {14.0f, 6.0f};
    case TextStyle::Label: return {12.0f, 5.0f};
    case TextStyle::Caption: return {10.0f, 5.0f};
    case TextStyle::Metric: return {24.0f, 5.0f};
    case TextStyle::Footer: return {9.0f, 0.0f};
    }
    return {14.0f, 6.0f};
}

SU::Text::TextLayoutResult LayoutTextField(
    std::string_view value, const SpriteUIVisualizer::BitmapFont& font) {
    SU::Text::TextLayoutConfig config;
    config.fontSize = GetTextStyle(TextStyle::Body).fontSize;
    const auto glyphs = font.Shape(value);
    return SU::Text::LayoutText(value, glyphs, font.Metrics(), config);
}

float TextFieldScrollX(const SU::Text::TextLayoutResult& layout, float visibleWidth,
                       std::uint32_t cursorByteOffset) {
    const SU::Rect caret = SU::Text::GetCaretRect(layout, cursorByteOffset);
    return std::max(0.0f, caret.position.x - (visibleWidth - 3.0f));
}

bool UpdateNativeTextInputArea(
    SDL_Window* window,
    const TextFieldState& field,
    const SpriteUIVisualizer::BitmapFont& font) {
    const SU::Rect rect = TextFieldRect();
    const SU::Rect clip{{rect.position.x + 7.0f, rect.position.y + 4.0f},
                        {rect.size.x - 14.0f, rect.size.y - 8.0f}};
    const auto layout = LayoutTextField(field.value, font);
    const SU::Rect caret = SU::Text::GetCaretRect(
        layout, std::min<std::uint32_t>(
            field.editor.cursorCluster, static_cast<std::uint32_t>(field.value.size())));
    const float scrollX = TextFieldScrollX(layout, clip.size.x, field.editor.cursorCluster);
    SDL_Rect area{
        static_cast<int>(std::lround(rect.position.x)),
        static_cast<int>(std::lround(rect.position.y)),
        static_cast<int>(std::lround(rect.size.x)),
        static_cast<int>(std::lround(rect.size.y))
    };
    const int cursorX = static_cast<int>(std::lround(
        7.0f + caret.position.x - scrollX));
    return SDL_SetTextInputArea(window, &area, cursorX);
}

float TextLine(Visual::Scene& scene, const SpriteUIVisualizer::BitmapFont& font,
               float x, float y, std::string_view text, SU::Vec4 color, TextStyle style) {
    const TextStyleSpec spec = GetTextStyle(style);
    SU::Text::TextLayoutConfig config;
    config.fontSize = spec.fontSize;
    const auto glyphs = font.Shape(text);
    const auto layout = SU::Text::LayoutText(text, glyphs, font.Metrics(), config);
    scene.Text(layout, {x, y}, color, SpriteUIVisualizer::BitmapFont::TextureSlice,
               Rendering::FlagBitmapText);
    return y + layout.size.y + spec.marginAfter;
}

float MeasureTextHeight(const SpriteUIVisualizer::BitmapFont& font,
                        std::string_view text, TextStyle style) {
    SU::Text::TextLayoutConfig config;
    config.fontSize = GetTextStyle(style).fontSize;
    const auto glyphs = font.Shape(text);
    return SU::Text::LayoutText(text, glyphs, font.Metrics(), config).size.y;
}

float TabsTop(const SpriteUIVisualizer::BitmapFont& font) {
    float y = 76.0f + MeasureTextHeight(font, "INTERFACE QUALITY SANDBOX", TextStyle::Title) +
              GetTextStyle(TextStyle::Title).marginAfter;
    y += MeasureTextHeight(font, "LAYOUT COLOR CLIPPING POINTER STATES", TextStyle::Subtitle) +
         GetTextStyle(TextStyle::Subtitle).marginAfter + 20.0f;
    return y;
}

float CardsTop(const SpriteUIVisualizer::BitmapFont& font) {
    return TabsTop(font) + 44.0f;
}

float MainPanelTop(const SpriteUIVisualizer::BitmapFont& font) {
    return CardsTop(font) + 126.0f;
}

SU::Rect ButtonRect(float width) {
    const float right = width - 28.0f;
    return {{right - 128.0f, 18.0f}, {128.0f, 38.0f}};
}

SU::Rect ControlPanelBounds(float width, float panelTop) {
    constexpr float contentX = 229.0f;
    const float contentWidth = std::max(320.0f, width - contentX - 24.0f);
    const float graphWidth = contentWidth * 0.48f;
    const float controlsX = contentX + graphWidth + 52.0f;
    const float controlsWidth = std::max(190.0f, contentWidth - graphWidth - 78.0f);
    return {{controlsX, panelTop + 76.0f}, {controlsWidth, 0.0f}};
}

SU::Rect ToggleRect(float width, float panelTop) {
    const SU::Rect panel = ControlPanelBounds(width, panelTop);
    return {{panel.position.x + 18.0f, panel.position.y + 78.0f}, {46.0f, 26.0f}};
}

SU::Rect SliderRect(float width, float panelTop) {
    const SU::Rect panel = ControlPanelBounds(width, panelTop);
    return {{panel.position.x + 18.0f, panel.position.y + 126.0f},
            {panel.size.x - 36.0f, 24.0f}};
}

SU::Rect TabRect(float width, const SpriteUIVisualizer::BitmapFont& font, std::size_t index) {
    constexpr float contentX = 229.0f;
    constexpr float gap = 10.0f;
    const float contentWidth = std::max(320.0f, width - contentX - 24.0f);
    const float tabWidth = (contentWidth - gap * 2.0f) / 3.0f;
    return {{contentX + static_cast<float>(index) * (tabWidth + gap), TabsTop(font)},
            {tabWidth, 32.0f}};
}

SU::Rect FloatingButtonRect(float width, float height, float panelTop, bool fadeControl) {
    constexpr float contentX = 229.0f;
    const float contentWidth = std::max(320.0f, width - contentX - 24.0f);
    const float panelHeight = std::max(210.0f, height - panelTop - 30.0f);
    const float x = contentX + contentWidth - (fadeControl ? 126.0f : 70.0f);
    return {{x, panelTop + panelHeight - 66.0f}, {48.0f, 48.0f}};
}

void AddTarget(std::vector<Input::HitTarget>& targets, SU::UIHandle handle, SU::Rect rect,
               std::uint64_t paintOrder) {
    Input::HitTarget target;
    target.handle = handle;
    target.localToScreen.tx = rect.position.x;
    target.localToScreen.ty = rect.position.y;
    target.localSize = rect.size;
    target.paintOrder = paintOrder;
    target.focusable = true;
    targets.push_back(target);
}

std::vector<Input::HitTarget> BuildTargets(float width, float height,
                                           const SpriteUIVisualizer::BitmapFont& font) {
    std::vector<Input::HitTarget> targets;
    AddTarget(targets, PaletteButton, ButtonRect(width), 10);
    const float panelTop = MainPanelTop(font);
    AddTarget(targets, ToggleButton, ToggleRect(width, panelTop), 20);
    AddTarget(targets, SliderTarget, SliderRect(width, panelTop), 30);
    AddTarget(targets, OverviewTab, TabRect(width, font, 0), 40);
    AddTarget(targets, StyleTab, TabRect(width, font, 1), 41);
    AddTarget(targets, LayoutTab, TabRect(width, font, 2), 42);
    AddTarget(targets, FadeButton, FloatingButtonRect(width, height, panelTop, true), 50);
    AddTarget(targets, FloatingActionButton, FloatingButtonRect(width, height, panelTop, false), 51);
    AddTarget(targets, TextFieldTarget, TextFieldRect(), 60);
    return targets;
}

Visual::Image BuildScene(int width, int height, std::size_t paletteIndex,
                         bool toggleOn, float sliderValue, std::size_t selectedTab,
                         float fabOpacity,
                         const Input::UIPointerRoutingState& routing,
                         SU::Vec2 pointer, std::string_view textFieldValue,
                         const SU::Widgets::UITextInputComponent& textFieldEditor,
                         const SpriteUIVisualizer::BitmapFont& font,
                         const SpriteUIVisualizer::BitmapImage& logo,
                         float dpiScale = 1.0f) {
    const Palette& colors = Palettes[paletteIndex % Palettes.size()];
    const float w = static_cast<float>(width) / dpiScale;
    const float h = static_cast<float>(height) / dpiScale;
    const float sidebarWidth = 205.0f;
    const float contentX = sidebarWidth + 24.0f;
    const float contentWidth = std::max(320.0f, w - contentX - 24.0f);
    const float cardGap = 14.0f;
    const float cardWidth = (contentWidth - cardGap * 2.0f) / 3.0f;
    const SU::Vec4 hover = {std::min(colors.accent.x + 0.12f, 1.0f),
                            std::min(colors.accent.y + 0.12f, 1.0f),
                            std::min(colors.accent.z + 0.12f, 1.0f), 1.0f};

    Visual::Scene scene(colors.background);
    scene.Rect({{0, 0}, {w, 68.0f}}, colors.panel, 0, Rendering::FlagRoundedPanel, 0.0f);
    scene.Rect({{0, 68.0f}, {sidebarWidth, h - 68.0f}}, colors.panel);
    scene.Rect({{sidebarWidth, 68.0f}, {1.0f, h - 68.0f}}, colors.surface);

    scene.Rect({{24.0f, 18.0f}, {32.0f, 32.0f}}, colors.accent,
               0, Rendering::FlagRoundedPanel, 9.0f);
    scene.Rect({{34.0f, 25.0f}, {5.0f, 18.0f}}, colors.text,
               0, Rendering::FlagRoundedPanel, 2.0f);
    scene.Rect({{27.0f, 31.0f}, {19.0f, 5.0f}}, colors.text,
               0, Rendering::FlagRoundedPanel, 2.0f);

    const SU::Rect paletteRect = ButtonRect(w);
    scene.Rect(paletteRect, routing.hoveredEntity == PaletteButton ? hover : colors.surface,
               0, Rendering::FlagRoundedPanel, 8.0f);
    scene.Outline(paletteRect, 1.0f, colors.accent);
    TextLine(scene, font, 72.0f, 23.0f, "SPRITE UI / CPU PREVIEW",
             colors.text, TextStyle::Title);
    TextLine(scene, font, w - 148.0f, 31.0f, "CYCLE PALETTE",
             colors.text, TextStyle::Label);

    const SU::Rect textField = TextFieldRect();
    scene.Rect(textField, colors.surface, 0, Rendering::FlagRoundedPanel, 7.0f);
    if (textFieldEditor.hasFocus) scene.Outline(textField, 1.0f, colors.accent);
    TextLine(scene, font, 38.0f, 78.0f, "WORKSPACE", colors.text, TextStyle::Subtitle);
    for (int i = 0; i < 5; ++i) {
        const float y = 164.0f + static_cast<float>(i) * 48.0f;
        const bool selected = i == 0;
        scene.Rect({{24.0f, y}, {sidebarWidth - 48.0f, 36.0f}},
                   selected ? colors.accent : colors.panel, 0,
                   Rendering::FlagRoundedPanel, 7.0f);
        scene.Rect({{38.0f, y + 11.0f}, {14.0f, 14.0f}},
                   selected ? colors.text : colors.muted, 0,
                   Rendering::FlagRoundedPanel, 4.0f);
    }
    TextLine(scene, font, 64.0f, 175.0f, "OVERVIEW", colors.text, TextStyle::Body);
    TextLine(scene, font, 64.0f, 223.0f, "COMPONENTS", colors.muted, TextStyle::Body);
    TextLine(scene, font, 64.0f, 271.0f, "LAYOUT", colors.muted, TextStyle::Body);
    TextLine(scene, font, 64.0f, 319.0f, "RENDERING", colors.muted, TextStyle::Body);
    TextLine(scene, font, 64.0f, 367.0f, "INTERACTION", colors.muted, TextStyle::Body);
    const float brandCaptionY = TextLine(
        scene, font, 38.0f, 402.0f, "SPRITE UI", colors.muted, TextStyle::Subtitle);
    TextLine(scene, font, 38.0f, brandCaptionY, "REFERENCE VIEW",
             colors.muted, TextStyle::Caption);
    const auto fieldLayout = LayoutTextField(textFieldValue, font);
    const SU::Rect fieldTextClip{{textField.position.x + 7.0f, textField.position.y + 4.0f},
                                {textField.size.x - 14.0f, textField.size.y - 8.0f}};
    const std::uint32_t fieldClipIndex = scene.AddClip({
        fieldTextClip.position.x, fieldTextClip.position.y,
        fieldTextClip.position.x + fieldTextClip.size.x,
        fieldTextClip.position.y + fieldTextClip.size.y});
    const SU::Rect caret = SU::Text::GetCaretRect(
        fieldLayout, std::min<std::uint32_t>(
            textFieldEditor.cursorCluster, static_cast<std::uint32_t>(textFieldValue.size())));
    const float horizontalScroll = TextFieldScrollX(
        fieldLayout, fieldTextClip.size.x, textFieldEditor.cursorCluster);
    const SU::Vec2 fieldTextOrigin{
        fieldTextClip.position.x - horizontalScroll,
        fieldTextClip.position.y + (fieldTextClip.size.y - fieldLayout.lineHeight) * 0.5f
    };
    scene.Text(fieldLayout, fieldTextOrigin, colors.text,
               SpriteUIVisualizer::BitmapFont::TextureSlice,
               Rendering::FlagBitmapText, fieldClipIndex);
    if (textFieldEditor.IsCursorVisible()) {
        scene.Caret(caret, fieldTextOrigin, colors.accent, 1.5f);
        scene.Items().back().clipIndex = fieldClipIndex;
    }
    TextLine(scene, font, 38.0f, 143.0f, "ENTER TO READ VALUE",
             colors.muted, TextStyle::Caption);

    float cardsY = TextLine(
        scene, font, contentX + 30.0f, 76.0f, "INTERFACE QUALITY SANDBOX",
        colors.text, TextStyle::Title);
    cardsY = TextLine(scene, font, contentX + 30.0f, cardsY,
                      "LAYOUT COLOR CLIPPING POINTER STATES",
                      colors.muted, TextStyle::Subtitle) + 20.0f;

    constexpr std::array<std::string_view, 3> tabLabels{{"OVERVIEW", "STYLE GUIDE", "LAYOUT"}};
    constexpr std::array<SU::UIHandle, 3> tabHandles{{OverviewTab, StyleTab, LayoutTab}};
    constexpr SU::Vec4 gradientEnds[3] = {
        {0.55f, 0.38f, 0.96f, 1.0f},
        {0.98f, 0.49f, 0.31f, 1.0f},
        {0.20f, 0.75f, 0.70f, 1.0f}
    };
    for (std::size_t i = 0; i < tabLabels.size(); ++i) {
        const SU::Rect tab = TabRect(w, font, i);
        const bool selected = i == selectedTab;
        const bool hovered = routing.hoveredEntity == tabHandles[i];
        if (selected) {
            scene.Rect(tab, {1, 1, 1, 1}, GradientTextureSlice,
                       Rendering::FlagRoundedPanel, 8.0f);
        } else {
            scene.Rect(tab, hovered ? hover : colors.surface,
                       0, Rendering::FlagRoundedPanel, 8.0f);
        }
        TextLine(scene, font, tab.position.x + 14.0f, tab.position.y + 9.0f,
                 tabLabels[i], selected ? colors.background : colors.muted, TextStyle::Caption);
    }
    cardsY += 44.0f;

    for (int i = 0; i < 3; ++i) {
        const float x = contentX + static_cast<float>(i) * (cardWidth + cardGap);
        scene.Rect({{x, cardsY}, {cardWidth, 112.0f}}, colors.panel,
                   0, Rendering::FlagRoundedPanel, 12.0f);
    }
    constexpr std::array<std::string_view, 3> cardLabels{{"FRAME TIME", "RENDER ITEMS", "FEATURED IMAGE"}};
    constexpr std::array<std::string_view, 3> cardValues{{"12.4 MS", "1,284", "SPRITE UI"}};
    constexpr std::array<std::string_view, 3> cardChanges{{"+8.2%", "+12.6%", "PNG / ATLAS TEST"}};
    for (int i = 0; i < 3; ++i) {
        const float x = contentX + static_cast<float>(i) * (cardWidth + cardGap) + 18.0f;
        const float top = cardsY + 13.0f;
        float nextY = TextLine(scene, font, x, top,
            cardLabels[static_cast<std::size_t>(i)], colors.muted, TextStyle::Caption);
        nextY = TextLine(scene, font, x, nextY,
            cardValues[static_cast<std::size_t>(i)], colors.text, TextStyle::Metric);
        (void)TextLine(scene, font, x, nextY,
            cardChanges[static_cast<std::size_t>(i)], colors.accent, TextStyle::Label);
        if (i == 2 && logo.IsLoaded()) {
            const float iconSide = 70.0f;
            const float sourceAspect = static_cast<float>(logo.Width()) /
                                       static_cast<float>(logo.Height());
            float iconWidth = iconSide;
            float iconHeight = iconSide;
            if (sourceAspect > 1.0f) iconHeight /= sourceAspect;
            else iconWidth *= sourceAspect;
            const float imageX = x + cardWidth - 18.0f - iconWidth;
            const float imageY = cardsY + (112.0f - iconHeight) * 0.5f;
            Rendering::UIIcon icon;
            icon.textureSlice = LogoTextureSlice;
            icon.sourceSize = {static_cast<float>(logo.Width()),
                               static_cast<float>(logo.Height())};
            const auto renderItem = Rendering::MakeIconRenderItem(
                icon, {{imageX, imageY}, {iconWidth, iconHeight}},
                {1.0f, 1.0f, 1.0f, 1.0f}, {100, 1}, Rendering::NoClipIndex,
                0.0f, 1000);
            if (renderItem) scene.Items().push_back(*renderItem);
        }
    }

    const float panelY = cardsY + 126.0f;
    const float panelHeight = std::max(210.0f, h - panelY - 30.0f);
    scene.Rect({{contentX, panelY}, {contentWidth, panelHeight}}, colors.panel,
               0, Rendering::FlagRoundedPanel, 12.0f);
    float panelHeadingY = TextLine(scene, font, contentX + 20.0f, panelY + 18.0f,
                                   "FRAME ANALYTICS", colors.text, TextStyle::Subtitle);
    (void)TextLine(scene, font, contentX + 20.0f, panelHeadingY,
                   "32 SAMPLES / LIVE PREVIEW", colors.muted, TextStyle::Caption);

    const float graphX = contentX + 24.0f;
    const float graphY = panelY + 76.0f;
    const float graphWidth = contentWidth * 0.48f;
    const float graphHeight = std::max(90.0f, panelHeight - 122.0f);
    const std::uint32_t graphClip = scene.AddClip({
        graphX, graphY, graphX + graphWidth, graphY + graphHeight});
    for (int row = 0; row < 4; ++row) {
        const float y = graphY + static_cast<float>(row) * graphHeight / 3.0f;
        scene.Rect({{graphX, y}, {graphWidth, 1.0f}}, colors.surface);
    }
    constexpr std::array<float, 32> samples{{
        0.24f, 0.32f, 0.29f, 0.44f, 0.39f, 0.55f, 0.47f, 0.62f,
        0.52f, 0.68f, 0.61f, 0.74f, 0.60f, 0.48f, 0.57f, 0.43f,
        0.50f, 0.66f, 0.59f, 0.78f, 0.69f, 0.82f, 0.72f, 0.63f,
        0.77f, 0.86f, 0.72f, 0.91f, 0.80f, 0.70f, 0.84f, 0.93f
    }};
    const float barGap = 3.0f;
    const float barWidth = std::max(2.0f, (graphWidth - barGap * 31.0f) / 32.0f);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float barHeight = samples[i] * graphHeight;
        const float x = graphX + static_cast<float>(i) * (barWidth + barGap);
        const float y = graphY + graphHeight - barHeight;
        scene.Rect({{x, y}, {barWidth, barHeight}}, colors.accent,
                   0, Rendering::FlagRoundedPanel, std::min(barWidth * 0.45f, 3.0f), graphClip);
    }

    const SU::Rect controls = ControlPanelBounds(w, panelY);
    const float controlsX = controls.position.x;
    const float controlsWidth = controls.size.x;
    scene.Rect({{controlsX, graphY}, {controlsWidth, graphHeight}}, colors.surface,
               0, Rendering::FlagRoundedPanel, 9.0f);
    const float interactionSubheadingY = TextLine(
        scene, font, controlsX + 18.0f, graphY + 18.0f, "INTERACTION",
        colors.text, TextStyle::Subtitle);
    (void)TextLine(scene, font, controlsX + 18.0f, interactionSubheadingY,
                   "ROUNDED PANELS / CLIP", colors.muted, TextStyle::Caption);

    const SU::Rect toggleRect = ToggleRect(w, panelY);
    scene.Rect(toggleRect, toggleOn ? colors.accent : colors.muted,
               0, Rendering::FlagRoundedPanel, 13.0f);
    const float knobX = toggleOn ? toggleRect.position.x + 32.0f : toggleRect.position.x + 14.0f;
    scene.Rect({{knobX, toggleRect.position.y + 4.0f}, {18.0f, 18.0f}}, colors.text,
               0, Rendering::FlagRoundedPanel, 9.0f);
    TextLine(scene, font, controlsX + 76.0f, toggleRect.position.y + 6.0f,
             toggleOn ? "TOGGLE ON" : "TOGGLE OFF", colors.text, TextStyle::Body);

    const SU::Rect slider = SliderRect(w, panelY);
    scene.Rect({{slider.position.x, slider.position.y + 9.0f},
                {slider.size.x, 6.0f}}, colors.muted,
               0, Rendering::FlagRoundedPanel, 3.0f);
    scene.Rect({{slider.position.x, slider.position.y + 9.0f},
                {slider.size.x * sliderValue, 6.0f}}, colors.accent,
               0, Rendering::FlagRoundedPanel, 3.0f);
    scene.Rect({{slider.position.x + slider.size.x * sliderValue - 9.0f,
                 slider.position.y + 3.0f}, {18.0f, 18.0f}},
               routing.hoveredEntity == SliderTarget ? hover : colors.text,
               0, Rendering::FlagRoundedPanel, 9.0f);
    const float sliderHintY = TextLine(
        scene, font, slider.position.x, slider.position.y + 34.0f,
        "ACCENT INTENSITY", colors.text, TextStyle::Label);
    TextLine(scene, font, slider.position.x, sliderHintY,
             "DRAG SLIDER TO ADJUST", colors.muted, TextStyle::Caption);

    const SU::Rect fadeRect = FloatingButtonRect(w, h, panelY, true);
    scene.Rect(fadeRect, routing.hoveredEntity == FadeButton ? hover : colors.surface,
               0, Rendering::FlagRoundedPanel, 24.0f);
    scene.Outline(fadeRect, 1.0f, colors.accent);
    scene.Rect({{fadeRect.position.x + 14.0f, fadeRect.position.y + 22.0f}, {4.0f, 5.0f}},
               colors.accent, 0, Rendering::FlagRoundedPanel, 2.0f);
    scene.Rect({{fadeRect.position.x + 22.0f, fadeRect.position.y + 17.0f}, {4.0f, 10.0f}},
               colors.accent, 0, Rendering::FlagRoundedPanel, 2.0f);
    scene.Rect({{fadeRect.position.x + 30.0f, fadeRect.position.y + 12.0f}, {4.0f, 15.0f}},
               colors.accent, 0, Rendering::FlagRoundedPanel, 2.0f);

    const SU::Rect fabRect = FloatingButtonRect(w, h, panelY, false);
    scene.Rect(fabRect, colors.accent, 0, Rendering::FlagRoundedPanel, 24.0f);
    scene.Items().back().opacity = fabOpacity;
    const SU::Vec4 fabIconColor{colors.text.x, colors.text.y, colors.text.z, 1.0f};
    scene.Rect({{fabRect.position.x + 22.0f, fabRect.position.y + 13.0f}, {4.0f, 22.0f}},
               fabIconColor, 0, Rendering::FlagRoundedPanel, 2.0f);
    scene.Items().back().opacity = fabOpacity;
    scene.Rect({{fabRect.position.x + 13.0f, fabRect.position.y + 22.0f}, {22.0f, 4.0f}},
               fabIconColor, 0, Rendering::FlagRoundedPanel, 2.0f);
    scene.Items().back().opacity = fabOpacity;

    TextLine(scene, font, 24.0f, h - 16.0f, "S SAVE PNG  CLICK CONTROLS  DRAG SLIDER",
             colors.muted, TextStyle::Footer);
    TextLine(scene, font, w - 284.0f, h - 16.0f, "CPU PREVIEW / NOT GPU",
             colors.muted, TextStyle::Footer);

    if (pointer.x >= 0.0f && pointer.y >= 0.0f) {
        scene.Rect({{pointer.x - 3.0f, pointer.y - 3.0f}, {6.0f, 6.0f}},
                   colors.text, 0, Rendering::FlagRoundedPanel, 3.0f);
    }
    return scene.Render(width, height, dpiScale,
        [&font, &logo, &colors, &gradientEnds, selectedTab](std::uint32_t slice, SU::Vec2 uv) {
            if (slice == GradientTextureSlice) {
                const SU::Vec4 start = Rendering::SrgbStraightToLinearPremultiplied(colors.accent);
                const SU::Vec4 end = Rendering::SrgbStraightToLinearPremultiplied(
                    gradientEnds[selectedTab % 3]);
                return start * (1.0f - SU::Clamp01(uv.x)) + end * SU::Clamp01(uv.x);
            }
            if (slice == LogoTextureSlice) return logo.Sample(uv);
            return font.Sample(slice, uv);
        });
}

bool ContainsAntialiasedHeaderText(const Visual::Image& image) {
    std::size_t coveredPixels = 0;
    std::size_t edgePixels = 0;
    const Palette& colors = Palettes[0];
    for (int y = 18; y < 58; ++y) {
        for (int x = 68; x < 340; ++x) {
            if (Visual::PixelIs(image, x, y, colors.panel, 3)) continue;
            ++coveredPixels;
            if (!Visual::PixelIs(image, x, y, colors.text, 3)) ++edgePixels;
        }
    }
    return coveredPixels >= 40 && edgePixels > 0;
}

bool ContainsTextFieldContent(const Visual::Image& image) {
    std::size_t textPixels = 0;
    for (int y = 108; y < 134; ++y) {
        for (int x = 32; x < 180; ++x) {
            if (!Visual::PixelIs(image, x, y, Palettes[0].surface, 3)) ++textPixels;
        }
    }
    return textPixels >= 20;
}

bool ContainsFeaturedLogo(const Visual::Image& image,
                          const SpriteUIVisualizer::BitmapFont& font) {
    const SU::Rect tab = TabRect(static_cast<float>(image.width), font, 0);
    const float contentX = 229.0f;
    const float cardGap = 14.0f;
    const float contentWidth = std::max(320.0f, static_cast<float>(image.width) - contentX - 24.0f);
    const float cardWidth = (contentWidth - cardGap * 2.0f) / 3.0f;
    const float cardsY = tab.position.y + 44.0f;
    const int left = static_cast<int>(std::lround(contentX + 2.0f * (cardWidth + cardGap) +
                                                  cardWidth - 94.0f));
    const int top = static_cast<int>(std::lround(cardsY + 10.0f));
    std::size_t brightPixels = 0;
    for (int y = top; y < top + 74; ++y) {
        for (int x = left; x < left + 74; ++x) {
            if (x < 0 || y < 0 || x >= image.width || y >= image.height) continue;
            const auto pixel = image.Srgb8(x, y);
            if (pixel[0] > 210 && pixel[1] > 210 && pixel[2] > 210) ++brightPixels;
        }
    }
    return brightPixels >= 100;
}

bool UploadImage(SDL_Texture* texture, const Visual::Image& image) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto color = image.Srgb8(x, y);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) * 4u;
            std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
    return SDL_UpdateTexture(texture, nullptr, pixels.data(), image.width * 4);
}

std::filesystem::path ResolveFontPath(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& requestedPath) {
    if (!requestedPath.empty()) return requestedPath;
    const std::filesystem::path relativeFont =
        std::filesystem::path("Fonts") / "Xmind" / "static" / "Xmind-Regular.ttf";
    std::error_code error;
    const std::filesystem::path executableDirectory =
        std::filesystem::absolute(executablePath, error).parent_path();
    const std::array<std::filesystem::path, 3> candidates{{
        executableDirectory / relativeFont,
        executableDirectory.parent_path() / relativeFont,
        std::filesystem::current_path(error) / relativeFont
    }};
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        error.clear();
    }
    return executableDirectory / relativeFont;
}

std::filesystem::path ResolveLogoPath(const std::filesystem::path& executablePath) {
    const std::filesystem::path relativeImage = std::filesystem::path("fotos") / "LOGO.png";
    std::error_code error;
    const std::filesystem::path executableDirectory =
        std::filesystem::absolute(executablePath, error).parent_path();
    const std::array<std::filesystem::path, 3> candidates{{
        executableDirectory / relativeImage,
        executableDirectory.parent_path() / relativeImage,
        std::filesystem::current_path(error) / relativeImage
    }};
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        error.clear();
    }
    return executableDirectory / relativeImage;
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    std::filesystem::path requestedFontPath;
    std::filesystem::path requestedImagePath;
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--headless") {
            headless = true;
        } else if (argument == "--font" && i + 1 < argc) {
            requestedFontPath = argv[++i];
        } else if (argument == "--image" && i + 1 < argc) {
            requestedImagePath = argv[++i];
        } else {
            std::cerr << "Usage: spriteui_visualizer [--headless] "
                         "[--font <TTF-or-OTF-path>] [--image <PNG-path>]\n";
            return 2;
        }
    }

    const std::filesystem::path fontPath = ResolveFontPath(argv[0], requestedFontPath);
    SpriteUIVisualizer::BitmapFont font;
    std::string fontError;
    if (!SpriteUIVisualizer::BitmapFont::Load(fontPath, font, fontError)) {
        std::cerr << "Could not load interface font '" << fontPath.string()
                  << "': " << fontError << '\n';
        return 1;
    }
    std::cout << "Interface font: " << font.FamilyName()
              << " (" << font.SourcePath().string() << ")\n";

    const std::filesystem::path imagePath = requestedImagePath.empty()
        ? ResolveLogoPath(argv[0])
        : requestedImagePath;
    SpriteUIVisualizer::BitmapImage logo;
    std::string imageError;
    if (!SpriteUIVisualizer::BitmapImage::LoadPng(imagePath, logo, imageError)) {
        std::cerr << "Could not load visualizer image '" << imagePath.string()
                  << "': " << imageError << '\n';
        return 1;
    }
    std::cout << "Featured image: " << logo.Width() << 'x' << logo.Height()
              << " (" << logo.SourcePath().string() << ")\n";

    if (headless) {
        TextFieldState fieldTest;
        if (ReadTextFieldValue(fieldTest) != "god only knows") {
            std::cerr << "The text field did not initialize with the requested value.\n";
            return 1;
        }
        fieldTest.editor.MoveCursor(3);
        InsertCommittedText(fieldTest, " UI");
        if (ReadTextFieldValue(fieldTest) != "god UI only knows") {
            std::cerr << "The text field did not insert text at the cursor.\n";
            return 1;
        }
        DeletePreviousCodePoint(fieldTest);
        if (ReadTextFieldValue(fieldTest) != "god U only knows") {
            std::cerr << "The text field did not delete the previous character.\n";
            return 1;
        }
        TextFieldState unicodeTest;
        unicodeTest.value = "A\xc3\xa9" "B";
        unicodeTest.editor.cursorCluster = 3;
        DeletePreviousCodePoint(unicodeTest);
        if (ReadTextFieldValue(unicodeTest) != "AB") {
            std::cerr << "The text field split a UTF-8 code point while deleting.\n";
            return 1;
        }
        std::cout << "Headless text field edit/readback passed: "
                  << ReadTextFieldValue(fieldTest) << '\n';

        TextFieldState previewField;
        const Visual::Image image = BuildScene(
            1180, 760, 0, true, 0.64f, 0, 0.68f, {}, {-1.0f, -1.0f},
            previewField.value, previewField.editor, font, logo);
        if (!ContainsAntialiasedHeaderText(image)) {
            std::cerr << "The font atlas did not render the expected antialiased header text.\n";
            return 1;
        }
        if (!ContainsTextFieldContent(image)) {
            std::cerr << "The initial text field value did not render in the preview.\n";
            return 1;
        }
        if (!ContainsFeaturedLogo(image, font)) {
            std::cerr << "The featured PNG did not render in the visualizer preview.\n";
            return 1;
        }
        const std::string path = Visual::SaveSnapshot(image, "spriteui_visualizer_preview");
        if (path.empty()) {
            std::cerr << "Could not save PNG snapshot to "
                      << Visual::SnapshotDir().string() << '\n';
            return 1;
        }
        std::cout << "Saved SpriteUI CPU preview: " << path << '\n';
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    const std::string windowTitle =
        "SpriteUI Visualizer - " + font.FamilyName() + " (CPU reference)";
    SDL_Window* window = SDL_CreateWindow(
        windowTitle.c_str(),
        1180, 760, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    (void)SDL_SetWindowMinimumSize(window, 900, 600);

    bool running = true;
    bool focused = true;
    bool primaryDown = false;
    bool primaryPressed = false;
    bool primaryReleased = false;
    bool saveRequested = false;
    bool nativeTextInputActive = false;
    bool toggleOn = true;
    bool fabVisible = true;
    bool dirty = true;
    float sliderValue = 0.64f;
    float fabOpacity = 1.0f;
    float previousMouseX = 0.0f;
    float previousMouseY = 0.0f;
    std::size_t paletteIndex = 0;
    std::size_t selectedTab = 0;
    std::uint64_t frameId = 0;
    Input::UIEventRouter router;
    TextFieldState textField;
    Animation::ToonMotionSystem motion;
    auto previousUpdateTime = std::chrono::steady_clock::now();
    SDL_Texture* frameTexture = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    constexpr float InteractiveRenderScale = 0.5f;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED &&
                       event.window.windowID == SDL_GetWindowID(window)) {
                focused = true;
                dirty = true;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
                       event.window.windowID == SDL_GetWindowID(window)) {
                focused = false;
                primaryDown = false;
                dirty = true;
            } else if (event.type == SDL_EVENT_TEXT_INPUT &&
                       event.text.windowID == SDL_GetWindowID(window) &&
                       focused && textField.editor.hasFocus && event.text.text) {
                InsertCommittedText(textField, event.text.text);
                dirty = true;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       event.button.windowID == SDL_GetWindowID(window) &&
                       event.button.button == SDL_BUTTON_LEFT) {
                primaryDown = true;
                primaryPressed = true;
                dirty = true;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                       event.button.windowID == SDL_GetWindowID(window) &&
                       event.button.button == SDL_BUTTON_LEFT) {
                primaryDown = false;
                primaryReleased = true;
                dirty = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       event.key.windowID == SDL_GetWindowID(window)) {
                if (textField.editor.hasFocus) {
                    switch (event.key.key) {
                    case SDLK_BACKSPACE:
                        DeletePreviousCodePoint(textField);
                        dirty = true;
                        break;
                    case SDLK_DELETE:
                        DeleteNextCodePoint(textField);
                        dirty = true;
                        break;
                    case SDLK_LEFT: {
                        const std::size_t cursor = textField.editor.cursorCluster;
                        textField.editor.MoveCursor(static_cast<std::uint32_t>(
                            PreviousCodePointStart(textField.value, cursor)));
                        dirty = true;
                        break;
                    }
                    case SDLK_RIGHT: {
                        const std::size_t cursor = textField.editor.cursorCluster;
                        textField.editor.MoveCursor(static_cast<std::uint32_t>(
                            NextCodePointEnd(textField.value, cursor)));
                        dirty = true;
                        break;
                    }
                    case SDLK_HOME:
                        textField.editor.MoveCursor(0);
                        dirty = true;
                        break;
                    case SDLK_END:
                        textField.editor.MoveCursor(
                            static_cast<std::uint32_t>(textField.value.size()));
                        dirty = true;
                        break;
                    case SDLK_RETURN:
                    case SDLK_RETURN2:
                        if (!event.key.repeat) {
                            std::cout << "Text field value returned to application: "
                                      << ReadTextFieldValue(textField) << '\n';
                        }
                        break;
                    default:
                        break;
                    }
                } else if (!event.key.repeat && event.key.key == SDLK_S) {
                    saveRequested = true;
                    dirty = true;
                }
            }
        }
        if (!running) break;

        float mouseX = 0.0f;
        float mouseY = 0.0f;
        (void)SDL_GetMouseState(&mouseX, &mouseY);
        if (mouseX != previousMouseX || mouseY != previousMouseY) dirty = true;

        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) {
            SDL_Delay(16);
            continue;
        }
        const int frameWidth = std::max(
            1, static_cast<int>(static_cast<float>(width) * InteractiveRenderScale));
        const int frameHeight = std::max(
            1, static_cast<int>(static_cast<float>(height) * InteractiveRenderScale));
        if (frameWidth != textureWidth || frameHeight != textureHeight) {
            if (frameTexture) SDL_DestroyTexture(frameTexture);
            frameTexture = SDL_CreateTexture(
                renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, frameWidth, frameHeight);
            if (!frameTexture) {
                std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
                running = false;
                break;
            }
            textureWidth = frameWidth;
            textureHeight = frameHeight;
            dirty = true;
        }

        const auto targets = BuildTargets(static_cast<float>(width),
                                          static_cast<float>(height), font);
        Input::UIInputSnapshot input;
        input.pointerPositionLogical = {mouseX, mouseY};
        input.pointerDeltaLogical = {mouseX - previousMouseX, mouseY - previousMouseY};
        input.primaryDown = primaryDown;
        input.primaryPressed = primaryPressed;
        input.primaryReleased = primaryReleased;
        input.windowFocused = focused;
        input.frameId = ++frameId;
        const auto uiEvents = router.Process(input, targets);
        for (const Input::UIEvent& uiEvent : uiEvents) {
            if (uiEvent.type == Input::UIEventType::PointerDown &&
                uiEvent.target == TextFieldTarget) {
                const auto layout = LayoutTextField(textField.value, font);
                const SU::Rect rect = TextFieldRect();
                const float visibleWidth = rect.size.x - 14.0f;
                const float scrollX = TextFieldScrollX(
                    layout, visibleWidth, textField.editor.cursorCluster);
                const std::uint32_t byteOffset = SU::Text::FindInsertionOffset(
                    layout, {uiEvent.localPosition.x - 7.0f + scrollX, 0.0f});
                textField.editor.MoveCursor(byteOffset);
                dirty = true;
            }
            if (uiEvent.type != Input::UIEventType::Click) continue;
            if (uiEvent.target == PaletteButton) {
                paletteIndex = (paletteIndex + 1u) % Palettes.size();
            } else if (uiEvent.target == ToggleButton) {
                toggleOn = !toggleOn;
            } else if (uiEvent.target == OverviewTab) {
                selectedTab = 0;
            } else if (uiEvent.target == StyleTab) {
                selectedTab = 1;
            } else if (uiEvent.target == LayoutTab) {
                selectedTab = 2;
            } else if (uiEvent.target == FadeButton) {
                fabVisible = !fabVisible;
                Animation::TweenTrack fade;
                fade.target = FloatingActionButton;
                fade.property = Animation::TweenProperty::Opacity;
                fade.from = {fabOpacity, 0.0f, 0.0f, 0.0f};
                fade.to = {fabVisible ? 1.0f : 0.18f, 0.0f, 0.0f, 0.0f};
                fade.duration = 0.36f;
                fade.easing = Animation::Easing::SmoothStep;
                if (!motion.Add(fade)) {
                    std::cerr << "Could not start floating-button fade animation.\n";
                    running = false;
                }
            } else if (uiEvent.target == FloatingActionButton) {
                std::cout << "Floating action button activated.\n";
            }
            dirty = true;
        }

        const bool textFieldFocused =
            focused && router.GetState().focusedEntity == TextFieldTarget;
        if (textField.editor.hasFocus != textFieldFocused) {
            textField.editor.hasFocus = textFieldFocused;
            textField.editor.blinkTimer = 0.0f;
            const bool success = textFieldFocused
                ? SDL_StartTextInput(window)
                : SDL_StopTextInput(window);
            if (!success) {
                std::cerr << "Could not " << (textFieldFocused ? "start" : "stop")
                          << " SDL text input: " << SDL_GetError() << '\n';
                running = false;
            } else {
                nativeTextInputActive = textFieldFocused;
            }
            dirty = true;
        }

        const auto updateTime = std::chrono::steady_clock::now();
        const float deltaTime = std::clamp(
            std::chrono::duration<float>(updateTime - previousUpdateTime).count(),
            0.0f, 0.05f);
        previousUpdateTime = updateTime;
        const Animation::TweenUpdate animationUpdate = motion.Update(deltaTime);
        const bool cursorWasVisible = textField.editor.IsCursorVisible();
        textField.editor.UpdateBlink(deltaTime);
        dirty = dirty || cursorWasVisible != textField.editor.IsCursorVisible();
        for (const Animation::TweenSample& sample : animationUpdate.samples) {
            if (sample.target == FloatingActionButton &&
                sample.property == Animation::TweenProperty::Opacity) {
                fabOpacity = SU::Clamp01(sample.value.x);
                dirty = true;
            }
        }

        const SU::Rect sliderRect = SliderRect(
            static_cast<float>(width), MainPanelTop(font));
        if (primaryDown && router.GetState().capturedEntity == SliderTarget) {
            sliderValue = SU::Clamp01(
                (mouseX - sliderRect.position.x) / sliderRect.size.x);
            dirty = true;
        }

        if (textField.editor.hasFocus && dirty &&
            !UpdateNativeTextInputArea(window, textField, font)) {
            std::cerr << "Could not update SDL text input area: " << SDL_GetError() << '\n';
            running = false;
            break;
        }

        if (dirty) {
            if (saveRequested) {
                const Visual::Image fullResolution = BuildScene(
                    width, height, paletteIndex, toggleOn, sliderValue,
                    selectedTab, fabOpacity,
                    router.GetState(), {mouseX, mouseY},
                    textField.value, textField.editor, font, logo);
                const std::string path = Visual::SaveSnapshot(
                    fullResolution, "spriteui_visualizer_" + std::to_string(frameId));
                if (path.empty()) {
                    std::cerr << "Could not save PNG snapshot to "
                              << Visual::SnapshotDir().string() << '\n';
                } else {
                    std::cout << "Saved SpriteUI CPU preview: " << path << '\n';
                }
                saveRequested = false;
            }
            const Visual::Image image = BuildScene(
                frameWidth, frameHeight, paletteIndex, toggleOn, sliderValue,
                selectedTab, fabOpacity,
                router.GetState(), {mouseX, mouseY},
                textField.value, textField.editor, font, logo, InteractiveRenderScale);
            if (!UploadImage(frameTexture, image)) {
                std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
                running = false;
                break;
            }
            (void)SDL_RenderClear(renderer);
            if (!SDL_RenderTexture(renderer, frameTexture, nullptr, nullptr)) {
                std::cerr << "SDL_RenderTexture failed: " << SDL_GetError() << '\n';
                running = false;
                break;
            }

            SDL_RenderPresent(renderer);
            dirty = false;
        }

        previousMouseX = mouseX;
        previousMouseY = mouseY;
        primaryPressed = false;
        primaryReleased = false;
        SDL_Delay(8);
    }

    if (nativeTextInputActive) (void)SDL_StopTextInput(window);
    if (frameTexture) SDL_DestroyTexture(frameTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
