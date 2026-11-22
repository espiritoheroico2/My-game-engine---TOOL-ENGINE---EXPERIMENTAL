#pragma once

// Janela Configuracoes em SpriteUI. Nesta rodada, a faixa de abas e a aba Graficos sao SpriteUI; as outras abas
// continuam no ImGui, logo abaixo da faixa (main.cpp: drawSettings).
//
// Regras e nomes vem de settings_model.hpp. Os elementos (abas, radio, combo, deslizante, caixa) sao ligados por
// settings_bridge.hpp. Os pedidos do usuario saem em settings_changes::Requests e o main.cpp grava no Editor.
// Este arquivo cuida de posicao, texto, rolagem, clique e desenho.
//
// Exemplo: a aba Graficos tem mais linhas do que cabe na janela. O usuario roda a roda do mouse sobre o conteudo:
// scroll_ muda o deslocamento, o layout sobe os controles e so a area de conteudo aparece (recorte).
//
// Como funciona:
//   draw:        monta a faixa de abas e, na aba Graficos, os controles de cima para baixo com a rolagem atual;
//                sincroniza cada elemento com Values e desenha. Fora da aba Graficos, o ImGui desenha o conteudo.
//                As linhas genericas (Values::form) de cada secao aberta vem depois do conteudo fixo.
//   handleEvent: clique, arrasto da barra e roda. Clique num combo aberto fecha os outros. Texto e teclas vao ao
//                campo de texto com foco. Pedidos vao para pending_.

#include "i18n.hpp"
#include "framed_window.hpp"
#include "settings_bridge.hpp"
#include "settings_changes.hpp"
#include "settings_form.hpp"
#include "settings_model.hpp"
#include "sui_panel.hpp"
#include "ui_style.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/RadioButton.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/TabBar.hpp"
#include "SpriteUi/Widgets/ScrollBar.hpp"
#include "SpriteUi/Widgets/TextEdit.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace editor::settings_panel {

namespace sb = editor::settings_bridge;
namespace sc = editor::settings_changes;
namespace sm = editor::settings_model;
namespace SU = ToolEngine::SpriteUi;
namespace Elm = SU::Elements;
namespace W = SU::Widgets;
namespace SR = SU::Rendering;
namespace ST = SU::Text;

inline constexpr int kGraphicsTab = 1;   // indice da secao API/Graphics em kTabNames
inline constexpr std::size_t kAppearanceTab = 0;   // indice da secao Appearance em kTabNames

// Valores que a janela mostra. O main.cpp copia do Editor a cada quadro.
struct Values {
    int tab = kGraphicsTab;
    // Linhas extra de cada secao (settings_form.hpp), desenhadas depois dos controles fixos da secao.
    // Lighting, Game, Optimizing e Language usam so estas linhas; API/Graphics recebe Physics, Tags e Project.
    std::array<editor::settings_form::Rows, sm::kTabCount> form{};
    int api = 0;
    std::array<bool, sb::kApiCount> apiBuilt{};
    std::string apiStatus;
    int resMode = 0;
    int customW = 1280;
    int customH = 720;
    bool fixedSize = false;   // o Game tem tamanho fixo (mostra a linha "renders at")
    int fixedW = 0;
    int fixedH = 0;
    float renderScale = 1.f;
    int upscale = 0;
    int aa = 0;
    bool pixelFilter = false;
    int fps = 0;
    int palette = 0;          // indice da paleta em uso (Configuracoes > Appearance)
    float buttonRadius = 3.f; // raio dos botoes (px)
    float windowRadius = 6.f; // raio dos cantos das janelas (px)
    float textScale = 1.f;    // escala do texto da interface (1 = 100%)
    bool shadows = false;
    int shadowQuality = 0;   // indice (0 a 3), ja convertido pelo main.cpp
    bool rayTracing = true;
    bool rtAo = false;
    std::string adapter;
    std::string pixelsLine;
    std::string gpuErrorLine;
    std::string gpuStatsLine;
};

struct Metrics {
    static constexpr float kStripH = 0.f;   // faixa de abas removida: o conteudo comeca no topo do painel
    static constexpr float kHeaderH = 30.f;   // cabecalho de cada secao (recolhivel)
    static constexpr float kPad = sui::kPanelPadding;
    static constexpr float kStripArrowW = 24.f;
    static constexpr float kLabelW = 190.f;
    static constexpr float kControlW = 260.f;
    static constexpr float kRowH = 26.f;
    static constexpr float kRadioRowH = 22.f;
    static constexpr float kHeadingH = 26.f;
    static constexpr float kTextH = 18.f;
    static constexpr float kGap = 4.f;
    static constexpr float kFont = 12.f;
    static constexpr float kSmallFont = 11.f;
    static constexpr float kStepW = 26.f;     // botoes - e + dos tamanhos personalizados
    static constexpr float kValueW = 56.f;    // espaco do valor ao lado do deslizante
    static constexpr float kScrollW = 8.f;    // largura da barra de rolagem do conteudo
    static constexpr float kWheelStep = 40.f; // pixels por tique da roda do mouse
    static constexpr int kCustomStep = 16;    // passo do ImGui (InputInt) para largura e altura
    static constexpr float kPopupTextZ = 0.97f;
    static constexpr float kPopupZ = 0.95f;
    static constexpr float kControlZ = 0.3f;
};

// Recortes do painel (indices em clips): janela inteira, area de conteudo e faixa de abas.
inline constexpr std::uint32_t kWindowClip = 0u;
inline constexpr std::uint32_t kContentClip = 1u;
inline constexpr std::uint32_t kStripClip = 2u;

class SettingsPanel {
 public:
    SettingsPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
        : strip_{text}, canvas_{text}, overlay_{text}, sdl_{sdl}, chrome_{text, sdl} {
        overlay_.setTextZ(Metrics::kPopupTextZ);
    }
    SettingsPanel(const SettingsPanel&) = delete;
    // Moldura SpriteUI (titulo, X, "?", grip e acoplamento). O corpo e a janela ImGui posicionada sobre ela.
    framed::Chrome& chrome() { return chrome_; }
    SettingsPanel& operator=(const SettingsPanel&) = delete;

    // true quando o evento foi da janela (faixa de abas ou, na aba Graficos, o conteudo). Texto e teclas vao so ao
    // campo de texto com foco (linhas Text de settings_form.hpp).
    bool handleEvent(const SDL_Event& e) {
        if (chrome_.handleEvent(e)) return true;
        if (!visible_) return false;
        switch (e.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button != SDL_BUTTON_LEFT) return inside(e.button.x, e.button.y);
                window_ = SDL_GetWindowFromID(e.button.windowID);   // janela do clique: liga e desliga o texto do SDL
                return pointer(Elm::PointerPhase::Down, e.button.x, e.button.y);
            case SDL_EVENT_TEXT_INPUT:
                if (FormSlot* s = focusedSlot()) {
                    if (e.text.text) s->edit.InsertText(e.text.text);
                    return true;
                }
                return false;
            case SDL_EVENT_KEY_DOWN:
                return textKey(e.key.key);
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (bar_.Dragging()) {
                    bar_.PointerUp();
                    return true;
                }
                if (e.button.button != SDL_BUTTON_LEFT) return inside(e.button.x, e.button.y);
                return pointer(Elm::PointerPhase::Up, e.button.x, e.button.y);
            case SDL_EVENT_MOUSE_MOTION:
                if (bar_.Dragging()) return dragScrollbar(e.motion.x, e.motion.y);
                return pointer(Elm::PointerPhase::Move, e.motion.x, e.motion.y);
            case SDL_EVENT_MOUSE_WHEEL:
                return wheel(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
            default:
                return false;
        }
    }

