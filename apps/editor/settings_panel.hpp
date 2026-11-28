#pragma once

// Janela Configuracoes em SpriteUI: faixa de abas e conteudo das abas.
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
//                sincroniza cada elemento com Values e desenha.
//                As linhas genericas (Values::form) de cada secao aberta vem depois do conteudo fixo.
//   handleEvent: clique, arrasto da barra e roda. Clique num combo aberto fecha os outros. Texto e teclas vao ao
//                campo de texto com foco. Pedidos vao para pending_.

#include "i18n.hpp"
#include "color_popup.hpp"
#include "framed_window.hpp"
#include "free_values.hpp"
#include "inspector_model.hpp"
#include "settings_bridge.hpp"
#include "settings_changes.hpp"
#include "settings_form.hpp"
#include "slider_value_edit.hpp"
#include "pointer_capture.hpp"
#include "settings_model.hpp"
#include "sui_panel.hpp"
#include "theme_colors.hpp"
#include "ui_style.hpp"

#include "SpriteUi/Core/DrawOrder.hpp"
#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/RadioButton.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Elements/TabBar.hpp"
#include "ui_scrollbar.hpp"
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
namespace im = editor::inspector_model;
namespace SU = ToolEngine::SpriteUi;
namespace Elm = SU::Elements;
namespace W = SU::Widgets;
namespace SR = SU::Rendering;
namespace ST = SU::Text;

inline constexpr int kGraphicsTab = sm::kTabGraphics;   // categoria "Graphics and API" (API, resolucao, FSR, escala, FPS)
inline constexpr int kQualityTab = sm::kTabQuality;     // categoria "Quality" (AA, sombras, RT, pos-processamento, malhas)
inline constexpr std::size_t kAppearanceTab = static_cast<std::size_t>(sm::kTabInterface);   // categoria "Interface" (temas, raios, texto, cores)

// Valores que a janela mostra. O main.cpp copia do Editor a cada quadro.
struct Values {
    int tab = kGraphicsTab;
    // Linhas extra de cada secao (settings_form.hpp), desenhadas depois dos controles fixos da secao.
    // Lighting, Game, Optimizing e Language usam so estas linhas; API/Graphics recebe Physics, Tags e Project.
    // Categoria Graphics and API: as linhas dela (bloco FSR) saem logo abaixo da escolha de API, antes de resolucao e escala.
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
    float renderScale = 1.f;      // escala MOSTRADA: o slider, ou a derivada do modo de qualidade quando o FSR manda
    bool scaleLocked = false;     // FSR ligado: o slider de Render scale fica desabilitado (mostra o valor derivado)
    int upscale = 0;
    int aa = 0;
    bool pixelFilter = false;
    int fps = 0;
    int palette = 0;          // indice da paleta em uso (Configuracoes > Appearance)
    float buttonRadius = 3.f; // raio dos botoes (px)
    float windowRadius = 6.f; // raio dos cantos das janelas (px)
    float textScale = 1.f;    // escala do texto da interface (1 = 100%)
    // Paleta Custom: 5 cores-base e 8 tokens, cada um como R, G, B (0..1). Ordem: base 0..4, depois tokens 5..12.
    std::array<float, 39> customColor{};
    std::vector<std::string> themeFiles;   // arquivos de tema (assets/themes), na ordem da lista
    bool shadows = false;
    int shadowQuality = 0;   // indice (0 a 3), ja convertido pelo main.cpp
    bool rayTracing = true;
    bool rtAo = false;
    bool gpuRayTracing = false;   // o dispositivo grafico ativo suporta ray tracing por GPU (so para a linha de status)
    std::string adapter;
    std::string pixelsLine;
    std::string gpuErrorLine;
    std::string gpuStatsLine;
};

// Medidas fixas da janela (alturas de linha, margens, larguras dos controles, camadas Z).
struct Metrics {
    static constexpr float kStripH = 0.f;   // faixa de abas removida: o conteudo comeca no topo do painel
    static constexpr float kHeaderH = 36.f;   // cabecalho de cada secao (recolhivel)
    static constexpr float kPad = sui::kPanelPadding;
    static constexpr float kStripArrowW = 24.f;
    static constexpr float kLabelW = 210.f;
    static constexpr float kControlW = 280.f;
    static constexpr float kRowH = 32.f;
    static constexpr float kRadioRowH = 27.f;
    static constexpr float kHeadingH = 32.f;
    static constexpr float kTextH = 22.f;
    static constexpr float kGap = 7.f;
    static constexpr float kFont = 15.f;
    static constexpr float kSmallFont = 13.f;
    static constexpr float kStepW = 26.f;     // botoes - e + dos tamanhos personalizados
    static constexpr float kValueW = 56.f;    // espaco do valor ao lado do deslizante
    static constexpr float kScrollW = 8.f;    // largura da barra de rolagem do conteudo
    static constexpr float kWheelStep = 40.f; // pixels por tique da roda do mouse
    static constexpr int kCustomStep = 16;    // passo para largura e altura
    static constexpr float kPopupTextZ = 0.97f;
    static constexpr float kPopupZ = 0.95f;
    static constexpr float kColorPopupW = 232.f, kColorPopupH = 210.f;   // seletor de cor (mesmo tamanho do Inspector)
    static constexpr float kControlZ = 0.3f;
};

// Recortes do painel (indices em clips): janela inteira, area de conteudo e faixa de abas.
inline constexpr std::uint32_t kWindowClip = 0u;
inline constexpr std::uint32_t kContentClip = 1u;
inline constexpr std::uint32_t kStripClip = 2u;

// Janela Configuracoes: faixa de abas rolavel, secoes que abrem/fecham (APIs graficas, qualidade, GPU, aparencia e formularios genericos), combos, sliders, campos e popup de cor. Altera so os pedidos (pending_); o host aplica.
class SettingsPanel {
 public:
    SettingsPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
        : strip_{text}, canvas_{text}, overlay_{text}, sdl_{sdl}, chrome_{text, sdl} {
        overlay_.setTextZ(Metrics::kPopupTextZ);
        for (FormSlot& s : fixed_) s.kind = settings_form::RowKind::Slider;
        fixed_[kFixedButtonRadius].field = sc::Field::ButtonRadius;
        fixed_[kFixedWindowRadius].field = sc::Field::WindowRadius;
        fixed_[kFixedTextScale].field = sc::Field::TextScale;
        colorPopup_.onChange([this](const float rgb[3]) {
            if (colorPopup_.key() == kFormColorKey) formColorPicked();
            else customPicked(rgb);
        });
    }
    SettingsPanel(const SettingsPanel&) = delete;
    // Moldura SpriteUI (titulo, X, "?", grip e acoplamento). O corpo e desenhado pelo painel, abaixo do cabecalho.
    framed::Chrome& chrome() { return chrome_; }