    // Desenha a janela na area `rect` (coordenadas da janela). Os pedidos saem em `out`.
    bool draw(SDL_Renderer* renderer, sui::Box rect, bool visible, const Values& v, sc::Requests& out) {
        rect_ = rect;
        visible_ = visible;
        if (!visible) return true;
        out.insert(out.end(), pending_.begin(), pending_.end());
        pending_.clear();
        strip_.clear();
        canvas_.clear();
        overlay_.clear();
        steps_.clear();
        if (v.tab != revealedTab_) {   // abrir uma secao pela janela de Configuracoes a deixa aberta
            revealedTab_ = v.tab;
            open_[static_cast<std::size_t>(v.tab)] = true;
        }
        refreshColors();
        strip_.setFontScale(editor::ui_style::textScale());
        canvas_.setFontScale(editor::ui_style::textScale());
        overlay_.setFontScale(editor::ui_style::textScale());
        dropdownStyle_.cornerRadius = Metrics::kRowH * 0.5f;   // campos de selecao sao redondos (pilula), como o resto da UI
        // Mede o conteudo, prende o deslocamento ao tamanho real e refaz o layout uma vez com ele.
        layoutSections(v);
        syncScroll();
        canvas_.clear();
        layoutSections(v);
        syncElements(v);
        layoutDropdowns();
        // Textos dos controles so existem com a secao aberta: recolhida, os rotulos antigos sobrariam por cima.
        if (open_[kGraphicsTab]) {
            dropdownTexts();
            radioTexts();
        }
        drawScrollbar();

        const std::vector<SR::ClipRectGPU> clips{
            {rect_.x, rect_.y, rect_.x + rect_.w, rect_.y + rect_.h},
            {rect_.x, rect_.y + Metrics::kStripH, rect_.x + rect_.w, rect_.y + rect_.h},
            {rect_.x, rect_.y, rect_.x + rect_.w, rect_.y + Metrics::kStripH}};
        const SU::UIHandle owner = sui::handleFor(0);
        std::vector<SR::UIRenderItem> items;
        std::uint64_t paint = 1;
        const float bottom = rect_.y + rect_.h;
        items.push_back(sui::Canvas::solidItem(rect_.x, rect_.y, rect_.w, rect_.h, panelColor_, 0.f, owner, kContentClip, paint++));
        canvas_.appendItems(items, rect_.x, rect_.y, 0.f, rect_.y + Metrics::kStripH, bottom, owner, kContentClip, paint);
        if (open_[kGraphicsTab]) appendControls(items, paint);
        if (open_[kAppearanceTab]) appendAppearance(items, paint);
        appendForm(items, paint);
        overlay_.appendItems(items, rect_.x, rect_.y, 0.f, rect_.y, bottom, owner, kWindowClip, paint);
        appendPopups(items, paint);

        packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
        if (!SR::IsFramePacketValid(packet_)) return true;
        return sdl_.draw(renderer, packet_);
    }

 private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    struct Stepper {
        sc::Field field;
        sui::Box minus;
        sui::Box plus;
        int value;
        int minValue;
        int maxValue;
    };

    struct DropdownSlot {
        Elm::Dropdown* dd;
        sc::Field field;
    };

    // ---------- Faixa de abas ----------
    void layoutStrip() {
        tabs_.border = rectOf(Metrics::kStripArrowW, 0.f, stripViewportWidth(), Metrics::kStripH);
        strip_.addSolid(0.f, 0.f, rect_.w, Metrics::kStripH, panelColor_, 0.1f);
        for (std::size_t i = 0; i < tabs_.control.labels.size(); ++i) {
            const SU::Rect r = Elm::TabRect(tabs_, i);
            const float labelW = strip_.layout(tabs_.control.labels[i], Metrics::kFont).size.x;
            strip_.addText(r.position.x + (r.size.x - labelW) * 0.5f, r.position.y + 7.f, tabs_.control.labels[i], Metrics::kFont,
                           theme_.text, r.size.x);
        }
        const bool canScrollLeft = tabs_.scrollX > 0.f;
        const bool canScrollRight = tabs_.scrollX < stripMaxScroll();
        strip_.addSolid(0.f, 0.f, Metrics::kStripArrowW, Metrics::kStripH, btnColor_, 0.3f);
        strip_.addSolid(rect_.w - Metrics::kStripArrowW, 0.f, Metrics::kStripArrowW, Metrics::kStripH, btnColor_, 0.3f);
        strip_.addText(8.f, 7.f, "<", Metrics::kFont, canScrollLeft ? theme_.text : theme_.textDim, 10.f);
        strip_.addText(rect_.w - Metrics::kStripArrowW + 8.f, 7.f, ">", Metrics::kFont,
                       canScrollRight ? theme_.text : theme_.textDim, 10.f);
    }

    [[nodiscard]] float stripViewportWidth() const { return std::max(0.f, rect_.w - 2.f * Metrics::kStripArrowW); }

    // Largura do conteudo das abas (sem a rolagem da faixa). Cada aba ocupa tabWidth; a folga separa as vizinhas.
    [[nodiscard]] float stripContentWidth() const {
        const float n = static_cast<float>(tabs_.control.labels.size());
        const float tabsW = n > 0.f ? n * tabs_.tabWidth + (n - 1.f) * tabs_.tabGap : 0.f;
        return 2.f * tabs_.stripPadding + tabsW;
    }

    void clampStrip() {
        const float maxX = stripMaxScroll();
        tabs_.scrollX = std::clamp(tabs_.scrollX, 0.f, maxX);
    }

    [[nodiscard]] float stripMaxScroll() const { return std::max(0.f, stripContentWidth() - stripViewportWidth()); }

    // Quando a aba ativa muda, a faixa rola ate ela aparecer. Depois disso, a roda do mouse manda.
    void revealActiveTab() {
        const std::size_t i = tabs_.control.activeIndex;
        if (i == npos) return;
        const float stride = tabs_.tabWidth + tabs_.tabGap;
        const float left = tabs_.stripPadding + static_cast<float>(i) * stride;
        const float right = left + tabs_.tabWidth;
        if (left - tabs_.scrollX < tabs_.stripPadding) {
            tabs_.scrollX = left - tabs_.stripPadding;
        } else if (right - tabs_.scrollX > stripViewportWidth() - tabs_.stripPadding) {
            tabs_.scrollX = right - stripViewportWidth() + tabs_.stripPadding;
        }
        clampStrip();
    }

    void appendTabs(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        append(items, paint, Elm::BuildTabBarItems(tabs_, tabStyle_, kStripClip, 0.2f, paint), kStripClip);
    }

    // ---------- Rolagem da aba Graficos ----------
    // O conteudo guardado vale do quadro anterior (a altura so muda quando os valores mudam); o deslocamento e
    // prendido nessa altura antes do layout.
    void syncScroll() {
        const float viewH = rect_.h - Metrics::kStripH;
        scroll_.viewportSize = {rect_.w, viewH};
        scroll_.contentSize = {rect_.w, contentH_};
        scroll_.ClampScroll();
        bar_.SetTrack(rectOf(rect_.w - Metrics::kScrollW, Metrics::kStripH, Metrics::kScrollW, viewH));
    }

    void drawScrollbar() {
        if (!bar_.HasOverflow(scroll_)) return;
        const SU::Rect track = bar_.Track();
        canvas_.addSolid(track.position.x, track.position.y, track.size.x, track.size.y, trackColor_, 0.2f);
        const SU::Rect thumb = bar_.ThumbRect(scroll_);
        canvas_.addSolid(thumb.position.x, thumb.position.y, thumb.size.x, thumb.size.y, thumbColor_, 0.25f);
    }

    // ---------- Layout da aba Graficos (coordenadas da janela, ja com a rolagem) ----------
    // Cada bloco devolve o y da linha seguinte. contentH_ guarda a altura total (com as margens) para a rolagem.
    // Secoes recolheis, uma embaixo da outra. Cada cabecalho e clicavel; o corpo so existe se a secao esta aberta.
    void layoutSections(const Values& v) {
        labelW_ = std::min(Metrics::kLabelW, rect_.w * 0.5f);
        controlW_ = rect_.w - 2.f * Metrics::kPad - labelW_;
        const float contentTop = Metrics::kStripH + Metrics::kPad;
        const float top = contentTop - scroll_.scrollOffset.y;
        const float headerW = rect_.w - 2.f * Metrics::kPad;
        for (auto& row : paletteRows_) row = sui::Box{};   // linhas de tema so existem com a secao aberta
        appearanceOpen_ = false;
        float y = top;
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            headers_[i] = sui::Box{Metrics::kPad, y, headerW, Metrics::kHeaderH};
            drawHeader(i, y, headerW);
            y += Metrics::kHeaderH + Metrics::kGap;
            if (!open_[i]) continue;
            if (i == kGraphicsTab) {
                y = layoutGraphicsBody(v, y);
            } else if (i == kAppearanceTab) {
                y = layoutAppearance(v, y);
            } else if (v.form[i].empty()) {
                canvas_.addText(Metrics::kPad, y, std::string(T("Not available in this version yet")), Metrics::kFont,
                                theme_.textDim, headerW);
                y += Metrics::kTextH;
            }
            y = layoutForm(i, v.form[i], y);   // linhas genericas da secao, depois do conteudo fixo
            y += 2.f * Metrics::kGap;
        }
        contentH_ = (y - top) + 2.f * Metrics::kPad;
    }

    void drawHeader(std::size_t i, float y, float w) {
        const bool hot = headers_[i].contains(hover_.x, hover_.y);
        canvas_.addSolid(Metrics::kPad, y, w, Metrics::kHeaderH, hot ? theme_.accent : theme_.surface, 0.2f,
                         editor::ui_style::buttonRadius());
        canvas_.addText(Metrics::kPad + 10.f, y + 7.f, std::string(T(sm::kTabNames[i])), Metrics::kFont + 1.f, theme_.text,
                        w - 56.f);
        // Botao redondo de 22 px com o sinal de recolher (-) ou expandir (+): visivel e facil de acertar.
        const float cx = Metrics::kPad + w - 17.f, cy = y + Metrics::kHeaderH * 0.5f;
        canvas_.addSolid(cx - 11.f, cy - 11.f, 22.f, 22.f, hot ? theme_.text : theme_.surface, 0.24f, 11.f);
        const SU::Vec4 sign = hot ? theme_.surface : theme_.text;
        canvas_.addSolid(cx - 6.f, cy - 1.5f, 12.f, 3.f, sign, 0.25f);                              // "-"
        if (!open_[i]) canvas_.addSolid(cx - 1.5f, cy - 6.f, 3.f, 12.f, sign, 0.25f);             // "+" quando fechada
    }

    // Corpo da secao Settings (graficos), a partir de y. Devolve o y final.
    float layoutGraphicsBody(const Values& v, float y) {
        text(Metrics::kPad, y, v.apiStatus, Metrics::kFont, textWidth());
        y += Metrics::kTextH + 6.f;
        y = layoutApis(v, y);
        y = layoutQuality(v, y);
        y = layoutGpu(v, y);
        return y;
    }

    // Corpo da secao Appearance: escolha da paleta (a linha marcada e a ativa).
    float layoutAppearance(const Values& v, float y) {
        canvas_.addText(Metrics::kPad, y, std::string(T("Theme")), Metrics::kFont, theme_.textDim, textWidth());
        y += Metrics::kTextH + 6.f;
        const auto& presets = editor::palette::kPresets;
        for (std::size_t k = 0; k < editor::palette::kCount; ++k) {
            paletteRows_[k] = sui::Box{Metrics::kPad, y, textWidth(), Metrics::kRowH};
            const bool selected = static_cast<int>(k) == v.palette;
            canvas_.addSolid(paletteRows_[k].x, y, paletteRows_[k].w, Metrics::kRowH,
                             selected ? theme_.accent : theme_.surface, 0.2f, editor::ui_style::buttonRadius());
            canvas_.addText(Metrics::kPad + 10.f, y + 5.f, std::string(T(presets[k].name)), Metrics::kFont, theme_.text,
                            textWidth() - 20.f);
            y += Metrics::kRowH + Metrics::kGap;
        }
        y += Metrics::kGap;
        appearanceOpen_ = true;
        y = styleRow(buttonRadius_, v.buttonRadius, "Button corner radius", y, 0.f, sm::kButtonRadiusMax, 1.f, true,
                     std::to_string(static_cast<int>(v.buttonRadius + 0.5f)) + " px");
        y = styleRow(windowRadius_, v.windowRadius, "Window corner radius", y, 0.f, sm::kWindowRadiusMax, 1.f, true,
                     std::to_string(static_cast<int>(v.windowRadius + 0.5f)) + " px");
        y = styleRow(textScale_, v.textScale, "Interface text size", y, sm::kTextScaleMin, sm::kTextScaleMax, 0.05f, false,
                     std::to_string(static_cast<int>(v.textScale * 100.f + 0.5f)) + "%");
        return y;
    }

    // Um slider com rotulo e valor na linha: a faixa do controle fica em `s.border` (coordenadas locais).
    float styleRow(Elm::Slider& s, float value, const char* label, float y, float lo, float hi, float step, bool integer,
                   const std::string& shown) {
        canvas_.addText(Metrics::kPad, y + 5.f, std::string(T(label)), Metrics::kFont, theme_.text, labelW_ - 8.f);
        s.range = Elm::SliderRange{lo, hi, step, integer};
        s.SetValue(value);
        s.border = rectOf(Metrics::kPad + labelW_, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
        canvas_.addText(Metrics::kPad + labelW_ + controlW_ - Metrics::kValueW + 6.f, y + 5.f, shown, Metrics::kFont,
                        theme_.textDim, Metrics::kValueW - 6.f);
        return y + Metrics::kRowH + Metrics::kGap;
    }

    void appendAppearance(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        constexpr float z = Metrics::kControlZ;
        append(items, paint, Elm::BuildSliderItems(buttonRadius_, sliderStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildSliderItems(windowRadius_, sliderStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildSliderItems(textScale_, sliderStyle_, z, paint), kContentClip);
    }

    // Arrasto dos sliders de Appearance: cada valor vai pelo mesmo caminho dos outros campos (pedido -> main.cpp).
    bool appearanceSliderPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        if (!appearanceOpen_) return false;
        struct Slide {
            Elm::Slider* slider;
            sc::Field field;
        };
        const Slide slides[] = {{&buttonRadius_, sc::Field::ButtonRadius},
                                {&windowRadius_, sc::Field::WindowRadius},
                                {&textScale_, sc::Field::TextScale}};
        for (const Slide& s : slides) {
            const Elm::SliderEvents ev = s.slider->OnPointer(event(phase, p, s.slider->border));
            if (ev.valueChanged) pending_.push_back({s.field, 0, s.slider->Value(), false});
            if (ev.pressStarted || ev.valueChanged || ev.pressEnded) return true;
        }
        return false;
    }

    // Tema lido a cada quadro: trocar a paleta muda esta janela na hora.
    void refreshColors() {
        theme_ = sui::Theme{};
        panelColor_ = theme_.panel;
        headingColor_ = theme_.scroll;
    }

    float layoutApis(const Values& v, float y) {
        heading(y, T("Graphics API"));
        y += Metrics::kHeadingH;
        const float radioH = static_cast<float>(builtApis(v)) * Metrics::kRadioRowH;
        apis_.border = rectOf(Metrics::kPad + labelW_, y, controlW_, radioH);
        return y + radioH + 6.f;
    }

    float layoutQuality(const Values& v, float y) {
        heading(y, T("Quality"));
        y += Metrics::kHeadingH;
        controlRow(resolution_, y, T("Game resolution"));
        y += Metrics::kRowH + Metrics::kGap;
        if (v.resMode == sm::kResolutionCustom) {
            stepper(y, T("Width"), sc::Field::CustomW, v.customW, sm::kCustomWidthMin, sm::kCustomWidthMax);
            y += Metrics::kRowH + Metrics::kGap;
            stepper(y, T("Height"), sc::Field::CustomH, v.customH, sm::kCustomHeightMin, sm::kCustomHeightMax);
            y += Metrics::kRowH + Metrics::kGap;
        }
        scale_.border = rectOf(Metrics::kPad + labelW_, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
        text(Metrics::kPad, y + 6.f, T("Render scale"), Metrics::kFont, labelW_ - 6.f);
        y += Metrics::kRowH + Metrics::kGap;
        text(Metrics::kPad, y, T("Below 1.0 renders fewer pixels (faster); above 1.0 renders more pixels (sharper, slower)."),
             Metrics::kSmallFont, textWidth());
        y += Metrics::kTextH + Metrics::kGap;
        controlRow(upscale_, y, T("Upscaling (software)"));
        y += Metrics::kRowH + Metrics::kGap;
        controlRow(aa_, y, T("Anti-aliasing (software)"));
        y += Metrics::kRowH + Metrics::kGap;
        checkRow(pixel_, y, T("Sharp pixels when enlarging (no smoothing)"));
        y += Metrics::kRowH + Metrics::kGap;
        controlRow(fps_, y, T("Frame rate limit"));
        y += Metrics::kRowH + Metrics::kGap;
        if (v.fixedSize) {
            char line[96];
            std::snprintf(line, sizeof line, T("Game renders at %d x %d."), v.fixedW, v.fixedH);
            text(Metrics::kPad, y, line, Metrics::kFont, textWidth());
            y += Metrics::kTextH + 6.f;
        }
        return y;
    }

    float layoutGpu(const Values& v, float y) {
        heading(y, T("GPU details"));
        y += Metrics::kHeadingH;
        checkRow(shadows_, y, T("Enable shadows"));
        y += Metrics::kRowH + Metrics::kGap;
        controlRow(shadowQuality_, y, T("Shadow quality"));
        y += Metrics::kRowH + Metrics::kGap;
        checkRow(rayTracing_, y, T("Enable ray tracing"));
        y += Metrics::kRowH + Metrics::kGap;
        checkRow(rtAo_, y, T("Ray-traced ambient occlusion (CPU)"));
        y += Metrics::kRowH + Metrics::kGap + 4.f;
        for (const std::string* line : {&v.adapter, &v.pixelsLine, &v.gpuErrorLine, &v.gpuStatsLine}) {
            text(Metrics::kPad, y, *line, Metrics::kFont, textWidth());
            y += Metrics::kTextH + 2.f;
        }
        text(Metrics::kPad, y, T("Tip: lower render scale and shadow quality first when you need more FPS."), Metrics::kSmallFont,
             textWidth());
        return y + Metrics::kTextH;
    }

    [[nodiscard]] float textWidth() const { return rect_.w - 2.f * Metrics::kPad; }

    void controlRow(Elm::Dropdown& dd, float y, const char* label) {
        dd.border = rectOf(Metrics::kPad + labelW_, y, controlW_, Metrics::kRowH);
        text(Metrics::kPad, y + 6.f, label, Metrics::kFont, labelW_ - 6.f);
    }
    void checkRow(Elm::Checkbox& cb, float y, const char* label) {
        cb.border = rectOf(Metrics::kPad + labelW_, y, controlW_, Metrics::kRowH);
        text(Metrics::kPad, y + 6.f, label, Metrics::kFont, labelW_ - 6.f);
    }

    // Tamanho personalizado: rotulo, valor e os botoes - e +. Cada clique muda kCustomStep (limites do modelo).
    void stepper(float y, const char* label, sc::Field field, int value, int minValue, int maxValue) {
        const float x = Metrics::kPad + labelW_;
        const float plusX = x + controlW_ - Metrics::kStepW;
        const float minusX = plusX - Metrics::kStepW - Metrics::kGap;
        text(Metrics::kPad, y + 6.f, label, Metrics::kFont, labelW_ - 6.f);
        text(x, y + 6.f, std::to_string(value), Metrics::kFont, minusX - x - Metrics::kGap);
        steps_.push_back({field, {minusX, y, Metrics::kStepW, Metrics::kRowH}, {plusX, y, Metrics::kStepW, Metrics::kRowH}, value,
                          minValue, maxValue});
        const Stepper& s = steps_.back();
        canvas_.addSolid(s.minus.x, s.minus.y, s.minus.w, s.minus.h, btnColor_, 0.2f, editor::ui_style::buttonRadius());
        canvas_.addSolid(s.plus.x, s.plus.y, s.plus.w, s.plus.h, btnColor_, 0.2f, editor::ui_style::buttonRadius());
        text(s.minus.x + 9.f, s.minus.y + 6.f, "-", Metrics::kFont, 12.f);
        text(s.plus.x + 8.f, s.plus.y + 6.f, "+", Metrics::kFont, 12.f);
    }

    static int builtApis(const Values& v) {
        int n = 0;
        for (bool built : v.apiBuilt) n += built ? 1 : 0;
        return n;
    }

    void heading(float y, const char* title) {
        canvas_.addText(Metrics::kPad, y + 4.f, title, Metrics::kFont + 1.f, headingColor_, textWidth());
    }

    void text(float x, float y, const std::string& s, float size, float maxW) {
        if (!s.empty()) canvas_.addText(x, y, s, size, theme_.text, maxW);
    }

    [[nodiscard]] static SU::Rect rectOf(float x, float y, float w, float h) {
        return {{x, y}, {w, h}};
    }

    [[nodiscard]] SU::Rect windowRect() const { return rectOf(0.f, 0.f, rect_.w, rect_.h); }

    // ---------- Textos dos elementos ----------
    // Texto do valor escolhido dentro de cada combo (a caixa do combo ja foi desenhada pelo elemento).
    void dropdownTexts() {
        dropdownValue(resolution_);
        dropdownValue(upscale_);
        dropdownValue(aa_);
        dropdownValue(fps_);
        dropdownValue(shadowQuality_);
    }

    void dropdownValue(const Elm::Dropdown& dd) {
        const std::size_t sel = dd.control.selectedIndex;
        if (sel >= dd.control.options.size()) return;
        const SU::Rect r = dd.border;
        text(r.position.x + 8.f, r.position.y + 6.f, dd.control.options[sel], Metrics::kFont, r.size.x - 24.f);
    }

    // Rotulo de cada opcao do radio (o elemento so desenha o circulo).
    void radioTexts() {
        for (std::size_t i = 0; i < apis_.options.size(); ++i) {
            const SU::Rect r = Elm::RadioLabelRect(apis_, radioStyle_, i);
            text(r.position.x, r.position.y + 2.f, apis_.options[i].label, Metrics::kFont, r.size.x);
        }
    }

    // Textos das opcoes dos combos abertos, em overlay (por cima dos controles).
    void dropdownPopupTexts() {
        for (const Elm::Dropdown* dd : allDropdowns()) popupTexts(*dd);
        forEachFormSlot([&](FormSlot& s) {
            if (s.kind == settings_form::RowKind::Dropdown) popupTexts(s.dropdown);
        });
    }

    void popupTexts(const Elm::Dropdown& dd) {
        if (!dd.control.open) return;
        for (std::size_t i = 0; i < dd.control.options.size(); ++i) {
            const SU::Rect r = Elm::DropdownRowRect(dd, i);
            overlay_.addText(r.position.x + 8.f, r.position.y + 6.f, dd.control.options[i], Metrics::kFont, theme_.text,
                             r.size.x - 16.f);
        }
    }

    [[nodiscard]] std::array<const Elm::Dropdown*, 5> allDropdowns() const {
        return {&resolution_, &upscale_, &aa_, &fps_, &shadowQuality_};
    }
    [[nodiscard]] std::array<Elm::Dropdown*, 5> allDropdownsMutable() {
        return {&resolution_, &upscale_, &aa_, &fps_, &shadowQuality_};
    }

    void layoutDropdowns() {
        for (Elm::Dropdown* dd : allDropdownsMutable()) dd->Layout(windowRect());
        forEachFormSlot([&](FormSlot& s) {
            if (s.kind == settings_form::RowKind::Dropdown) s.dropdown.Layout(windowRect());
        });
        dropdownPopupTexts();
    }

    // ---------- Sincronia dos elementos com os valores (ponte) ----------
    // Abas: nomes traduzidos (T) na ordem da barra. A faixa rola ate a aba ativa quando ela muda.
    void syncTabs(const Values& v) {
        sb::SyncTabs(tabs_, v.tab);
        for (std::size_t i = 0; i < tabs_.control.labels.size(); ++i) tabs_.control.labels[i] = T(sm::kTabNames[i]);
        if (v.tab != revealedTab_) {
            revealedTab_ = v.tab;
            revealActiveTab();
        }
        clampStrip();
    }

    void syncElements(const Values& v) {
        sb::SyncApiRadio(apis_, v.apiBuilt, v.api);
        sb::SyncDropdown(resolution_, resolutionOptions(), v.resMode);
        sb::SyncDropdown(upscale_, {T("Off (SDL linear)"), T("Bilinear"), T("Bicubic (Catmull-Rom)"), T("Lanczos2")}, v.upscale);
        sb::SyncDropdown(aa_, {T("Off"), T("FXAA")}, v.aa);
        sb::SyncDropdown(fps_, {T("VSync"), T("30 FPS"), T("60 FPS"), T("120 FPS"), T("Unlimited")}, v.fps);
        sb::SyncDropdown(shadowQuality_, {T("Low (512)"), T("Medium (1024)"), T("High (2048)"), T("Ultra (4096)")}, v.shadowQuality);
        sb::SyncRenderScale(scale_, v.renderScale);
        sb::SyncCheckbox(pixel_, v.pixelFilter);
        sb::SyncCheckbox(shadows_, v.shadows);
        sb::SyncCheckbox(rayTracing_, v.rayTracing);
        sb::SyncCheckbox(rtAo_, v.rayTracing && v.rtAo);
        rayTracingEnabled_ = v.rayTracing;
        scaleEnabled_ = v.resMode == 0;   // a escala so vale com a resolucao de janela (igual ao ImGui)
    }

    static std::vector<std::string> resolutionOptions() {
        return {T("Match window size (x render scale)"), "640 x 360", "1280 x 720 (HD)", "1920 x 1080 (Full HD)",
                "2560 x 1440 (QHD)", T("Custom")};
    }

    // ---------- Linhas genericas (settings_form.hpp) ----------
    // Cada secao tem um pool de controles, um por linha, criado de novo quando o numero de linhas muda. Os controles
    // guardam estado entre quadros (arraste, combo aberto, foco do texto). O tipo da linha escolhe o controle usado.
    struct FormSlot {
        settings_form::RowKind kind = settings_form::RowKind::Gap;
        int id = 0;                  // id da linha, copiado para o pedido
        Elm::Slider slider;
        Elm::Checkbox check;
        Elm::Dropdown dropdown;
        Elm::RadioGroup radio;
        Elm::Button button;
        W::TextEditState edit;       // Text: edicao (texto e caret). Sem foco, espelha o valor da janela
        std::string known;           // Text: ultimo valor recebido; o pedido so sai se o texto mudou
        SU::Rect box;                // Text: o campo; Check: a linha inteira (hover e clique)
    };

    // Linhas de uma secao, depois do conteudo fixo. Devolve o y final.
    float layoutForm(std::size_t i, const settings_form::Rows& rows, float y) {
        syncFormPool(i, rows);
        for (std::size_t k = 0; k < rows.size(); ++k) y = layoutFormRow(i, k, rows[k], y);
        return y;
    }

    // Pool da secao: recriado quando o numero de linhas muda (o foco do texto dessa secao se perde).
    void syncFormPool(std::size_t i, const settings_form::Rows& rows) {
        auto& pool = formPool_[i];
        if (pool.size() == rows.size()) return;
        if (focusSection_ == i) dropFocus();
        pool.clear();
        for (std::size_t k = 0; k < rows.size(); ++k) pool.push_back(std::make_unique<FormSlot>());
    }

    // Controle da linha k. Se o tipo da linha mudou, o controle e trocado por um novo do tipo certo.
    FormSlot& slotFor(std::size_t i, std::size_t k, settings_form::RowKind kind) {
        std::unique_ptr<FormSlot>& p = formPool_[i][k];
        if (p->kind != kind) {
            if (isFocused(i, k)) dropFocus();
            p = std::make_unique<FormSlot>();
            p->kind = kind;
        }
        return *p;
    }

    float layoutFormRow(std::size_t i, std::size_t k, const settings_form::Row& row, float y) {
        using settings_form::RowKind;
        FormSlot& s = slotFor(i, k, row.kind);
        s.id = row.id;
        const float x = Metrics::kPad + labelW_;
        const SU::Vec4 ink = row.enabled ? theme_.text : theme_.textDim;
        switch (row.kind) {
            case RowKind::Heading:   // o titulo usa a cor e o tamanho dos titulos (a fonte nao tem negrito)
                canvas_.addText(Metrics::kPad, y + 4.f, row.label, Metrics::kFont + 1.f, headingColor_, textWidth());
                return y + Metrics::kHeadingH;
            case RowKind::Note: {
                if (!row.label.empty()) y += canvas_.addParagraph(Metrics::kPad, y, row.label, Metrics::kFont, ink, textWidth()) + Metrics::kGap;
                if (!row.note.empty()) y += canvas_.addParagraph(Metrics::kPad, y, row.note, Metrics::kSmallFont, theme_.textDim, textWidth()) + Metrics::kGap;
                return y;
            }
            case RowKind::Gap:
                return y + Metrics::kRowH * 0.5f;
            case RowKind::Button: {
                s.button.states.disabled = !row.enabled;
                s.button.cornerRadius = editor::ui_style::buttonRadius();
                s.button.border = rectOf(x, y, controlW_, Metrics::kRowH);
                const float tw = canvas_.layout(row.label, Metrics::kFont).size.x;
                canvas_.addText(x + (controlW_ - tw) * 0.5f, y + 6.f, row.label, Metrics::kFont, ink, controlW_);
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Text: {
                const bool focused = isFocused(i, k);
                s.box = rectOf(x, y, controlW_, Metrics::kRowH);
                // Sem foco, o campo mostra o valor da janela. So recarrega quando ele mudou la (row.text diferente do
                // ultimo recebido), para o texto confirmado nao voltar ao valor antigo enquanto o pedido ainda vai.
                if (!focused && row.text != s.known) s.edit = W::TextEditState(row.text);
                s.known = row.text;
                const std::string& shown = s.edit.Text();
                canvas_.addText(Metrics::kPad, y + 6.f, row.label, Metrics::kFont, ink, labelW_ - 6.f);
                canvas_.addSolid(s.box.position.x, s.box.position.y, s.box.size.x, s.box.size.y, focused ? theme_.surface : btnColor_,
                                 0.2f, editor::ui_style::buttonRadius());
                canvas_.addText(s.box.position.x + 8.f, y + 6.f, shown, Metrics::kFont, ink, s.box.size.x - 16.f);
                if (focused) {   // caret no fim do texto ate o caret (medido com a fonte do canvas)
                    const float cx = s.box.position.x + 8.f + canvas_.layout(shown.substr(0, s.edit.Caret()), Metrics::kFont).size.x;
                    canvas_.addSolid(cx, y + 4.f, 1.5f, Metrics::kRowH - 8.f, theme_.text, 0.25f);
                }
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Slider: {
                const bool asInt = row.isInt;
                s.slider.range = Elm::SliderRange{row.lo, row.hi, asInt ? 1.f : 0.f, asInt};
                s.slider.SetValue(settings_form::ClampSliderValue(row.value, row.lo, row.hi, asInt));
                s.slider.states.disabled = !row.enabled;
                s.slider.border = rectOf(x, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
                canvas_.addText(Metrics::kPad, y + 5.f, row.label, Metrics::kFont, ink, labelW_ - 8.f);
                const std::string shown = row.note.empty() ? Elm::SliderValueText(s.slider) : row.note;
                canvas_.addText(x + controlW_ - Metrics::kValueW + 6.f, y + 5.f, shown, Metrics::kFont, theme_.textDim,
                                Metrics::kValueW - 6.f);
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Check: {
                s.check.checked = row.on;
                s.check.states.disabled = !row.enabled;
                s.box = rectOf(Metrics::kPad, y, textWidth(), Metrics::kRowH);
                s.check.border = rectOf(x, y, controlW_, Metrics::kRowH);
                if (row.enabled && contains(s.box, hover_)) {   // hover: a linha inteira pinta com o destaque do tema
                    canvas_.addSolid(s.box.position.x, s.box.position.y, s.box.size.x, s.box.size.y, theme_.accent, 0.2f,
                                     editor::ui_style::buttonRadius());
                }
                canvas_.addText(Metrics::kPad, y + 6.f, row.label, Metrics::kFont, ink, labelW_ - 6.f);
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Dropdown: {
                sb::SyncDropdown(s.dropdown, row.options, row.index);
                s.dropdown.control.disabled = !row.enabled;
                s.dropdown.states.disabled = !row.enabled;
                s.dropdown.border = rectOf(x, y, controlW_, Metrics::kRowH);
                canvas_.addText(Metrics::kPad, y + 6.f, row.label, Metrics::kFont, ink, labelW_ - 6.f);
                dropdownValue(s.dropdown);
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Radio: {
                Elm::RadioGroup& g = s.radio;
                g.rowHeight = Metrics::kRadioRowH;
                g.states.disabled = !row.enabled;
                g.options.clear();
                for (std::size_t j = 0; j < row.options.size(); ++j) {
                    (void)g.AddOption(static_cast<Elm::RadioOptionId>(j), row.options[j]);
                }
                const bool asked = row.index >= 0 && row.index < static_cast<int>(row.options.size());
                g.selectedId = asked ? static_cast<Elm::RadioOptionId>(row.index) : Elm::NoRadioOption;
                const float h = static_cast<float>(row.options.size()) * Metrics::kRadioRowH;
                g.border = rectOf(x, y, controlW_, h);
                canvas_.addText(Metrics::kPad, y + 2.f, row.label, Metrics::kFont, ink, labelW_ - 6.f);
                for (std::size_t j = 0; j < g.options.size(); ++j) {
                    const SU::Rect r = Elm::RadioLabelRect(g, radioStyle_, j);
                    canvas_.addText(r.position.x, r.position.y + 2.f, g.options[j].label, Metrics::kFont, ink, r.size.x);
                }
                return y + h + Metrics::kGap;
            }
        }
        return y;
    }

    [[nodiscard]] bool isFocused(std::size_t i, std::size_t k) const { return focusSection_ == i && focusIndex_ == k; }

    // Campo de texto com foco, ou nullptr.
    [[nodiscard]] FormSlot* focusedSlot() {
        if (focusSection_ == npos || focusIndex_ >= formPool_[focusSection_].size()) return nullptr;
        return formPool_[focusSection_][focusIndex_].get();
    }

    // Foco no campo de texto: o caret vai para o fim e o SDL passa a mandar o texto digitado.
    void focusText(std::size_t i, std::size_t k, FormSlot& s) {
        if (isFocused(i, k)) return;
        commitText();   // outro campo com foco: confirma antes de trocar
        focusSection_ = i;
        focusIndex_ = k;
        s.edit.MoveEnd();
        if (window_) SDL_StartTextInput(window_);
    }

    // Solta o foco sem confirmar. O texto volta ao valor da janela na proxima sincronia.
    void dropFocus() {
        if (focusSection_ == npos) return;
        focusSection_ = npos;
        focusIndex_ = npos;
        if (window_) SDL_StopTextInput(window_);
    }

    // Confirma o texto em edicao: manda o pedido so se o valor mudou, e solta o foco.
    void commitText() {
        FormSlot* s = focusedSlot();
        if (!s) return;
        if (s->edit.Text() != s->known) pending_.push_back(formChange(s->id, 0.f, false, 0, std::string(s->edit.Text())));
        dropFocus();
    }

    // Clique fora do campo com foco confirma o texto.
    void blurOutside(const SU::Vec2& p) {
        const FormSlot* s = focusedSlot();
        if (s && !contains(s->box, p)) commitText();
    }

    // Tecla com foco no campo de texto: Enter confirma, Esc desfaz, as setas e apagar editam. Sem foco, nao consome.
    bool textKey(SDL_Keycode key) {
        FormSlot* s = focusedSlot();
        if (!s) return false;
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: commitText(); break;
            case SDLK_ESCAPE:   // desfaz a edicao: volta ao ultimo valor recebido
                s->edit = W::TextEditState(s->known);
                dropFocus();
                break;
            case SDLK_BACKSPACE: (void)s->edit.Backspace(); break;
            case SDLK_DELETE: (void)s->edit.Delete(); break;
            case SDLK_LEFT: s->edit.MoveLeft(); break;
            case SDLK_RIGHT: s->edit.MoveRight(); break;
            case SDLK_HOME: s->edit.MoveHome(); break;
            case SDLK_END: s->edit.MoveEnd(); break;
            default: break;
        }
        return true;
    }

    // Pedido de uma linha generica: o id da linha e o campo do tipo (value, on, index ou text).
    [[nodiscard]] static sc::Change formChange(int id, float value, bool on, int index, std::string text) {
        return sc::Change{sc::Field::Form, index, value, on, id, std::move(text)};
    }

    template <class F>
    void forEachFormSlot(F&& f) {
        for (auto& pool : formPool_) {
            for (auto& s : pool) f(*s);
        }
    }

    [[nodiscard]] bool graphicsDropdownOpen() const {
        for (const Elm::Dropdown* dd : allDropdowns()) {
            if (dd->control.open) return true;
        }
        return false;
    }

    [[nodiscard]] FormSlot* openFormDropdown() {
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            if (!open_[i]) continue;
            for (auto& s : formPool_[i]) {
                if (s->kind == settings_form::RowKind::Dropdown && s->dropdown.control.open) return s.get();
            }
        }
        return nullptr;
    }

    // Combo de linha: mesma regra dos combos da aba Graficos. Devolve true se o evento foi dele.
    bool formDropdownPointer(FormSlot& s, Elm::PointerPhase phase, const SU::Vec2& p) {
        const Elm::DropdownEvents ev = dropdownEvents(s.dropdown, phase, p);
        if (ev.opened) closeOthers(&s.dropdown);
        if (ev.valueChanged) {
            pending_.push_back(formChange(s.id, 0.f, false, static_cast<int>(s.dropdown.control.selectedIndex), ""));
        }
        return ev.opened || ev.closed || ev.valueChanged;
    }

    // Ponteiro nas linhas genericas das secoes abertas. Devolve true se um controle consumiu o evento; movimento sem
    // controle ativo devolve false, para um arraste da aba Graficos continuar recebendo o ponteiro.
    bool formPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        if (graphicsDropdownOpen()) return false;   // a lista da aba Graficos cobre o resto (contentPointer)
        if (FormSlot* open = openFormDropdown()) {  // lista aberta de combo de linha: so ela recebe o ponteiro
            formDropdownPointer(*open, phase, p);
            return true;
        }
        bool used = false;
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            if (!open_[i]) continue;
            for (std::size_t k = 0; k < formPool_[i].size(); ++k) {
                used = formSlotPointer(i, k, *formPool_[i][k], phase, p) || used;
            }
        }
        return used;
    }

    bool formSlotPointer(std::size_t i, std::size_t k, FormSlot& s, Elm::PointerPhase phase, const SU::Vec2& p) {
        using settings_form::RowKind;
        switch (s.kind) {
            case RowKind::Slider: {
                const Elm::SliderEvents ev = s.slider.OnPointer(event(phase, p, s.slider.border));
                if (ev.valueChanged) pending_.push_back(formChange(s.id, s.slider.Value(), false, 0, ""));
                return ev.pressStarted || ev.valueChanged || ev.pressEnded;
            }
            case RowKind::Check: {
                const Elm::CheckboxEvents ev = s.check.OnPointer(event(phase, p, s.check.border));
                if (ev.valueChanged) pending_.push_back(formChange(s.id, 0.f, s.check.checked, 0, ""));
                return ev.pressStarted || ev.valueChanged || ev.pressEnded || ev.clicked;
            }
            case RowKind::Dropdown:
                return formDropdownPointer(s, phase, p);
            case RowKind::Radio: {
                const Elm::RadioEvents ev = s.radio.OnPointer(event(phase, p, s.radio.border));
                if (ev.valueChanged) pending_.push_back(formChange(s.id, 0.f, false, static_cast<int>(s.radio.selectedId), ""));
                return ev.pressStarted || ev.valueChanged || ev.pressEnded || ev.clicked;
            }
            case RowKind::Button: {
                const Elm::ButtonEvents ev = s.button.OnPointer(event(phase, p, s.button.border));
                if (ev.clicked) pending_.push_back(formChange(s.id, 0.f, false, 0, ""));
                return ev.pressStarted || ev.pressEnded || ev.clicked;
            }
            case RowKind::Text:
                if (phase == Elm::PointerPhase::Down && contains(s.box, p)) {
                    focusText(i, k, s);
                    return true;
                }
                return false;
            case RowKind::Heading:
            case RowKind::Note:
            case RowKind::Gap:
                return false;
        }
        return false;
    }

    // Itens dos controles de linha das secoes abertas (o texto sai pelo canvas, em layoutFormRow).
    void appendForm(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        using settings_form::RowKind;
        constexpr float z = Metrics::kControlZ;
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            if (!open_[i]) continue;
            for (auto& sp : formPool_[i]) {
                FormSlot& s = *sp;
                switch (s.kind) {
                    case RowKind::Slider:
                        append(items, paint, Elm::BuildSliderItems(s.slider, sliderStyle_, z, paint), kContentClip);
                        break;
                    case RowKind::Check:
                        append(items, paint, Elm::BuildCheckboxItems(s.check, checkStyle_, z, paint), kContentClip);
                        break;
                    case RowKind::Dropdown: {
                        Elm::DropdownStyle style = dropdownStyle_;
                        if (contains(s.dropdown.border, hover_) && !s.dropdown.control.disabled) style.button = theme_.accent;
                        append(items, paint, Elm::BuildDropdownButtonItems(s.dropdown, style, z, paint), kContentClip);
                        break;
                    }
                    case RowKind::Radio:
                        append(items, paint, Elm::BuildRadioGroupItems(s.radio, radioStyle_, z, paint), kContentClip);
                        break;
                    case RowKind::Button: {
                        Elm::ButtonStyle style = buttonStyle_;
                        const bool hot = !s.button.states.disabled && contains(s.button.border, hover_);
                        style.background = hot ? theme_.accent : btnColor_;
                        append(items, paint, Elm::BuildButtonBackground(s.button, style, z, paint), kContentClip);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    // ---------- Itens de desenho ----------
    void appendControls(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        constexpr float z = Metrics::kControlZ;
        append(items, paint, Elm::BuildRadioGroupItems(apis_, radioStyle_, z, paint), kContentClip);
        for (const Elm::Dropdown* dd : allDropdowns()) {
            // Hover: o combo sob o ponteiro pinta com a cor de destaque do tema (amarelo no tema Ember, por exemplo).
            Elm::DropdownStyle style = dropdownStyle_;
            if (dd->border.Contains(hover_) && !dd->control.disabled) style.button = theme_.accent;
            append(items, paint, Elm::BuildDropdownButtonItems(*dd, style, z, paint), kContentClip);
        }
        append(items, paint, Elm::BuildSliderItems(scale_, sliderStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildCheckboxItems(pixel_, checkStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildCheckboxItems(shadows_, checkStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildCheckboxItems(rayTracing_, checkStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildCheckboxItems(rtAo_, checkStyle_, z, paint), kContentClip);
    }

    // Lista aberta de um combo: fundo e linhas com z acima dos controles. Os textos vem de overlay_.
    void appendPopups(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        for (const Elm::Dropdown* dd : allDropdowns()) {
            if (dd->control.open) {
                append(items, paint, Elm::BuildDropdownPopupItems(*dd, dropdownStyle_, kWindowClip, Metrics::kPopupZ, paint), kWindowClip);
            }
        }
        forEachFormSlot([&](FormSlot& s) {
            if (s.kind == settings_form::RowKind::Dropdown && s.dropdown.control.open) {
                append(items, paint, Elm::BuildDropdownPopupItems(s.dropdown, dropdownStyle_, kWindowClip, Metrics::kPopupZ, paint),
                       kWindowClip);
            }
        });
    }

    // Os builders devolvem posicoes locais da janela, usam o dono do elemento (que so existe numa ElementTree) e
    // NoClipIndex. O extrator recusa dono e recorte invalidos: aqui entram o dono valido, o recorte pedido e a
    // origem da janela na tela.
    void append(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, std::vector<SR::UIRenderItem> built, std::uint32_t clip) const {
        for (SR::UIRenderItem& item : built) {
            item.handle = sui::handleFor(0);
            item.clipIndex = clip;
            item.layout.resolvedPosition.x += rect_.x;
            item.layout.resolvedPosition.y += rect_.y;
        }
        paint += built.size() + 1;
        items.insert(items.end(), built.begin(), built.end());
    }

    // ---------- Ponteiro e roda ----------
    [[nodiscard]] bool inside(float x, float y) const {
        return visible_ && sui::Box{0.f, 0.f, rect_.w, rect_.h}.contains(x - rect_.x, y - rect_.y);
    }

    bool pointer(Elm::PointerPhase phase, float x, float y) {
        if (phase == Elm::PointerPhase::Move) hover_ = {x - rect_.x, y - rect_.y};   // hover: cabecalhos, combos, opcoes
        if (!inside(x, y)) {
            if (phase == Elm::PointerPhase::Down) {
                closeAllDropdowns();
                commitText();
            }
            return false;
        }
        const SU::Vec2 p{x - rect_.x, y - rect_.y};
        if (phase == Elm::PointerPhase::Down) blurOutside(p);   // clique fora do campo de texto confirma o texto
        if (phase == Elm::PointerPhase::Up && sectionPointer(p)) return true;
        if (phase == Elm::PointerPhase::Up && paletteRowPointer(p)) return true;
        if (appearanceSliderPointer(phase, p)) return true;
        if (phase == Elm::PointerPhase::Down && scrollbarPointer(p)) return true;
        if (formPointer(phase, p)) return true;
        if (!open_[kGraphicsTab]) return true;
        if (phase == Elm::PointerPhase::Down && stepperClick(p)) return true;
        contentPointer(phase, p);
        return true;
    }

    bool sectionPointer(const SU::Vec2& p) {
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            if (headers_[i].contains(p.x, p.y)) {
                open_[i] = !open_[i];
                if (!open_[i]) closeAllDropdowns();   // lista aberta nao sobrevive ao recolher
                return true;
            }
        }
        return false;
    }

    bool paletteRowPointer(const SU::Vec2& p) {
        for (std::size_t k = 0; k < editor::palette::kCount; ++k) {
            if (paletteRows_[k].contains(p.x, p.y)) {
                pending_.push_back({sc::Field::Palette, static_cast<int>(k), 0.f, false});
                return true;
            }
        }
        return false;
    }

    bool stripPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        if (phase == Elm::PointerPhase::Up && p.x < Metrics::kStripArrowW) {
            tabs_.scrollX -= 3.f * (tabs_.tabWidth + tabs_.tabGap);
            clampStrip();
            return true;
        }
        if (phase == Elm::PointerPhase::Up && p.x >= rect_.w - Metrics::kStripArrowW) {
            tabs_.scrollX += 3.f * (tabs_.tabWidth + tabs_.tabGap);
            clampStrip();
            return true;
        }
        const Elm::TabEvents ev = tabs_.OnPointer(event(phase, p, tabs_.border));
        if (ev.changed) pending_.push_back({sc::Field::Tab, static_cast<int>(tabs_.control.activeIndex), 0.f, false});
        return true;
    }

    // Roda sobre a faixa rola as abas; sobre o conteudo rola o conteudo (todas as secoes ficam na mesma lista
    // rolavel). Fora da janela, o ImGui fica com a roda (false).
    bool wheel(float x, float y, float dy) {
        if (!inside(x, y)) return false;
        const float delta = -dy * Metrics::kWheelStep;
        if (y - rect_.y < Metrics::kStripH) {
            tabs_.scrollX += delta;
            clampStrip();
            return true;
        }
        (void)bar_.Wheel(delta, scroll_);
        return true;
    }

    bool scrollbarPointer(const SU::Vec2& p) {
        if (!bar_.HasOverflow(scroll_) || p.x < bar_.Track().position.x) return false;
        (void)bar_.PointerDown(p, scroll_);
        return true;
    }

    bool dragScrollbar(float x, float y) {
        (void)bar_.PointerMove(SU::Vec2{x - rect_.x, y - rect_.y}, scroll_);
        return true;
    }

    void contentPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        // Combo aberto: a lista cobre os controles de baixo, entao so ele recebe o ponteiro.
        for (const DropdownSlot& slot : dropdownSlots()) {
            if (!slot.dd->control.open) continue;
            applyDropdown(*slot.dd, phase, p, slot.field);
            return;
        }
        const Elm::RadioEvents radio = apis_.OnPointer(event(phase, p, apis_.border));
        if (radio.valueChanged) pending_.push_back({sc::Field::Api, sb::ReadApiRadio(apis_), 0.f, false});
        applyDropdown(resolution_, phase, p, sc::Field::ResMode);
        if (scaleEnabled_) {
            const Elm::SliderEvents slide = scale_.OnPointer(event(phase, p, scale_.border));
            if (slide.valueChanged) pending_.push_back({sc::Field::RenderScale, 0, scale_.Value(), false});
        }
        applyDropdown(upscale_, phase, p, sc::Field::Upscale);
        applyDropdown(aa_, phase, p, sc::Field::Aa);
        applyCheck(pixel_, phase, p, sc::Field::PixelFilter);
        applyDropdown(fps_, phase, p, sc::Field::Fps);
        applyCheck(shadows_, phase, p, sc::Field::Shadows);
        applyCheck(rayTracing_, phase, p, sc::Field::RayTracing);
        if (rayTracingEnabled_) applyCheck(rtAo_, phase, p, sc::Field::RtAo);
        applyDropdown(shadowQuality_, phase, p, sc::Field::ShadowQuality);
    }

    // Botao: OnPointer abre ou fecha. Lista aberta: o soltar numa linha escolhe a opcao (ChooseAt). Clique fora do
    // botao e da lista fecha a lista sem escolher.
    [[nodiscard]] Elm::DropdownEvents dropdownEvents(Elm::Dropdown& dd, Elm::PointerPhase phase, const SU::Vec2& p) {
        const bool onList = dd.control.open && contains(dd.Popup(), p);
        Elm::DropdownEvents ev;
        if (onList) {
            if (phase == Elm::PointerPhase::Up) ev = dd.ChooseAt(p, windowRect());
        } else if (dd.control.open && phase == Elm::PointerPhase::Down && !contains(dd.border, p)) {
            ev = dd.CloseOutside();
        } else {
            ev = dd.OnPointer(event(phase, p, dd.border), windowRect());
        }
        return ev;
    }

    void applyDropdown(Elm::Dropdown& dd, Elm::PointerPhase phase, const SU::Vec2& p, sc::Field field) {
        const Elm::DropdownEvents ev = dropdownEvents(dd, phase, p);
        if (ev.opened) closeOthers(&dd);
        if (ev.valueChanged) pending_.push_back({field, static_cast<int>(dd.control.selectedIndex), 0.f, false});
    }

    void applyCheck(Elm::Checkbox& cb, Elm::PointerPhase phase, const SU::Vec2& p, sc::Field field) {
        const Elm::CheckboxEvents ev = cb.OnPointer(event(phase, p, cb.border));
        if (ev.valueChanged) pending_.push_back({field, 0, 0.f, cb.checked});
    }

    [[nodiscard]] static bool contains(const SU::Rect& box, const SU::Vec2& p) {
        return p.x >= box.position.x && p.x < box.position.x + box.size.x && p.y >= box.position.y &&
               p.y < box.position.y + box.size.y;
    }

    [[nodiscard]] static Elm::PointerEvent event(Elm::PointerPhase phase, const SU::Vec2& p, const SU::Rect& box) {
        return Elm::PointerEvent{phase, p, 0u, contains(box, p)};
    }

    bool stepperClick(const SU::Vec2& p) {
        for (Stepper& s : steps_) {
            if (sui::Box{s.minus.x, s.minus.y, s.minus.w, s.minus.h}.contains(p.x, p.y)) return step(s, -Metrics::kCustomStep);
            if (sui::Box{s.plus.x, s.plus.y, s.plus.w, s.plus.h}.contains(p.x, p.y)) return step(s, Metrics::kCustomStep);
        }
        return false;
    }

    bool step(const Stepper& s, int delta) {
        const int next = std::clamp(s.value + delta, s.minValue, s.maxValue);
        if (next != s.value) pending_.push_back({s.field, next, 0.f, false});
        return true;
    }

    void closeOthers(const Elm::Dropdown* keep) {
        for (const DropdownSlot& slot : dropdownSlots()) {
            if (slot.dd != keep) slot.dd->control.Close();
        }
        forEachFormSlot([&](FormSlot& s) {
            if (s.kind == settings_form::RowKind::Dropdown && &s.dropdown != keep) s.dropdown.control.Close();
        });
    }

    [[nodiscard]] std::array<DropdownSlot, 5> dropdownSlots() {
        return {{{&resolution_, sc::Field::ResMode},
                 {&upscale_, sc::Field::Upscale},
                 {&aa_, sc::Field::Aa},
                 {&fps_, sc::Field::Fps},
                 {&shadowQuality_, sc::Field::ShadowQuality}}};
    }

    void closeAllDropdowns() { closeOthers(nullptr); }

    sui::Canvas strip_;     // textos da faixa (recorte da faixa)
    sui::Canvas canvas_;    // textos e formas da aba Graficos (recorte do conteudo)
    sui::Canvas overlay_;   // textos das listas abertas (por cima dos controles)
    sui::SpriteUiSdlRenderer& sdl_;
    framed::Chrome chrome_;
    sui::Theme theme_;
    std::array<bool, sm::kTabCount> open_ = [] {
        std::array<bool, sm::kTabCount> a{};
        a[kGraphicsTab] = true;   // API/Graphics abre aberta
        return a;
    }();
    std::array<sui::Box, sm::kTabCount> headers_{};                      // cabecalhos (coordenadas locais)
    std::array<sui::Box, editor::palette::kCount> paletteRows_{};        // linhas de tema (coordenadas locais)
    SU::Vec4 panelColor_ = sui::Srgb(0.055f, 0.075f, 0.11f, 0.97f);
    SU::Vec4 headingColor_ = sui::Srgb(0.45f, 0.72f, 0.96f);
    SU::Vec4 btnColor_ = sui::Srgb(0.13f, 0.16f, 0.21f);
    SU::Vec4 trackColor_ = sui::Srgb(0.09f, 0.11f, 0.15f);
    SU::Vec4 thumbColor_ = sui::Srgb(0.30f, 0.40f, 0.55f);
    SR::RenderExtractor extractor_;
    SR::FramePacket packet_;
    std::uint64_t frameId_ = 0;

    sui::Box rect_;
    bool visible_ = false;
    int revealedTab_ = -1;
    bool scaleEnabled_ = true;
    bool rayTracingEnabled_ = true;
    float labelW_ = Metrics::kLabelW;     // largura da coluna de rotulos (cabe na janela)
    float controlW_ = Metrics::kControlW; // largura da coluna de controles
    float contentH_ = 0.f;                // altura do conteudo da aba Graficos (do ultimo layout)
    std::vector<Stepper> steps_;
    sc::Requests pending_;
    std::array<std::vector<std::unique_ptr<FormSlot>>, sm::kTabCount> formPool_{};   // controles das linhas, por secao
    std::size_t focusSection_ = npos;   // campo de texto com foco (secao e linha); npos = nenhum
    std::size_t focusIndex_ = npos;
    SDL_Window* window_ = nullptr;      // janela do ultimo clique: liga e desliga o texto do SDL

    W::UIScrollViewComponent scroll_;
    W::ScrollBarControl bar_;

    Elm::TabBar tabs_;
    Elm::RadioGroup apis_;
    Elm::Dropdown resolution_;
    Elm::Slider scale_;
    Elm::Slider buttonRadius_, windowRadius_, textScale_;   // Appearance: raios e escala do texto
    bool appearanceOpen_ = false;
    Elm::Dropdown upscale_;
    Elm::Dropdown aa_;
    Elm::Checkbox pixel_;
    Elm::Dropdown fps_;
    Elm::Checkbox shadows_;
    Elm::Dropdown shadowQuality_;
    Elm::Checkbox rayTracing_;
    Elm::Checkbox rtAo_;

    Elm::TabBarStyle tabStyle_;
    SU::Vec2 hover_{-1.f, -1.f};   // ultima posicao do ponteiro (coordenadas da janela), para o hover
    Elm::RadioStyle radioStyle_;
    Elm::DropdownStyle dropdownStyle_;
    Elm::SliderStyle sliderStyle_;
    Elm::CheckboxStyle checkStyle_;
    Elm::ButtonStyle buttonStyle_;
};

}  // namespace editor::settings_panel