    // Dica (tooltip) de uma opcao: retangulo em coordenadas de TELA (as mesmas do mouse) e o texto ja traduzido.
    struct Tip {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        std::string text;
    };
    // Dicas do ultimo quadro desenhado (so a parte visivel, recortada na area de conteudo). O host registra cada uma em
    // Tooltip::target. O vetor e refeito a cada draw(); fica vazio com a janela escondida.
    [[nodiscard]] const std::vector<Tip>& tips() const { return tips_; }
    // true se o ponto (coordenadas da tela) esta sobre a janela visivel (o host usa para pedir quadros do balao).
    [[nodiscard]] bool containsPoint(float x, float y) const { return inside(x, y); }
    // true se ha lista de combo ou seletor de cor aberto: o host suprime o balao para nao ficar por cima da lista.
    [[nodiscard]] bool overlayOpen() const {
        if (colorPopup_.isOpen() || graphicsDropdownOpen()) return true;
        for (const auto& pool : formPool_)
            for (const auto& s : pool)
                if (s->kind == settings_form::RowKind::Dropdown && s->dropdown.control.open) return true;
        return false;
    }
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
                if (!pointer(Elm::PointerPhase::Down, e.button.x, e.button.y)) return false;
                capture_.down();   // so um clique dentro do painel abre a captura
                return true;
            case SDL_EVENT_TEXT_INPUT:
                if (colorPopup_.editingHex()) return colorPopup_.text(e.text.text, window_);
                if (FormSlot* s = focusedSlot()) {
                    if (e.text.text) s->edit.InsertText(e.text.text);
                    return true;
                }
                return false;
            case SDL_EVENT_KEY_DOWN:
                if (colorPopup_.editingHex()) return colorPopup_.keyDown(e.key.key, static_cast<SDL_Keymod>(e.key.mod), window_);
                return textKey(e.key.key);
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (sbar_.pointerUp()) {   // soltou o polegar da barra de rolagem
                    (void)capture_.up();
                    return true;
                }
                if (e.button.button != SDL_BUTTON_LEFT) return inside(e.button.x, e.button.y);
                // Up dentro do painel segue o caminho normal (cabecalhos, paletas, controles) mesmo com a captura aberta:
                // a captura so muda o caso de soltar fora. Soltar fora solta os controles sem disparar cliques.
                {
                    const bool hadCapture = capture_.up();
                    if (hadCapture && !inside(e.button.x, e.button.y)) {
                        releasePointer(e.button.x, e.button.y);
                        return true;
                    }
                    // Up sem Down anterior dentro do painel (soltura duplicada ou sintetica da guarda do botao) nao e
                    // clique: sem isto o cabecalho de secao abria e fechava na mesma soltura ("o + nao expande").
                    if (!hadCapture) return inside(e.button.x, e.button.y);
                }
                return pointer(Elm::PointerPhase::Up, e.button.x, e.button.y);
            case SDL_EVENT_MOUSE_MOTION:
                if (sbar_.dragging()) {
                    if ((e.motion.state & SDL_BUTTON_LMASK) == 0) {   // soltura perdida: encerra o arraste
                        (void)sbar_.pointerUp();
                        capture_.reset();
                        return true;
                    }
                    return dragScrollbar(e.motion.x, e.motion.y);
                }
                (void)sbar_.pointerMove(e.motion.x - rect_.x, e.motion.y - rect_.y, scroll_.scrollOffset.y);   // so hover
                // Sem o botao esquerdo, nao existe arrasto: se a captura estava aberta, solta os controles.
                if (capture_.move((e.motion.state & SDL_BUTTON_LMASK) != 0)) {
                    releasePointer(e.motion.x, e.motion.y);
                    return true;
                }
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
        if (!visible) { tips_.clear(); return true; }
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
            dropdownTexts(true);
            radioTexts();
        }
        if (open_[kQualityTab]) dropdownTexts(false);
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
        if (open_[kGraphicsTab]) appendControls(items, paint, true);
        if (open_[kQualityTab]) appendControls(items, paint, false);
        if (open_[kAppearanceTab]) appendAppearance(items, paint);
        appendForm(items, paint);
        colorPopup_.setLook(colorLook());
        colorPopup_.append(items, paint, owner, SU::Vec2{rect_.x, rect_.y}, kWindowClip, SU::Rect{{rect_.x, rect_.y}, {rect_.w, rect_.h}});
        overlay_.appendItems(items, rect_.x, rect_.y, 0.f, rect_.y, bottom, owner, kWindowClip, paint);
        appendPopups(items, paint);

        packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
        if (!SR::IsFramePacketValid(packet_)) return true;
        return sdl_.draw(renderer, packet_);
    }

 private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    // Controle de +/- para um valor inteiro (limites e caixas dos botoes).
    struct Stepper {
        sc::Field field;
        sui::Box minus;
        sui::Box plus;
        int value;
        int minValue;
        int maxValue;
    };

    // Par combo + campo de configuracao que ele altera.
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

    // Prende a rolagem horizontal da faixa de abas nos limites.
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

    // Desenha a faixa de abas.
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
        sbar_.layout({rect_.w - ui_scrollbar::kWidth, Metrics::kStripH, ui_scrollbar::kWidth, viewH}, contentH_, viewH);
    }

    // Barra de rolagem unica do editor (ui_scrollbar.hpp).
    void drawScrollbar() {
        for (const ui_scrollbar::Quad& q : sbar_.quads(palette::active(), scroll_.scrollOffset.y)) {
            if (q.rect.w > 0.f && q.rect.h > 0.f) canvas_.addSolid(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.25f);
        }
    }

    // ---------- Layout da aba Graficos (coordenadas da janela, ja com a rolagem) ----------
    // Cada bloco devolve o y da linha seguinte. contentH_ guarda a altura total (com as margens) para a rolagem.
    // Secoes recolheis, uma embaixo da outra. Cada cabecalho e clicavel; o corpo so existe se a secao esta aberta.
    void layoutSections(const Values& v) {
        palette_ = v.palette;
        labelW_ = std::min(Metrics::kLabelW, rect_.w * 0.5f);
        controlW_ = rect_.w - 2.f * Metrics::kPad - labelW_;
        const float contentTop = Metrics::kStripH + Metrics::kPad;
        const float top = contentTop - scroll_.scrollOffset.y;
        const float headerW = rect_.w - 2.f * Metrics::kPad;
        for (auto& row : paletteRows_) row = sui::Box{};   // linhas de tema so existem com a secao aberta
        for (auto& row : themeRows_) row = sui::Box{};
        baseBtns_.fill(sui::Box{});
        colorGroups_.clear();   // refeitos por layoutForm a cada passada
        tokenPrev_ = tokenNext_ = sui::Box{};
        appearanceOpen_ = false;
        tips_.clear();   // as dicas sao refeitas a cada passada do layout
        float y = top;
        for (std::size_t i = 0; i < sm::kTabCount; ++i) {
            headers_[i] = sui::Box{Metrics::kPad, y, headerW, Metrics::kHeaderH};
            drawHeader(i, y, headerW);
            y += Metrics::kHeaderH + Metrics::kGap;
            if (!open_[i]) continue;
            if (i == kAppearanceTab) {
                // Formulario de presets de configuracao PRIMEIRO (topo da janela), depois temas e estilo.
                y = layoutForm(i, v.form[i], y);
                y = layoutAppearance(v, y);
                y += 2.f * Metrics::kGap;
                continue;
            }
            if (i == static_cast<std::size_t>(kGraphicsTab)) {
                y = layoutGraphicsBody(v, y);   // inclui as linhas do bloco FSR (v.form), logo abaixo da API
                y += 2.f * Metrics::kGap;
                continue;
            }
            if (i == static_cast<std::size_t>(kQualityTab)) {
                y = layoutQualityBody(v, y);   // controles fixos primeiro; depois as linhas (pos-processamento, malhas)
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

    // Cabecalho de uma secao (clicavel; realca no hover).
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
    // Ordem: status, escolha de API, bloco FSR (linhas de v.form), resolucao, Render scale, upscale de software e FPS.
    float layoutGraphicsBody(const Values& v, float y) {
        text(Metrics::kPad, y, v.apiStatus, Metrics::kFont, textWidth());
        y += Metrics::kTextH + 6.f;
        y = layoutApis(v, y);
        y = layoutForm(static_cast<std::size_t>(kGraphicsTab), v.form[kGraphicsTab], y);   // bloco "Upscaling FSR"
        y = layoutQuality(v, y);
        return y;
    }

    // Corpo da categoria Quality: anti-aliasing, sombras e ray tracing (controles fixos), depois as linhas da categoria.
    float layoutQualityBody(const Values& v, float y) {
        return layoutGpu(v, y);
    }

    // Corpo da secao Appearance: escolha da paleta (a linha marcada e a ativa).
    float layoutAppearance(const Values& v, float y) {
        canvas_.addText(Metrics::kPad, y, std::string(T("Theme")), Metrics::kFont, theme_.textDim, textWidth());
        y += Metrics::kTextH + 6.f;
        const auto& presets = editor::palette::kPresets;
        for (std::size_t k = 0; k <= editor::palette::kCustomIndex; ++k) {   // as prontas e, por ultimo, Custom
            paletteRows_[k] = sui::Box{Metrics::kPad, y, textWidth(), Metrics::kRowH};
            const bool selected = static_cast<int>(k) == v.palette;
            canvas_.addSolid(paletteRows_[k].x, y, paletteRows_[k].w, Metrics::kRowH,
                             selected ? theme_.accent : theme_.surface, 0.2f, editor::ui_style::buttonRadius());
            const std::string name = k < editor::palette::kCount ? std::string(T(presets[k].name)) : std::string(T("Custom"));
            canvas_.addText(Metrics::kPad + 10.f, y + 5.f, name, Metrics::kFont, theme_.text, textWidth() - 20.f);
            tip(Metrics::kPad, y, textWidth(), Metrics::kRowH,
                k < editor::palette::kCount ? "Applies this color theme to the whole editor."
                                            : "Your own color theme. Edit the colors below.");
            y += Metrics::kRowH + Metrics::kGap;
        }
        y += Metrics::kGap;
        appearanceOpen_ = true;
        if (v.palette == static_cast<int>(editor::palette::kCustomIndex)) y = layoutCustom(v, y);
        if (!v.themeFiles.empty()) {   // arquivos de tema (assets/themes): clique aplica
            canvas_.addText(Metrics::kPad, y, std::string(T("User themes")), Metrics::kFont, theme_.textDim, textWidth());
            y += Metrics::kTextH + 6.f;
            themeRows_.assign(v.themeFiles.size(), sui::Box{});
            for (std::size_t k = 0; k < v.themeFiles.size(); ++k) {
                themeRows_[k] = sui::Box{Metrics::kPad, y, textWidth(), Metrics::kRowH};
                canvas_.addSolid(themeRows_[k].x, y, themeRows_[k].w, Metrics::kRowH, theme_.surface, 0.2f, editor::ui_style::buttonRadius());
                canvas_.addText(Metrics::kPad + 10.f, y + 5.f, v.themeFiles[k], Metrics::kFont, theme_.text, textWidth() - 20.f);
                tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, "Applies this theme file from the themes folder.");
                y += Metrics::kRowH + Metrics::kGap;
            }
            y += Metrics::kGap;
        }
        y = styleRow(kFixedButtonRadius, v.buttonRadius, "Button corner radius", y, 0.f, sm::kButtonRadiusMax, 1.f, true,
                     std::to_string(static_cast<int>(v.buttonRadius + 0.5f)) + " px");
        y = styleRow(kFixedWindowRadius, v.windowRadius, "Window corner radius", y, 0.f, sm::kWindowRadiusMax, 1.f, true,
                     std::to_string(static_cast<int>(v.windowRadius + 0.5f)) + " px");
        y = styleRow(kFixedTextScale, v.textScale, "Interface text size", y, sm::kTextScaleMin, sm::kTextScaleMax, 0.05f, false,
                     std::to_string(static_cast<int>(v.textScale * 100.f + 0.5f)) + "%", editor::free_values::kTextScaleTypedMin,
                     editor::free_values::kTextScaleTypedMax);
        return y;
    }

    // Editor da paleta Custom: as 5 cores-base (botoes), a lista avancada (token com < e >) e os sliders R, G, B da
    // cor escolhida. colorSlot_: 0..4 = cor-base; 5..12 = token (5 + indice do token).
    float layoutCustom(const Values& v, float y) {
        canvas_.addText(Metrics::kPad, y, std::string(T("Base colors")), Metrics::kFont, theme_.textDim, textWidth());
        y += Metrics::kTextH + 6.f;
        const float bw = textWidth() / static_cast<float>(editor::palette::kBaseCount);
        for (std::size_t b = 0; b < editor::palette::kBaseCount; ++b) {
            baseBtns_[b] = sui::Box{Metrics::kPad + bw * static_cast<float>(b), y, bw - 4.f, Metrics::kRowH};
            const bool sel = colorSlot_ == static_cast<int>(b);
            canvas_.addSolid(baseBtns_[b].x, y, baseBtns_[b].w, Metrics::kRowH, sel ? theme_.accent : theme_.surface, 0.2f,
                             editor::ui_style::buttonRadius());
            canvas_.addText(baseBtns_[b].x + 4.f, y + 5.f, std::string(T(editor::palette::kBaseNames[b])), Metrics::kSmallFont,
                            theme_.text, bw - 12.f);
            tip(baseBtns_[b].x, y, baseBtns_[b].w, Metrics::kRowH, "Selects this base color of the custom theme to edit.");
        }
        y += Metrics::kRowH + Metrics::kGap;
        canvas_.addText(Metrics::kPad, y, std::string(T("Advanced colors")), Metrics::kFont, theme_.textDim, textWidth());
        y += Metrics::kTextH + 6.f;
        tokenPrev_ = sui::Box{Metrics::kPad, y, Metrics::kRowH, Metrics::kRowH};
        tokenNext_ = sui::Box{Metrics::kPad + textWidth() - Metrics::kRowH, y, Metrics::kRowH, Metrics::kRowH};
        canvas_.addSolid(tokenPrev_.x, y, tokenPrev_.w, tokenPrev_.h, theme_.surface, 0.2f, editor::ui_style::buttonRadius());
        canvas_.addSolid(tokenNext_.x, y, tokenNext_.w, tokenNext_.h, theme_.surface, 0.2f, editor::ui_style::buttonRadius());
        canvas_.addText(tokenPrev_.x + 9.f, y + 5.f, std::string("<"), Metrics::kFont, theme_.text, 20.f);
        canvas_.addText(tokenNext_.x + 9.f, y + 5.f, std::string(">"), Metrics::kFont, theme_.text, 20.f);
        canvas_.addText(tokenPrev_.x + Metrics::kRowH + 6.f, y + 5.f,
                        std::string(T(editor::palette::kTokenNames[colorSlot_ >= 5 ? colorSlot_ - 5 : 0])), Metrics::kFont,
                        theme_.text, textWidth() - 2.f * Metrics::kRowH - 12.f);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, "Use the arrows to choose which detailed color of the custom theme to edit.");
        y += Metrics::kRowH + Metrics::kGap;
        // Cor da paleta escolhida: a amostra abre o seletor (o mesmo popup do Inspector). Sem o seletor aberto, a linha
        // mostra o valor do modelo; com ele aberto, mostra a cor que esta sendo editada.
        const int slot = std::clamp(colorSlot_, 0, 12);
        for (std::size_t ch = 0; ch < 3; ++ch) customModel_[ch] = v.customColor[static_cast<std::size_t>(slot) * 3 + ch];
        const float* shown = colorPopup_.isOpen() ? customRgb_ : customModel_;
        customSwatch_ = SU::Rect{{Metrics::kPad, y}, {textWidth(), Metrics::kRowH}};
        canvas_.addSolid(customSwatch_.position.x, y, customSwatch_.size.x, customSwatch_.size.y, btnColor_, 0.2f,
                         editor::ui_style::buttonRadius());
        canvas_.addSolid(customSwatch_.position.x + 6.f, y + 6.f, 26.f, customSwatch_.size.y - 12.f,
                         SU::Vec4{shown[0], shown[1], shown[2], 1.f}, 0.24f);
        canvas_.addText(customSwatch_.position.x + 40.f, y + 5.f, std::string(T("Color")) + "   " + im::HexText(shown), Metrics::kFont,
                        theme_.text, customSwatch_.size.x - 46.f);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, "Click to open the color picker for the selected color.");
        y += Metrics::kRowH + Metrics::kGap;
        return y + Metrics::kGap;
    }

    struct FormSlot;   // definido com as linhas genericas, abaixo

    // Caixa do valor de um deslizante, a direita da faixa: a mesma geometria em toda linha de slider. Mostra o texto
    // dado (unidade ou numero); sem foco de digitacao (quem digita usa layoutValueBox).
    void drawValueBox(const std::string& shown, float y) {
        const float vx = Metrics::kPad + labelW_ + controlW_ - Metrics::kValueW + 4.f;
        const float vw = Metrics::kValueW - 4.f;
        const SU::Rect box = rectOf(vx, y, vw, Metrics::kRowH);
        canvas_.addSolid(box.position.x, box.position.y, box.size.x, box.size.y, btnColor_, 0.2f,
                         editor::ui_style::buttonRadius());
        canvas_.addText(box.position.x + 6.f, y + 5.f, shown, Metrics::kFont, theme_.textDim, vw - 8.f);
    }

    // Um slider com rotulo e valor na linha de Appearance (slot fixo k). A faixa fica em `s.slider.border`.
    float styleRow(std::size_t k, float value, const char* label, float y, float lo, float hi, float step, bool integer,
                   const std::string& shown, float typedLo = 0.f, float typedHi = 0.f) {
        FormSlot& s = fixed_[k];
        canvas_.addText(Metrics::kPad, y + 5.f, std::string(T(label)), Metrics::kFont, theme_.text, labelW_ - 8.f);
        s.slider.range = Elm::SliderRange{lo, hi, step, integer};
        s.slider.SetValue(value);
        s.slider.border = rectOf(Metrics::kPad + labelW_, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
        s.lo = lo;
        s.hi = hi;
        s.step = step;
        s.isInt = integer;
        s.typedLo = typedLo;   // numero digitado: faixa propria (maior que a do slider) quando typedHi > typedLo
        s.typedHi = typedHi;
        s.modelValue = value;
        layoutValueBox(s, kFixedSection, k, y, shown);
        // Dica do slider fixo: texto do raio dos botoes, das janelas ou da escala do texto.
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH,
            k == kFixedButtonRadius   ? "How rounded the corners of buttons and fields are."
            : k == kFixedWindowRadius ? "How rounded the corners of windows are."
                                      : "Scales all the text of the editor interface.");
        return y + Metrics::kRowH + Metrics::kGap;
    }

    // Campo do valor de um deslizante (a direita da faixa). Sem foco mostra `shown`; com foco mostra o texto digitado
    // e o caret. Clique nele abre a digitacao (sliderPointer).
    void layoutValueBox(FormSlot& s, std::size_t section, std::size_t k, float y, const std::string& shown) {
        const bool focused = isFocused(section, k);
        const float vx = Metrics::kPad + labelW_ + controlW_ - Metrics::kValueW + 4.f;
        const float vw = Metrics::kValueW - 4.f;
        const SU::Rect box = rectOf(vx, y, vw, Metrics::kRowH);
        s.box = box;
        s.shown = shown;
        canvas_.addSolid(box.position.x, box.position.y, box.size.x, box.size.y, focused ? theme_.surface : btnColor_, 0.2f,
                         editor::ui_style::buttonRadius());
        if (!focused) {
            canvas_.addText(box.position.x + 6.f, y + 5.f, shown, Metrics::kFont, theme_.textDim, vw - 8.f);
            return;
        }
        const std::string& typed = s.edit.Text();
        canvas_.addText(box.position.x + 6.f, y + 5.f, typed, Metrics::kFont, theme_.text, vw - 8.f);
        const float cx = box.position.x + 6.f + canvas_.layout(typed.substr(0, s.edit.Caret()), Metrics::kFont).size.x;
        canvas_.addSolid(cx, y + 4.f, 1.5f, Metrics::kRowH - 8.f, theme_.text, 0.25f);
    }

    // Itens de desenho dos sliders da secao Aparencia.
    void appendAppearance(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        constexpr float z = Metrics::kControlZ;
        append(items, paint, Elm::BuildSliderItems(fixed_[kFixedButtonRadius].slider, sliderStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildSliderItems(fixed_[kFixedWindowRadius].slider, sliderStyle_, z, paint), kContentClip);
        append(items, paint, Elm::BuildSliderItems(fixed_[kFixedTextScale].slider, sliderStyle_, z, paint), kContentClip);
    }

    // Arrasto dos sliders de Appearance: cada valor vai pelo mesmo caminho dos outros campos (pedido -> main.cpp).
    bool appearanceSliderPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        if (!appearanceOpen_) return false;
        const std::size_t sliders[] = {kFixedButtonRadius, kFixedWindowRadius, kFixedTextScale};
        for (std::size_t k : sliders) {
            if (sliderPointer(kFixedSection, k, fixed_[k], phase, p)) return true;
        }
        return false;
    }

    // Tema lido a cada quadro: trocar a paleta muda esta janela na hora.
    void refreshColors() {
        theme_ = sui::Theme{};
        panelColor_ = theme_.panel;
        headingColor_ = theme_.scroll;
        btnColor_ = sui::PaletteColor(editor::theme_colors::Active().btn);
    }

    // Posiciona a secao de API grafica (radios das APIs compiladas); devolve o Y seguinte.
    float layoutApis(const Values& v, float y) {
        heading(y, T("Graphics API"));
        y += Metrics::kHeadingH;
        const float radioH = static_cast<float>(builtApis(v)) * Metrics::kRadioRowH;
        apis_.border = rectOf(Metrics::kPad + labelW_, y, controlW_, radioH);
        tip(Metrics::kPad, y, textWidth(), radioH, "Graphics API used to draw the editor and the game. Only the APIs built into this version appear.");
        return y + radioH + 6.f;
    }

    // Posiciona a secao de qualidade (resolucao, upscale, anti-aliasing...); devolve o Y seguinte.
    float layoutQuality(const Values& v, float y) {
        heading(y, T("Resolution and frame rate"));
        y += Metrics::kHeadingH;
        controlRow(resolution_, y, T("Game resolution"), "Size of the image the game renders. 'Match window' follows the window size.");
        y += Metrics::kRowH + Metrics::kGap;
        if (v.resMode == sm::kResolutionCustom) {
            stepper(y, T("Width"), sc::Field::CustomW, v.customW, sm::kCustomWidthMin, sm::kCustomWidthMax);
            y += Metrics::kRowH + Metrics::kGap;
            stepper(y, T("Height"), sc::Field::CustomH, v.customH, sm::kCustomHeightMin, sm::kCustomHeightMax);
            y += Metrics::kRowH + Metrics::kGap;
        }
        scale_.border = rectOf(Metrics::kPad + labelW_, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
        // Com o FSR ligado o slider fica apagado e mostra a escala derivada do modo de qualidade (1 / fator).
        const bool scaleOff = v.scaleLocked || v.resMode != 0;
        const SU::Vec4 scaleInk = scaleOff ? theme_.textDim : theme_.text;
        canvas_.addText(Metrics::kPad, y + 6.f, std::string(v.scaleLocked ? T("Render scale (set by FSR)") : T("Render scale")),
                        Metrics::kFont, scaleInk, labelW_ - 6.f);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH,
            v.scaleLocked ? "While FSR is on, the render scale comes from the FSR quality mode. Turn FSR off to use the slider."
                          : "Multiplies the render resolution. Lower is faster, higher is sharper.");
        drawValueBox(Elm::SliderValueText(scale_), y);
        y += Metrics::kRowH + Metrics::kGap;
        text(Metrics::kPad, y, T("Below 1.0 renders fewer pixels (faster); above 1.0 renders more pixels (sharper, slower)."),
             Metrics::kSmallFont, textWidth());
        y += Metrics::kTextH + Metrics::kGap;
        controlRow(upscale_, y, T("Upscaling (software)"), "Filter used to enlarge the image when it is drawn bigger than it was rendered.");
        y += Metrics::kRowH + Metrics::kGap;
        controlRow(fps_, y, T("Frame rate limit"), "Limits how many frames per second the editor draws. VSync follows the monitor.");
        y += Metrics::kRowH + Metrics::kGap;
        if (v.fixedSize) {
            char line[96];
            std::snprintf(line, sizeof line, T("Game renders at %d x %d."), v.fixedW, v.fixedH);
            text(Metrics::kPad, y, line, Metrics::kFont, textWidth());
            y += Metrics::kTextH + 6.f;
        }
        return y;
    }

    // Posiciona a secao de detalhes de GPU (sombras, etc); devolve o Y seguinte.
    float layoutGpu(const Values& v, float y) {
        heading(y, T("Anti-aliasing and filtering"));
        y += Metrics::kHeadingH;
        controlRow(aa_, y, T("Anti-aliasing (software)"), "Smooths jagged edges. FXAA is a fast screen filter.");
        y += Metrics::kRowH + Metrics::kGap;
        checkRow(pixel_, y, T("Sharp pixels when enlarging (no smoothing)"), "Keeps hard pixel edges when the image is enlarged, good for pixel art.", v.pixelFilter);
        y += Metrics::kRowH + Metrics::kGap;
        heading(y, T("Shadows and ray tracing"));
        y += Metrics::kHeadingH;
        checkRow(shadows_, y, T("Enable shadows"), "Turns shadows on or off. Shadows cost performance.", v.shadows);
        y += Metrics::kRowH + Metrics::kGap;
        // A qualidade (resolucao) do mapa de sombra fica so em "Lighting and shadows" (Light Prism).
        checkRow(rayTracing_, y, T("Enable ray tracing"), "Today it only affects CPU AO (slow). GPU ray tracing is not available yet.", v.rayTracing);
        y += Metrics::kRowH + Metrics::kGap;
        // Linha de status (so texto): mostra o estado real do ray tracing na GPU.
        text(Metrics::kPad, y, T(sm::RayTracingGpuStatusKey(v.gpuRayTracing)), Metrics::kSmallFont, textWidth());
        y += Metrics::kTextH + Metrics::kGap;
        checkRow(rtAo_, y, T("Ray-traced ambient occlusion (CPU)"), "Darkens corners and creases using rays computed on the CPU. Needs ray tracing.",
                 v.rayTracing && v.rtAo, v.rayTracing);
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

    // Registra a dica de uma opcao. (x, y, w, h) em coordenadas locais da janela (ja com a rolagem); `key` e o texto em
    // ingles (chave de traducao). O retangulo e recortado na area de conteudo visivel e convertido para a tela.
    void tip(float x, float y, float w, float h, const char* key) {
        if (!key || !*key) return;
        const float top = Metrics::kStripH, bottom = rect_.h;   // area de conteudo (local)
        const float y0 = std::max(y, top), y1 = std::min(y + h, bottom);
        if (y1 <= y0 || w <= 0.f) return;   // opcao rolada para fora da janela
        tips_.push_back({rect_.x + x, rect_.y + y0, w, y1 - y0, std::string(T(key))});
    }

    // Linha com rotulo a esquerda e combo a direita.
    void controlRow(Elm::Dropdown& dd, float y, const char* label, const char* tipKey = nullptr) {
        dd.border = rectOf(Metrics::kPad + labelW_, y, controlW_, Metrics::kRowH);
        text(Metrics::kPad, y + 6.f, label, Metrics::kFont, labelW_ - 6.f);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, tipKey);   // dica sobre a linha inteira
    }
    // Toggle (trilho arredondado + bolinha): o controle unico que substituiu as caixas de selecao, igual nos controles
    // fixos e nas linhas genericas. (x, y) e o canto do trilho; `on` liga; `enabled` false apaga; `hot` realca o contorno.
    // O clique continua vindo do Elm::Checkbox (estado e evento); so o desenho mudou. Desenha no canvas (z do controle).
    void drawToggle(float x, float rowY, bool on, bool enabled, bool hot) {
        constexpr float kW = 42.f, kH = 22.f, kKnob = 16.f;
        const float y = rowY + (Metrics::kRowH - kH) * 0.5f;
        const SU::Vec4 rim = hot && enabled ? theme_.accent : theme_.divider;
        const SU::Vec4 fill = !enabled ? theme_.surface : (on ? theme_.accent : btnColor_);
        const SU::Vec4 knob = enabled ? theme_.text : theme_.textDim;
        canvas_.addSolid(x, y, kW, kH, rim, 0.22f, kH * 0.5f);                           // contorno do trilho
        canvas_.addSolid(x + 1.5f, y + 1.5f, kW - 3.f, kH - 3.f, fill, 0.23f, (kH - 3.f) * 0.5f);   // preenchimento
        const float kx = on ? x + kW - kKnob - 3.f : x + 3.f;                            // bolinha: direita = ligado
        canvas_.addSolid(kx, y + (kH - kKnob) * 0.5f, kKnob, kKnob, knob, 0.25f, kKnob * 0.5f);
    }

    // Linha com rotulo a esquerda e toggle a direita. `on` e o valor mostrado (vem dos Values, sem atraso de um quadro).
    void checkRow(Elm::Checkbox& cb, float y, const char* label, const char* tipKey, bool on, bool enabled = true) {
        cb.border = rectOf(Metrics::kPad + labelW_, y, controlW_, Metrics::kRowH);
        canvas_.addText(Metrics::kPad, y + 6.f, label, Metrics::kFont, enabled ? theme_.text : theme_.textDim, labelW_ - 6.f);
        drawToggle(Metrics::kPad + labelW_, y, on, enabled, contains(cb.border, hover_));
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, tipKey);
    }

    // Tamanho personalizado: rotulo, valor e os botoes - e +. Cada clique muda kCustomStep (limites do modelo).
    void stepper(float y, const char* label, sc::Field field, int value, int minValue, int maxValue) {
        const float x = Metrics::kPad + labelW_;
        const float plusX = x + controlW_ - Metrics::kStepW;
        const float minusX = plusX - Metrics::kStepW - Metrics::kGap;
        text(Metrics::kPad, y + 6.f, label, Metrics::kFont, labelW_ - 6.f);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH,
            field == sc::Field::CustomW ? "Width of the game image in pixels (custom resolution)."
                                        : "Height of the game image in pixels (custom resolution).");
        text(x, y + 6.f, std::to_string(value), Metrics::kFont, minusX - x - Metrics::kGap);
        steps_.push_back({field, {minusX, y, Metrics::kStepW, Metrics::kRowH}, {plusX, y, Metrics::kStepW, Metrics::kRowH}, value,
                          minValue, maxValue});
        const Stepper& s = steps_.back();
        canvas_.addSolid(s.minus.x, s.minus.y, s.minus.w, s.minus.h, btnColor_, 0.2f, editor::ui_style::buttonRadius());
        canvas_.addSolid(s.plus.x, s.plus.y, s.plus.w, s.plus.h, btnColor_, 0.2f, editor::ui_style::buttonRadius());
        text(s.minus.x + 9.f, s.minus.y + 6.f, "-", Metrics::kFont, 12.f);
        text(s.plus.x + 8.f, s.plus.y + 6.f, "+", Metrics::kFont, 12.f);
    }

    // Quantas APIs graficas estao compiladas neste build.
    static int builtApis(const Values& v) {
        int n = 0;
        for (bool built : v.apiBuilt) n += built ? 1 : 0;
        return n;
    }

    // Titulo de secao dentro do conteudo.
    void heading(float y, const char* title) {
        canvas_.addText(Metrics::kPad, y + 4.f, title, Metrics::kFont + 1.f, headingColor_, textWidth());
    }

    // Texto simples no canvas (ignora texto vazio).
    void text(float x, float y, const std::string& s, float size, float maxW) {
        if (!s.empty()) canvas_.addText(x, y, s, size, theme_.text, maxW);
    }

    // Retangulo a partir de x, y, largura e altura.
    [[nodiscard]] static SU::Rect rectOf(float x, float y, float w, float h) {
        return {{x, y}, {w, h}};
    }

    [[nodiscard]] SU::Rect windowRect() const { return rectOf(0.f, 0.f, rect_.w, rect_.h); }

    // ---------- Textos dos elementos ----------
    // Texto do valor escolhido dentro de cada combo (a caixa do combo ja foi desenhada pelo elemento).
    // `graphics` true: combos da categoria Graphics and API; false: os da Quality.
    void dropdownTexts(bool graphics) {
        if (graphics) {
            dropdownValue(resolution_);
            dropdownValue(upscale_);
            dropdownValue(fps_);
        } else {
            dropdownValue(aa_);
        }
    }

    // Texto da opcao escolhida dentro da caixa do combo.
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

    // Textos da lista aberta do combo (so quando aberto).
    void popupTexts(const Elm::Dropdown& dd) {
        if (!dd.control.open) return;
        for (std::size_t i = 0; i < dd.control.options.size(); ++i) {
            const SU::Rect r = Elm::DropdownRowRect(dd, i);
            overlay_.addText(r.position.x + 8.f, r.position.y + 6.f, dd.control.options[i], Metrics::kFont, theme_.text,
                             r.size.x - 16.f);
        }
    }

    // Os 4 combos fixos da janela (somente leitura).
    [[nodiscard]] std::array<const Elm::Dropdown*, 4> allDropdowns() const {
        return {&resolution_, &upscale_, &aa_, &fps_};
    }
    // Os 4 combos fixos da janela (editaveis).
    [[nodiscard]] std::array<Elm::Dropdown*, 4> allDropdownsMutable() {
        return {&resolution_, &upscale_, &aa_, &fps_};
    }

    // Posiciona a lista aberta de cada combo dentro da janela.
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

    // Copia os valores atuais do Editor para os controles (API, resolucao, upscale, sombras...).
    void syncElements(const Values& v) {
        sb::SyncApiRadio(apis_, v.apiBuilt, v.api);
        sb::SyncDropdown(resolution_, resolutionOptions(), v.resMode);
        sb::SyncDropdown(upscale_, {T("Off (SDL linear)"), T("Bilinear"), T("Bicubic (Catmull-Rom)"), T("Lanczos2")}, v.upscale);
        sb::SyncDropdown(aa_, {T("Off"), T("FXAA")}, v.aa);
        sb::SyncDropdown(fps_, {T("VSync"), T("30 FPS"), T("60 FPS"), T("120 FPS"), T("Unlimited")}, v.fps);
        sb::SyncRenderScale(scale_, v.renderScale);
        sb::SyncCheckbox(pixel_, v.pixelFilter);
        sb::SyncCheckbox(shadows_, v.shadows);
        sb::SyncCheckbox(rayTracing_, v.rayTracing);
        sb::SyncCheckbox(rtAo_, v.rayTracing && v.rtAo);
        rayTracingEnabled_ = v.rayTracing;
        scaleEnabled_ = v.resMode == 0 && !v.scaleLocked;   // so vale com a resolucao de janela e sem FSR mandando na escala
        scale_.states.disabled = !scaleEnabled_;
    }

    // Opcoes do combo de resolucao do jogo.
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
        W::TextEditState edit;       // Text e Slider (campo do valor): edicao (texto e caret). Text sem foco espelha a janela
        std::string known;           // Text: ultimo valor recebido; o pedido so sai se o texto mudou. Slider: texto ao abrir
        std::string shown;           // Slider: texto do valor sem foco (ex.: "4 px")
        SU::Rect box;                // Text: o campo; Slider: caixa do valor; Check: a linha inteira (hover e clique)
        float lo = 0.f, hi = 1.f, step = 0.f;   // Slider: intervalo, passo e se e inteiro (valor digitado)
        bool isInt = false;
        sc::Field field = sc::Field::Form;      // Slider: pedido. Form (linha generica) usa id; os demais sao campos fixos
        int index = 0;                          // Slider de campo fixo: canal (cor personalizada: 0, 1 ou 2)
        bool hidden = false;                    // Slider de um grupo de cor: nao desenha nem recebe ponteiro (o seletor de cor o substitui)
        float typedLo = 0.f, typedHi = 0.f;     // Slider de tamanho de fonte: faixa do numero digitado (typedHi > typedLo = livre)
        float modelValue = 0.f;                 // Slider livre: valor real do modelo (o slider so mostra ate o fim da faixa)
    };

    // Grupo de cor (linhas R, G, B e, se houver, A seguidas): um seletor de cor no lugar dos sliders.
    struct ColorGroup {
        std::size_t section = 0, first = 0, count = 0;   // secao, primeira linha e numero de canais (3 ou 4)
        int ids[4] = {0, 0, 0, 0};                        // id de cada canal (pedido Field::Form)
        float rgba[4] = {0.f, 0.f, 0.f, 1.f};             // valor do modelo neste quadro
        SU::Rect box;                                     // amostra clicavel (janela)
        bool enabled = true;
    };

    // Linhas de uma secao, depois do conteudo fixo. Devolve o y final. Tres ou quatro linhas seguidas "(R) (G) (B) [(A)]"
    // viram uma so linha com o seletor de cor compartilhado (color_popup.hpp).
    float layoutForm(std::size_t i, const settings_form::Rows& rows, float y) {
        syncFormPool(i, rows);
        for (std::size_t k = 0; k < rows.size(); ++k) {
            const std::size_t n = colorGroupSize(rows, k);
            if (n != 0) {
                y = layoutColorGroup(i, k, n, rows, y);
                k += n - 1;
                continue;
            }
            y = layoutFormRow(i, k, rows[k], y);
        }
        return y;
    }

    // Quantas linhas (3 ou 4) formam um grupo de cor a partir de k, ou 0 se k nao abre um grupo.
    [[nodiscard]] static std::size_t colorGroupSize(const settings_form::Rows& rows, std::size_t k) {
        using settings_form::RowKind;
        auto has = [&](std::size_t j, const char* tag) {
            return j < rows.size() && rows[j].kind == RowKind::Slider && rows[j].label.find(tag) != std::string::npos;
        };
        if (!has(k, " (R)") || !has(k + 1, " (G)") || !has(k + 2, " (B)")) return 0;
        return has(k + 3, " (A)") ? 4 : 3;
    }

    // Linha de cor: rotulo, amostra com o hex (e o alfa) e clique para abrir o seletor. Os sliders do grupo ficam ocultos.
    float layoutColorGroup(std::size_t i, std::size_t k, std::size_t n, const settings_form::Rows& rows, float y) {
        ColorGroup g;
        g.section = i;
        g.first = k;
        g.count = n;
        g.enabled = rows[k].enabled;
        for (std::size_t j = 0; j < n; ++j) {
            FormSlot& s = slotFor(i, k + j, settings_form::RowKind::Slider);
            s.hidden = true;
            s.id = rows[k + j].id;
            s.box = SU::Rect{};
            s.slider.border = SU::Rect{};
            g.ids[j] = rows[k + j].id;
            g.rgba[j] = std::clamp(rows[k + j].value, 0.f, 1.f);
        }
        const bool live = colorPopup_.isOpen() && colorPopup_.key() == kFormColorKey && activeGroup_.section == i && activeGroup_.first == k;
        const float* c = live ? formRgba_ : g.rgba;
        std::string label = rows[k].label;
        label.erase(label.find(" (R)"), 4);
        const SU::Vec4 ink = g.enabled ? theme_.text : theme_.textDim;
        g.box = rectOf(Metrics::kPad + labelW_, y, controlW_, Metrics::kRowH);
        canvas_.addText(Metrics::kPad, y + 5.f, label, Metrics::kFont, ink, labelW_ - 8.f);
        const bool hot = g.enabled && contains(g.box, hover_);
        canvas_.addSolid(g.box.position.x, y, g.box.size.x, g.box.size.y, hot ? theme_.accent : btnColor_, 0.2f, editor::ui_style::buttonRadius());
        canvas_.addSolid(g.box.position.x + 6.f, y + 5.f, 26.f, Metrics::kRowH - 10.f, SU::Vec4{c[0], c[1], c[2], n == 4 ? c[3] : 1.f}, 0.24f);
        std::string text = im::HexText(c);
        if (n == 4) text += "   A " + std::to_string(static_cast<int>(c[3] * 100.f + 0.5f)) + "%";
        canvas_.addText(g.box.position.x + 40.f, y + 5.f, text, Metrics::kFont, ink, g.box.size.x - 46.f);
        colorGroups_.push_back(g);
        tip(Metrics::kPad, y, textWidth(), Metrics::kRowH, rowTip(rows[k]));   // dica do grupo de cor (primeira linha)
        return y + Metrics::kRowH + Metrics::kGap;
    }

    // Clique na amostra de um grupo de cor (secao aberta): abre o seletor ali. Devolve true se abriu.
    bool colorGroupPointer(const SU::Vec2& p) {
        for (const ColorGroup& g : colorGroups_) {
            if (!g.enabled || !open_[g.section] || !contains(g.box, p)) continue;
            activeGroup_ = g;
            for (int j = 0; j < 4; ++j) formRgba_[j] = g.rgba[j];
            const float total = Metrics::kColorPopupH + color_popup::ColorPopup::kExtraH;
            float y = g.box.position.y + g.box.size.y + 4.f;
            if (y + total > rect_.h) y = std::max(0.f, g.box.position.y - total - 4.f);
            const float x = std::min(g.box.position.x, std::max(0.f, rect_.w - Metrics::kColorPopupW - 2.f));
            colorPopup_.open(kFormColorKey, formRgba_, SU::Rect{{x, y}, {Metrics::kColorPopupW, Metrics::kColorPopupH}},
                             g.count == 4 ? &formRgba_[3] : nullptr);
            return true;
        }
        return false;
    }

    // O seletor mudou a cor de um grupo: um pedido por canal (R, G, B e o alfa quando o grupo o tem).
    void formColorPicked() {
        for (std::size_t j = 0; j < activeGroup_.count; ++j) pending_.push_back(formChange(activeGroup_.ids[j], formRgba_[j], false, 0, ""));
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

    // Dica da linha: Row::tip se houver, senao a dica padrao do id (settings_form::DefaultTip). nullptr = sem dica.
    [[nodiscard]] static const char* rowTip(const settings_form::Row& row) {
        return row.tip.empty() ? settings_form::DefaultTip(row.id) : row.tip.c_str();
    }

    // Posiciona uma linha generica e registra a dica sobre a linha inteira (rotulo + controle).
    float layoutFormRow(std::size_t i, std::size_t k, const settings_form::Row& row, float y) {
        const float y0 = y;
        const float next = layoutFormRowImpl(i, k, row, y);
        using settings_form::RowKind;
        if (row.kind != RowKind::Heading && row.kind != RowKind::Note && row.kind != RowKind::Gap)
            tip(Metrics::kPad, y0, textWidth(), next - y0 - Metrics::kGap, rowTip(row));
        return next;
    }

    // Posiciona uma linha generica do formulario (slider, combo, campo, cor...) e devolve o Y seguinte.
    float layoutFormRowImpl(std::size_t i, std::size_t k, const settings_form::Row& row, float y) {
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
                s.hidden = false;
                s.slider.range = Elm::SliderRange{row.lo, row.hi, asInt ? 1.f : 0.f, asInt};
                s.slider.SetValue(settings_form::ClampSliderValue(row.value, row.lo, row.hi, asInt));
                s.slider.states.disabled = !row.enabled;
                s.slider.border = rectOf(x, y, controlW_ - Metrics::kValueW, Metrics::kRowH);
                canvas_.addText(Metrics::kPad, y + 5.f, row.label, Metrics::kFont, ink, labelW_ - 8.f);
                s.lo = row.lo;
                s.hi = row.hi;
                s.step = asInt ? 1.f : 0.f;
                s.isInt = asInt;
                const std::string shown = row.note.empty() ? Elm::SliderValueText(s.slider) : row.note;
                layoutValueBox(s, i, k, y, shown);
                return y + Metrics::kRowH + Metrics::kGap;
            }
            case RowKind::Check: {
                s.check.checked = row.on;
                s.check.states.disabled = !row.enabled;
                s.box = rectOf(Metrics::kPad, y, textWidth(), Metrics::kRowH);
                s.check.border = rectOf(x, y, controlW_, Metrics::kRowH);
                // Toggle (mesmo desenho dos controles fixos): o hover realca o contorno do trilho, sem pintar a linha.
                canvas_.addText(Metrics::kPad, y + 6.f, row.label, Metrics::kFont, ink, labelW_ - 6.f);
                drawToggle(x, y, row.on, row.enabled, contains(s.box, hover_));
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

    // Campo de texto com foco, ou nullptr. Os sliders fixos de Appearance usam a secao kFixedSection.
    [[nodiscard]] FormSlot* focusedSlot() {
        if (focusSection_ == kFixedSection) return focusIndex_ < kFixedCount ? &fixed_[focusIndex_] : nullptr;
        if (focusSection_ == npos || focusIndex_ >= formPool_[focusSection_].size()) return nullptr;
        return formPool_[focusSection_][focusIndex_].get();
    }

    // Digitar o valor de um slider: o texto vira o valor do Slider (mesmos limites e passo do arraste) e so grava
    // quando o valor muda. Texto invalido nao muda nada.
    void commitSliderEdit(FormSlot& s) {
        if (s.typedHi > s.typedLo) {   // tamanho de fonte: o numero digitado vale alem da faixa do slider (prendido por seguranca)
            float typedFree = 0.f;
            if (slider_edit::ParseTypedNumber(s.edit.Text(), typedFree)) {
                typedFree = std::clamp(typedFree, s.typedLo, s.typedHi);
                if (std::fabs(typedFree - s.modelValue) > 1e-4f) {
                    s.slider.SetValue(std::clamp(typedFree, s.lo, s.hi));
                    pending_.push_back(sc::Change{s.field, s.index, typedFree, false});
                }
            }
            return;
        }
        const float typed = slider_edit::CommitSliderText(s.edit.Text(), s.slider.Value(), s.lo, s.hi, s.step, s.isInt);
        if (s.slider.SetValue(typed)) sliderChanged(s);
    }

    // Cor do seletor mudou: um pedido por canal (index = slot * 3 + canal), o mesmo caminho dos outros campos.
    void customPicked(const float rgb[3]) {
        const int slot = std::clamp(colorSlot_, 0, 12);
        for (int ch = 0; ch < 3; ++ch) pending_.push_back(sc::Change{sc::Field::CustomColor, slot * 3 + ch, rgb[ch], false});
    }

    // Clique na amostra: o seletor abre abaixo dela (ou acima, se nao couber), com a cor atual do modelo.
    void openCustomPicker() {
        for (std::size_t ch = 0; ch < 3; ++ch) customRgb_[ch] = customModel_[ch];
        float y = customSwatch_.position.y + customSwatch_.size.y + 4.f;
        const float total = Metrics::kColorPopupH + color_popup::ColorPopup::kExtraH;
        if (y + total > rect_.h) y = std::max(0.f, customSwatch_.position.y - total - 4.f);
        const float x = std::min(customSwatch_.position.x, std::max(0.f, rect_.w - Metrics::kColorPopupW - 2.f));
        colorPopup_.open(0u, customRgb_, SU::Rect{{x, y}, {Metrics::kColorPopupW, Metrics::kColorPopupH}});
    }

    // Visual do popup de cor a partir das cores da janela.
    [[nodiscard]] color_popup::Look colorLook() const {
        color_popup::Look look;
        look.frame.cornerRadius = editor::ui_style::windowRadius();
        look.frame.background = panelColor_;
        look.frame.border = theme_.divider;
        look.field = btnColor_;
        look.fieldFocus = theme_.surface;
        look.text = theme_.text;
        look.selection = theme_.accent;
        look.font = Metrics::kFont;
        look.fieldFont = editor::ui_style::uiFont(Metrics::kFont);
        look.zPanel = Metrics::kPopupZ;
        return look;
    }

    // Um slider mudou (arraste ou digitado): o pedido segue o mesmo caminho para os dois casos.
    void sliderChanged(FormSlot& s) {
        const float value = s.slider.Value();
        if (s.field == sc::Field::Form) {
            pending_.push_back(formChange(s.id, value, false, 0, ""));
            return;
        }
        pending_.push_back(sc::Change{s.field, s.index, value, false});
    }

    // Clique no campo do valor abre a digitacao; o texto de partida e o valor atual (Esc volta a ele).
    void focusSlider(std::size_t i, std::size_t k, FormSlot& s) {
        if (isFocused(i, k)) return;
        commitText();
        s.known = Elm::SliderValueText(s.slider);
        if (s.typedHi > s.typedLo) {   // livre: o texto de partida e o valor real do modelo, mesmo alem do fim do slider
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", static_cast<double>(s.modelValue));
            s.known = buf;
        }
        s.edit = W::TextEditState(s.known);
        focusText(i, k, s);
    }

    // Ponteiro de um slider (linha generica ou fixo): Down na caixa do valor abre a digitacao; o arraste de sempre
    // continua na faixa. Devolve true se o slider consumiu o evento.
    bool sliderPointer(std::size_t i, std::size_t k, FormSlot& s, Elm::PointerPhase phase, const SU::Vec2& p) {
        if (s.hidden) return false;   // slider de grupo de cor: quem responde e o seletor
        if (phase == Elm::PointerPhase::Down && !s.slider.states.disabled && contains(s.box, p)) {
            focusSlider(i, k, s);
            return true;
        }
        const Elm::SliderEvents ev = s.slider.OnPointer(event(phase, p, s.slider.border));
        if (ev.valueChanged) sliderChanged(s);
        return ev.pressStarted || ev.valueChanged || ev.pressEnded;
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
        if (s->kind == settings_form::RowKind::Slider) {
            commitSliderEdit(*s);
        } else if (s->edit.Text() != s->known) {
            pending_.push_back(formChange(s->id, 0.f, false, 0, std::string(s->edit.Text())));
        }
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

    // Percorre todas as linhas genericas (de todas as secoes) chamando f.
    template <class F>
    void forEachFormSlot(F&& f) {
        for (auto& pool : formPool_) {
            for (auto& s : pool) f(*s);
        }
    }

    // Algum combo fixo esta aberto?
    [[nodiscard]] bool graphicsDropdownOpen() const {
        for (const Elm::Dropdown* dd : allDropdowns()) {
            if (dd->control.open) return true;
        }
        return false;
    }

    // Combo aberto de alguma linha generica, ou nullptr.
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

    // Trata o ponteiro numa linha generica conforme o tipo (slider, combo, campo, cor...); true se consumiu.
    bool formSlotPointer(std::size_t i, std::size_t k, FormSlot& s, Elm::PointerPhase phase, const SU::Vec2& p) {
        using settings_form::RowKind;
        switch (s.kind) {
            case RowKind::Slider:
                return sliderPointer(i, k, s, phase, p);
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
                        if (s.hidden) break;
                        append(items, paint, Elm::BuildSliderItems(s.slider, sliderStyle_, z, paint), kContentClip);
                        break;
                    case RowKind::Check:   // toggle: ja desenhado no canvas (drawToggle), nada a acrescentar aqui
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
    // `graphics` true: controles da categoria Graphics and API (API, resolucao, escala, upscale, FPS);
    // false: os da Quality (AA e qualidade de sombra). Os toggles sao desenhados no canvas (drawToggle).
    void appendControls(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, bool graphics) {
        constexpr float z = Metrics::kControlZ;
        if (graphics) append(items, paint, Elm::BuildRadioGroupItems(apis_, radioStyle_, z, paint), kContentClip);
        for (const Elm::Dropdown* dd : allDropdowns()) {
            if (isGraphicsDropdown(dd) != graphics) continue;
            // Hover: o combo sob o ponteiro pinta com a cor de destaque do tema (amarelo no tema Ember, por exemplo).
            Elm::DropdownStyle style = dropdownStyle_;
            if (dd->border.Contains(hover_) && !dd->control.disabled) style.button = theme_.accent;
            append(items, paint, Elm::BuildDropdownButtonItems(*dd, style, z, paint), kContentClip);
        }
        if (graphics) append(items, paint, Elm::BuildSliderItems(scale_, sliderStyle_, z, paint), kContentClip);
    }

    // O combo e da categoria Graphics and API (resolucao, upscale, FPS)? Os demais (AA, qualidade de sombra) sao da Quality.
    [[nodiscard]] bool isGraphicsDropdown(const Elm::Dropdown* dd) const { return dd == &resolution_ || dd == &upscale_ || dd == &fps_; }

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

    // Entrada unica do ponteiro (mover/apertar/soltar): fecha menus, trata abas, secoes, barra de rolagem e conteudo. true = consumiu.
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
        if (colorPopup_.isOpen()) {   // seletor de cor aberto: a caixa dele recebe o ponteiro primeiro
            if (colorPopup_.contains(p) || colorPopup_.dragging()) {   // arrasto em curso segue mesmo fora da caixa
                colorPopup_.pointer(phase, p, window_);
                return true;
            }
            if (phase == Elm::PointerPhase::Down) colorPopup_.close(window_);
        }
        if (phase == Elm::PointerPhase::Down && palette_ == static_cast<int>(editor::palette::kCustomIndex) && contains(customSwatch_, p)) {
            openCustomPicker();
            return true;
        }
        if (phase == Elm::PointerPhase::Down && colorGroupPointer(p)) return true;   // amostra de cor das linhas (Sol, Ceu...)
        if (phase == Elm::PointerPhase::Up && sectionPointer(p)) return true;
        if (phase == Elm::PointerPhase::Up && paletteRowPointer(p)) return true;
        if (appearanceSliderPointer(phase, p)) return true;
        if (phase == Elm::PointerPhase::Down && scrollbarPointer(p)) return true;
        if (formPointer(phase, p)) return true;
        if (!open_[kGraphicsTab] && !open_[kQualityTab]) return true;
        if (open_[kGraphicsTab] && phase == Elm::PointerPhase::Down && stepperClick(p)) return true;
        contentPointer(phase, p);
        return true;
    }

    // Solta os controles que ficaram "pressionados" (botao solto fora do painel ou sem botao no movimento).
    // So entrega o Up aos controles: nao abre secoes, paletas nem passos, porque o clique nao foi nelas.
    void releasePointer(float x, float y) {
        const SU::Vec2 p{x - rect_.x, y - rect_.y};
        if (colorPopup_.isOpen()) colorPopup_.pointer(Elm::PointerPhase::Up, p, window_);
        if (appearanceSliderPointer(Elm::PointerPhase::Up, p)) return;
        if (formPointer(Elm::PointerPhase::Up, p)) return;
        if (open_[kGraphicsTab] || open_[kQualityTab]) contentPointer(Elm::PointerPhase::Up, p);
    }

    // Clique no cabecalho de secao: abre ou fecha.
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

    // Clique numa linha da paleta de cores: pede a troca de tema.
    bool paletteRowPointer(const SU::Vec2& p) {
        for (std::size_t k = 0; k <= editor::palette::kCustomIndex; ++k) {
            if (paletteRows_[k].contains(p.x, p.y)) {
                pending_.push_back({sc::Field::Palette, static_cast<int>(k), 0.f, false});
                return true;
            }
        }
        return customPointer(p);
    }

    // Cliques da paleta Custom (cor-base, token < e >) e das linhas de tema. Mudanca de cor pelos sliders vai no arrasto.
    bool customPointer(const SU::Vec2& p) {
        for (std::size_t b = 0; b < editor::palette::kBaseCount; ++b) {
            if (baseBtns_[b].contains(p.x, p.y)) { colorSlot_ = static_cast<int>(b); return true; }
        }
        const int tokens = static_cast<int>(editor::palette::kTokenCount);
        if (tokenPrev_.contains(p.x, p.y)) { colorSlot_ = 5 + (colorSlot_ >= 5 ? (colorSlot_ - 5 + tokens - 1) % tokens : tokens - 1); return true; }
        if (tokenNext_.contains(p.x, p.y)) { colorSlot_ = 5 + (colorSlot_ >= 5 ? (colorSlot_ - 5 + 1) % tokens : 0); return true; }
        for (std::size_t k = 0; k < themeRows_.size(); ++k) {
            if (themeRows_[k].contains(p.x, p.y)) {
                pending_.push_back({sc::Field::ThemeFile, static_cast<int>(k), 0.f, false});
                return true;
            }
        }
        return false;
    }

    // Setas e abas da faixa: rola a faixa ou escolhe a aba.
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
    // rolavel). Fora da janela, a roda nao e consumida (false).
    bool wheel(float x, float y, float dy) {
        if (!inside(x, y)) return false;
        const float delta = -dy * Metrics::kWheelStep;
        if (y - rect_.y < Metrics::kStripH) {
            tabs_.scrollX += delta;
            clampStrip();
            return true;
        }
        scroll_.scrollOffset.y += delta;
        scroll_.ClampScroll();
        return true;
    }

    // Clique na barra de rolagem do conteudo.
    bool scrollbarPointer(const SU::Vec2& p) {
        if (!sbar_.pointerDown(p.x, p.y, scroll_.scrollOffset.y)) return false;
        scroll_.ClampScroll();
        return true;
    }

    // Arraste do polegar da barra de rolagem (continua mesmo com o mouse fora).
    bool dragScrollbar(float x, float y) {
        if (sbar_.pointerMove(x - rect_.x, y - rect_.y, scroll_.scrollOffset.y)) scroll_.ClampScroll();
        return true;
    }

    // Ponteiro no conteudo: com combo aberto so ele recebe; senao radios, checkboxes, steppers e linhas genericas.
    void contentPointer(Elm::PointerPhase phase, const SU::Vec2& p) {
        // Combo aberto: a lista cobre os controles de baixo, entao so ele recebe o ponteiro.
        for (const DropdownSlot& slot : dropdownSlots()) {
            if (!slot.dd->control.open) continue;
            applyDropdown(*slot.dd, phase, p, slot.field);
            return;
        }
        if (open_[kGraphicsTab]) {   // categoria Graphics and API
            const Elm::RadioEvents radio = apis_.OnPointer(event(phase, p, apis_.border));
            if (radio.valueChanged) pending_.push_back({sc::Field::Api, sb::ReadApiRadio(apis_), 0.f, false});
            applyDropdown(resolution_, phase, p, sc::Field::ResMode);
            if (scaleEnabled_) {   // com o FSR ligado a escala e do FSR: o slider nao recebe o ponteiro
                const Elm::SliderEvents slide = scale_.OnPointer(event(phase, p, scale_.border));
                if (slide.valueChanged) pending_.push_back({sc::Field::RenderScale, 0, scale_.Value(), false});
            }
            applyDropdown(upscale_, phase, p, sc::Field::Upscale);
            applyDropdown(fps_, phase, p, sc::Field::Fps);
        }
        if (open_[kQualityTab]) {   // categoria Quality
            applyDropdown(aa_, phase, p, sc::Field::Aa);
            applyCheck(pixel_, phase, p, sc::Field::PixelFilter);
            applyCheck(shadows_, phase, p, sc::Field::Shadows);
            applyCheck(rayTracing_, phase, p, sc::Field::RayTracing);
            if (rayTracingEnabled_) applyCheck(rtAo_, phase, p, sc::Field::RtAo);
        }
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

    // Aplica o evento do combo e, se o valor mudou, enfileira o pedido.
    void applyDropdown(Elm::Dropdown& dd, Elm::PointerPhase phase, const SU::Vec2& p, sc::Field field) {
        const Elm::DropdownEvents ev = dropdownEvents(dd, phase, p);
        if (ev.opened) closeOthers(&dd);
        if (ev.valueChanged) pending_.push_back({field, static_cast<int>(dd.control.selectedIndex), 0.f, false});
    }

    // Aplica o evento da caixa de marcar e enfileira o pedido se mudou.
    void applyCheck(Elm::Checkbox& cb, Elm::PointerPhase phase, const SU::Vec2& p, sc::Field field) {
        const Elm::CheckboxEvents ev = cb.OnPointer(event(phase, p, cb.border));
        if (ev.valueChanged) pending_.push_back({field, 0, 0.f, cb.checked});
    }

    // Ponto dentro do retangulo.
    [[nodiscard]] static bool contains(const SU::Rect& box, const SU::Vec2& p) {
        return p.x >= box.position.x && p.x < box.position.x + box.size.x && p.y >= box.position.y &&
               p.y < box.position.y + box.size.y;
    }

    // Monta o evento de ponteiro relativo a caixa do controle.
    [[nodiscard]] static Elm::PointerEvent event(Elm::PointerPhase phase, const SU::Vec2& p, const SU::Rect& box) {
        return Elm::PointerEvent{phase, p, 0u, contains(box, p)};
    }

    // Clique em - ou + de algum stepper.
    bool stepperClick(const SU::Vec2& p) {
        for (Stepper& s : steps_) {
            if (sui::Box{s.minus.x, s.minus.y, s.minus.w, s.minus.h}.contains(p.x, p.y)) return step(s, -Metrics::kCustomStep);
            if (sui::Box{s.plus.x, s.plus.y, s.plus.w, s.plus.h}.contains(p.x, p.y)) return step(s, Metrics::kCustomStep);
        }
        return false;
    }

    // Soma delta ao valor (preso nos limites) e enfileira o pedido se mudou.
    bool step(const Stepper& s, int delta) {
        const int next = std::clamp(s.value + delta, s.minValue, s.maxValue);
        if (next != s.value) pending_.push_back({s.field, next, 0.f, false});
        return true;
    }

    // Fecha todos os combos exceto `keep` (so um aberto por vez).
    void closeOthers(const Elm::Dropdown* keep) {
        for (const DropdownSlot& slot : dropdownSlots()) {
            if (slot.dd != keep) slot.dd->control.Close();
        }
        forEachFormSlot([&](FormSlot& s) {
            if (s.kind == settings_form::RowKind::Dropdown && &s.dropdown != keep) s.dropdown.control.Close();
        });
    }

    // Lista dos 4 combos fixos com o campo que cada um altera.
    [[nodiscard]] std::array<DropdownSlot, 4> dropdownSlots() {
        return {{{&resolution_, sc::Field::ResMode},
                 {&upscale_, sc::Field::Upscale},
                 {&aa_, sc::Field::Aa},
                 {&fps_, sc::Field::Fps}}};
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
    std::array<sui::Box, editor::palette::kCount + 1> paletteRows_{};        // linhas de tema (coordenadas locais)
    SU::Vec4 panelColor_ = sui::PaletteColor(palette::active().panel);
    SU::Vec4 headingColor_ = sui::PaletteColor(palette::active().scroll);
    SU::Vec4 btnColor_ = sui::PaletteColor(editor::theme_colors::Active().btn);
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
    std::vector<Tip> tips_;   // dicas do quadro (refeitas a cada layoutSections)
    // Cor personalizada: a linha mostra o modelo (customModel_); o seletor edita a copia customRgb_ e avisa cada mudanca.
    SU::Rect customSwatch_{};
    float customModel_[3] = {0.f, 0.f, 0.f};
    float customRgb_[3] = {0.f, 0.f, 0.f};
    // Cores das linhas genericas (Sol, Ceu, Ambiente...): grupos do quadro, o grupo aberto no seletor e a copia editada (RGBA).
    static constexpr std::uint32_t kFormColorKey = 1u;
    std::vector<ColorGroup> colorGroups_;
    ColorGroup activeGroup_;
    float formRgba_[4] = {0.f, 0.f, 0.f, 1.f};
    color_popup::ColorPopup colorPopup_{canvas_, overlay_};
    std::array<std::vector<std::unique_ptr<FormSlot>>, sm::kTabCount> formPool_{};   // controles das linhas, por secao
    std::size_t focusSection_ = npos;   // campo de texto com foco (secao e linha); npos = nenhum
    std::size_t focusIndex_ = npos;
    SDL_Window* window_ = nullptr;      // janela do ultimo clique: liga e desliga o texto do SDL

    W::UIScrollViewComponent scroll_;
    ui_scrollbar::Scrollbar sbar_;

    Elm::TabBar tabs_;
    Elm::RadioGroup apis_;
    Elm::Dropdown resolution_;
    Elm::Slider scale_;
    // Sliders fixos (Appearance): raios e escala do texto. Cada um tem a caixa do valor digitavel. Ficam fora das
    // secoes de linhas; o foco deles usa a secao kFixedSection.
    static constexpr std::size_t kFixedSection = npos - 1;
    static constexpr std::size_t kFixedButtonRadius = 0, kFixedWindowRadius = 1, kFixedTextScale = 2;
    static constexpr std::size_t kFixedCount = 3;
    std::array<FormSlot, kFixedCount> fixed_;
    std::array<sui::Box, editor::palette::kBaseCount> baseBtns_{};   // cores-base (botoes)
    sui::Box tokenPrev_{}, tokenNext_{};                             // lista avancada (< e >)
    std::vector<sui::Box> themeRows_;                                // arquivos de tema (clique aplica)
    int colorSlot_ = 0;                                              // cor editada: 0..4 base, 5..12 token
    int palette_ = 0;                                                // paleta em uso neste quadro (para desenhar os sliders)
    bool appearanceOpen_ = false;
    Elm::Dropdown upscale_;
    Elm::Dropdown aa_;
    Elm::Checkbox pixel_;
    Elm::Dropdown fps_;
    Elm::Checkbox shadows_;
    Elm::Checkbox rayTracing_;
    Elm::Checkbox rtAo_;

    Elm::TabBarStyle tabStyle_;
    SU::Vec2 hover_{-1.f, -1.f};   // ultima posicao do ponteiro (coordenadas da janela), para o hover
    pointer_capture::PointerCapture capture_;   // botao esquerdo segurado dentro do painel (ver pointer_capture.hpp)
    Elm::RadioStyle radioStyle_;
    Elm::DropdownStyle dropdownStyle_;
    Elm::SliderStyle sliderStyle_;
    Elm::CheckboxStyle checkStyle_;
    Elm::ButtonStyle buttonStyle_;
};

}  // namespace editor::settings_panel
