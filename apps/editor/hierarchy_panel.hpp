#pragma once

// Janela Hierarchy feita com SpriteUI.
//
// A arvore, a selecao e a expansao vem de hierarchy_model.hpp. Este arquivo faz so o desenho (linhas, icones,
// botoes, rolagem), o clique, o arraste e as teclas. O que precisa mexer na cena (criar, apagar, renomear, mudar
// o pai) nao e feito aqui: sai em Requests e o main.cpp aplica.
//
// Exemplo: arrastar a Arma sobre o Player faz a Arma virar filha do Player (a posicao no mundo fica a mesma).
// Soltar a Arma num espaco vazio abaixo das linhas a tira do Player.
//
// Como funciona:
//   draw:        sincroniza o modelo com a lista do quadro e desenha so as linhas da faixa visivel.
//   handleEvent: clique (seta, olho, cadeado, selecao, arraste), roda do mouse, teclas e texto do renomear.
//                Os pedidos ficam em pending_ e saem no proximo draw.
//
// Teclas so sao consumidas com o foco na janela (clique dentro).
//
// Icones: a bolinha do tipo, o olho (V) e o cadeado (L) sao sprites de um atlas. Os PNGs assets/icons/eye.png e
// lock.png sobem uma vez, na primeira vez que a janela desenha, e todos os icones saem num lote so.
// Cor cheia = ligado; cor apagada = desligado. Exemplo: com a Parede oculta, o olho fica apagado; com a Parede
// travada, o cadeado fica laranja.

#include "window_palette.hpp"
#include "theme_colors.hpp"
#include "console_model.hpp"   // LineEdit: o campo de renomear usa o mesmo editor de linha do Console
#include "hierarchy_model.hpp"
#include "ui_scrollbar.hpp"
#include "i18n.hpp"
#include "image_io.hpp"
#include "sui_panel.hpp"
#include "ui_style.hpp"
#include "window_frame.hpp"
#include "window_help_icon.hpp"

#include "SpriteUi/Rendering/Sprite.hpp"
#include "SpriteUi/Widgets/DragDrop.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <optional>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace editor::hierarchy_panel {

namespace hm = editor::hierarchy_model;
namespace Dd = ToolEngine::SpriteUi::Widgets;
namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace cm = editor::console_model;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace wf = editor::window_frame;

// Acoes que o painel pede ao Editor. Algumas usam id (o objeto), outras parent (novo pai), kind (menu Create) ou text.
// Com id de linha de elemento de UI (hm::IsElemId), Select/Rename/Duplicate/Delete/MoveUp/MoveDown valem para o elemento.
// AddElem: id = canvas alvo (NoId = o canvas selecionado), kind = tipo do elemento (indice em hm::kElemKindNames).
enum class Act : std::uint8_t { Select, Activate, Reparent, Detach, ToggleHidden, ToggleLocked, Rename, Duplicate, Delete, Create,
                                MoveUp, MoveDown, AddElem, EditIndividual };   // EditIndividual: isola o objeto na janela Node

// Pedido da janela ao Editor (selecionar, renomear, apagar, criar...); o main.cpp aplica.
struct Action {
    Act act;
    hm::Id id;
    hm::Id parent;   // Reparent: novo pai
    int kind;        // Create: indice em kCreateLabels
    std::string text;   // Rename: novo nome

    Action(Act a, hm::Id i = hm::NoId, hm::Id p = hm::NoId, int k = 0, std::string t = {})
        : act{a}, id{i}, parent{p}, kind{k}, text{std::move(t)} {}
};
// Fila de pedidos entregue ao host a cada quadro.
using Requests = std::vector<Action>;

// O que a janela mostra: a lista de objetos deste quadro e a selecao do Editor.
struct Source {
    std::vector<hm::Item> items;
    hm::Id selected = hm::NoId;
};

// Itens do menu "+ Create", na ordem exibida. O main.cpp cria o objeto pelo indice.
inline constexpr const char* kCreateLabels[] = {"Empty object", "Cube", "Sphere", "Plane", "Camera",
                                                "Main Camera (Principal)", "Global Light", "Point Light", "Audio Source", "Canvas"};
inline constexpr int kCreateCount = static_cast<int>(sizeof(kCreateLabels) / sizeof(kCreateLabels[0]));   // a lista e a unica fonte

// Medidas fixas da janela (alturas de linha, recuos, fontes e camadas Z dos menus).
struct Metrics {
    static constexpr float kToolbarH = 30.f;
    static constexpr float kToolbarBtnH = 22.f;
    static constexpr float kToolbarBtnFont = 11.f;
    static constexpr float kRowH = 22.f;          // linha dos menus (Create e contexto)
    static constexpr float kListRowH = 28.f;      // linha da lista de objetos (era 22: maior para ler melhor)
    static constexpr float kListFont = 15.f;      // texto da lista de objetos (era 12)
    static constexpr float kIndent = 14.f;        // recuo por nivel da arvore
    static constexpr float kPad = sui::kPanelPadding;
    static constexpr float kExpander = 12.f;      // area do sinal + / -
    static constexpr float kIcon = 12.f;
    static constexpr float kToggle = 18.f;        // olho V (visivel) e cadeado L (travado), em px
    static constexpr float kToggleGap = 4.f;
    static constexpr float kScrollLane = 10.f;    // faixa da barra de rolagem a direita
    static constexpr float kBtnPad = 10.f;
    static constexpr float kBtnGap = 6.f;
    static constexpr float kMenuW = 200.f;
    static constexpr float kContextMenuW = 190.f;
    static constexpr float kSubMenuW = 150.f;     // submenu "Add element" (tipos de elemento de UI)
    static constexpr float kMenuZ = 0.95f;       // fundo do menu: acima de linhas, selecao e texto da lista
    static constexpr float kMenuHoverZ = 0.955f; // destaque da linha: acima do fundo do menu
    static constexpr float kMenuTextZ = 0.97f;   // texto do menu: acima do fundo e do destaque
    static constexpr float kFont = 12.f;
    static constexpr float kSmallFont = 11.f;
    static constexpr float kWheelRows = 3.f;      // linhas por giro da roda
    static constexpr std::uint64_t kDoubleClickMs = 400;
};

// Cores da lista: da paleta em uso (theme_colors.hpp), refeitas a cada quadro em draw(). Ficam fixos: dropOn (verde de
// alvo de soltar), lockOn (ambar do cadeado) e as cores de icone por tipo (identidade do tipo, lida em qualquer tema).
struct Colors {
    SU::Vec4 rowSel, rowHover, btn, toggleOn, toggleOff, scroll, menu, field, caret;
    SU::Vec4 dropOn = sui::Srgb(0.20f, 0.45f, 0.30f);
    SU::Vec4 lockOn = sui::Srgb(0.95f, 0.70f, 0.30f);
    Colors() {
        const editor::theme_colors::Colors t = editor::theme_colors::Active();
        rowSel = sui::PaletteColor(t.rowSel);
        rowHover = sui::PaletteColor(t.rowHover);
        btn = sui::PaletteColor(t.btn);
        toggleOn = sui::PaletteColor(t.text);
        toggleOff = sui::PaletteColor(t.border);
        scroll = sui::PaletteColor(t.scroll);
        menu = sui::PaletteColor(t.surface);
        field = sui::PaletteColor(t.field);
        caret = sui::PaletteColor(t.caret);
    }
    // Cor do icone por tipo, na ordem de hm::Kind.
    SU::Vec4 kinds[7] = {sui::Srgb(0.62f, 0.68f, 0.78f), sui::Srgb(0.42f, 0.85f, 0.52f), sui::Srgb(1.f, 0.85f, 0.35f),
                         sui::Srgb(0.85f, 0.55f, 0.95f), sui::Srgb(1.f, 0.6f, 0.3f), sui::Srgb(0.35f, 0.95f, 0.7f),
                         sui::Srgb(0.55f, 0.80f, 0.95f)};
};

// Botoes da barra do topo.
enum class Button : std::uint8_t { None, Create, Duplicate, Delete };

// Atlas de icones: celulas de kIconCell px, lado a lado. A ordem e a do IconCell.
inline constexpr int kIconCell = 32;
inline constexpr int kIconCellCount = 6;
inline constexpr int kAtlasW = kIconCell * kIconCellCount;
enum IconCell : int { kDotCell, kEyeCell, kLockCell, kSquareCell, kTriangleCell, kRingCell };
inline constexpr const char* kEyePath = "assets/icons/eye.png";
inline constexpr const char* kLockPath = "assets/icons/lock.png";

// Janela Hierarchy: arvore de objetos da cena com selecao, arrastar para reparentar, renomear, olho/cadeado, menu "+ Create" e menu de contexto. Mostra so as linhas visiveis.
class HierarchyPanel {
 public:
    HierarchyPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
        : toolbar_{text}, list_{text}, overlay_{text}, chrome_{text}, sdl_{sdl} {
        // Menus (+ Create e o do botao direito) ficam acima do texto da lista. Sem isso, o texto das linhas
        // aparecia por cima do menu e as duas listas se misturavam.
        overlay_.setTextZ(Metrics::kMenuTextZ);
        // Moldura: mesmo visual do Console e do Profiler.
    }
    HierarchyPanel(const HierarchyPanel&) = delete;
    HierarchyPanel& operator=(const HierarchyPanel&) = delete;

    // Pedido do X da moldura (lido e zerado pelo host).
    bool consumeClose() {
        const bool v = closeRequested_;
        closeRequested_ = false;
        return v;
    }
    // "?" da moldura: o host abre a Help na secao da Hierarchy.
    bool consumeHelp() {
        const bool v = helpRequested_;
        helpRequested_ = false;
        return v;
    }

    // true quando o evento e da janela 
    bool handleEvent(const SDL_Event& e) {
        if (!visible_) return false;
        // Moldura primeiro: X fecha, so o grip arrasta. Soltar o botao encerra o arraste.
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            const wf::Event ev = frame_.buttonAt({e.button.x, e.button.y});
            if (ev == wf::Event::Close) closeRequested_ = true;
            if (ev == wf::Event::Help) helpRequested_ = true;
            if (ev != wf::Event::None) return true;
        }
        switch (e.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) return onDown(e.button.x, e.button.y, e.button.timestamp / 1000000u);
                if (e.button.button == SDL_BUTTON_RIGHT) return onRightDown(e.button.x, e.button.y);
                return contains(e.button.x, e.button.y);
            case SDL_EVENT_MOUSE_MOTION:
                return onMotion(e.motion.x, e.motion.y);
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return onUp(e.button.x, e.button.y);
            case SDL_EVENT_MOUSE_WHEEL:
                if (!contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
                scrollBy(-e.wheel.y * Metrics::kListRowH * Metrics::kWheelRows);
                return true;
            case SDL_EVENT_KEY_DOWN:
                return onKey(e.key.key, static_cast<SDL_Keymod>(e.key.mod));
            case SDL_EVENT_TEXT_INPUT:
                if (!renaming_) return false;
                rename_.insert(e.text.text);
                return true;
            default:
                return false;
        }
    }

    // Area dada pelo dock (folha, sem a faixa de abas). nullopt: a janela flutua na posicao em que estava.
    void setDock(std::optional<SU::Rect> dock) { dock_ = dock; }
    [[nodiscard]] bool docked() const { return frame_.docked(); }
    [[nodiscard]] SU::Rect frameRect() const { return frame_.rect(); }
    [[nodiscard]] SU::Rect gripRect() const { return frame_.gripRect(); }
    // Moldura de posicao e tamanho: o host do dock a usa para redimensionar pelas bordas.
    window_frame::Frame* frameState() { return &frame_; }
    // Tira a janela do dock e a coloca flutuando no ponto dado.
    void floatAt(SU::Vec2 at) {
        dock_.reset();
        frame_.undock(at);
    }

    // Desenha a janela (moldura SpriteUI, barra, lista e menu). Os pedidos saem em `out`.
    bool draw(SDL_Renderer* renderer, SDL_Window* window, bool visible, Source src, Requests& out) {
      theme_ = sui::Theme{};   // cores da paleta em uso (Configuracoes > Appearance)
      colors_ = Colors{};
        const float textScale = editor::ui_style::textScale();
        toolbar_.setFontScale(textScale);
        list_.setFontScale(textScale);
        overlay_.setFontScale(textScale);
        chrome_.setFontScale(textScale);
        visible_ = visible;
        window_ = window;
        if (!visible) return true;
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        if (winW <= 0 || winH <= 0) return true;
        // Primeira posicao: a esquerda, na parte de baixo, com a altura da lista (cabe ~10 linhas).
        frame_.placeIn(static_cast<float>(winW), static_cast<float>(winH), dock_, kFrameW, kFrameH, 16.f);
        const SU::Rect content = frame_.content();
        rect_ = sui::Box{content.position.x, content.position.y, content.size.x, content.size.y};
        if (!iconsTried_) {
            iconsTried_ = true;
            loadIcons(renderer);
        }
        model_.Sync(std::move(src.items));
        syncSelection(src.selected);
        clampScroll();
        out.insert(out.end(), pending_.begin(), pending_.end());
        pending_.clear();

        buildToolbar();
        buildList();
        buildOverlay();

        const SU::Rect frameRect = frame_.rect();
        const std::vector<SR::ClipRectGPU> clips{{rect_.x, rect_.y, rect_.x + rect_.w, rect_.y + rect_.h},
                                                 {rect_.x, rect_.y + Metrics::kToolbarH, rect_.x + rect_.w, rect_.y + rect_.h},
                                                 {frameRect.position.x, frameRect.position.y, frameRect.position.x + frameRect.size.x,
                                                  frameRect.position.y + frameRect.size.y}};
        const sui::Box list = listBox();
        const SU::UIHandle owner = sui::handleFor(0);
        std::vector<SR::UIRenderItem> items;
        std::uint64_t paint = 1;
        appendFrame(items, paint, owner);
        items.push_back(sui::Canvas::solidItem(rect_.x, rect_.y, rect_.w, rect_.h, theme_.panel, 0.f, owner, 0u, paint++));
        toolbar_.appendItems(items, rect_.x, rect_.y, 0.f, rect_.y, rect_.y + rect_.h, owner, 0u, paint);
        list_.appendItems(items, rect_.x, rect_.y, 0.f, list.y + rect_.y, list.y + rect_.y + list.h, owner, 1u, paint);
        appendIcons(items, paint);
        overlay_.appendItems(items, rect_.x, rect_.y, 0.f, rect_.y, rect_.y + rect_.h, owner, 0u, paint);

        packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
        if (!SR::IsFramePacketValid(packet_)) return true;
        return sdl_.draw(renderer, packet_);
    }

    // true se o mouse esta sobre o campo de renomear (cursor de texto).
    [[nodiscard]] bool wantsTextCursor(float x, float y) const {
        return visible_ && renaming_ &&
               (renameDragging_ || renameField().contains(x - rect_.x, y - rect_.y));
    }

 private:
    // ---------- Geometria (coordenadas locais da janela: origem no canto da janela) ----------
    [[nodiscard]] sui::Box listBox() const {
        return {0.f, Metrics::kToolbarH, rect_.w - Metrics::kScrollLane, rect_.h - Metrics::kToolbarH};
    }
    // Y da linha na janela, ja descontada a rolagem.
    [[nodiscard]] float rowTop(std::size_t row) const {
        return Metrics::kToolbarH + static_cast<float>(row) * Metrics::kListRowH - scrollY_;
    }
    [[nodiscard]] float contentH() const { return static_cast<float>(model_.RowCount()) * Metrics::kListRowH; }
    [[nodiscard]] float maxScroll() const { return std::max(0.f, contentH() - listBox().h); }
    void clampScroll() { scrollY_ = std::clamp(scrollY_, 0.f, maxScroll()); }
    // Rola a lista e prende a rolagem nos limites.
    void scrollBy(float dy) {
        scrollY_ += dy;
        clampScroll();
    }
    [[nodiscard]] bool contains(float x, float y) const { return visible_ && sui::Box{0.f, 0.f, rect_.w, rect_.h}.contains(x - rect_.x, y - rect_.y); }

    // Linha sob o ponto (coordenadas da janela), ou npos.
    [[nodiscard]] std::size_t rowAt(float x, float y) const {
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!listBox().contains(lx, ly)) return npos;
        const std::size_t row = static_cast<std::size_t>((ly - Metrics::kToolbarH + scrollY_) / Metrics::kListRowH);
        return row < model_.RowCount() ? row : npos;
    }

    // Botao da barra sob o ponto (None se nenhum).
    [[nodiscard]] Button buttonAt(float x, float y) const {
        if (!contains(x, y)) return Button::None;
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        for (const auto& [box, which] : buttons_) if (box.contains(lx, ly)) return which;
        return Button::None;
    }

    // Indice do item do menu Create sob o ponto, ou -1.
    [[nodiscard]] int menuItemAt(float x, float y) const {
        if (!menuOpen_) return -1;
        const sui::Box menu = menuBox();
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!menu.contains(lx, ly)) return -1;
        const int index = static_cast<int>((ly - menu.y) / Metrics::kRowH);
        return index < kMenuRows ? index : -1;
    }

    // Indice do item do menu de contexto sob o ponto, ou -1.
    [[nodiscard]] int contextItemAt(float x, float y) const {
        if (!contextOpen_) return -1;
        const sui::Box menu = contextMenuBox();
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!menu.contains(lx, ly)) return -1;
        const int index = static_cast<int>((ly - menu.y) / Metrics::kRowH);
        return index < static_cast<int>(contextItems_.size()) ? index : -1;
    }

    // Indice do tipo de elemento no submenu sob o ponto, ou -1.
    [[nodiscard]] int subItemAt(float x, float y) const {
        if (subOpen_ == 0) return -1;
        const sui::Box sub = subMenuBox();
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!sub.contains(lx, ly)) return -1;
        const int index = static_cast<int>((ly - sub.y) / Metrics::kRowH);
        return index < hm::kElemKindCount ? index : -1;
    }

    // Caixa do submenu "Add element": ao lado do item que o abriu (a esquerda se nao couber a direita).
    [[nodiscard]] sui::Box subMenuBox() const {
        const float h = Metrics::kRowH * static_cast<float>(hm::kElemKindCount);
        const float w = Metrics::kSubMenuW;
        sui::Box parent;
        float rowY = 0.f;
        if (subOpen_ == 2) {
            parent = menuBox();
            rowY = parent.y + Metrics::kRowH * static_cast<float>(kCreateCount);
        } else {
            parent = contextMenuBox();
            int at = 0;
            for (std::size_t i = 0; i < contextItems_.size(); ++i) if (contextItems_[i].cmd == kCtxAddElem) at = static_cast<int>(i);
            rowY = parent.y + Metrics::kRowH * static_cast<float>(at);
        }
        float x = parent.x + parent.w;
        if (x + w > rect_.w) x = std::max(0.f, parent.x - w);
        return {x, std::clamp(rowY, 0.f, std::max(0.f, rect_.h - h)), w, h};
    }

    // Fecha os menus (Create, contexto e submenu).
    void closeMenus() {
        menuOpen_ = false;
        contextOpen_ = false;
        subOpen_ = 0;
        hoverSub_ = -1;
    }

    // Caixa do menu Create, presa dentro da janela.
    [[nodiscard]] sui::Box menuBox() const {
        const float h = std::min(Metrics::kRowH * kMenuRows + 8.f, std::max(0.f, rect_.h - Metrics::kToolbarH - 8.f));
        const float w = std::min(Metrics::kMenuW, std::max(120.f, rect_.w - Metrics::kPad));
        return {std::clamp(createX_, 0.f, std::max(0.f, rect_.w - w)),
                Metrics::kToolbarH, w, h};
    }

    // Caixa do menu de contexto (clique direito), presa dentro da janela.
    [[nodiscard]] sui::Box contextMenuBox() const {
        const float h = Metrics::kRowH * static_cast<float>(contextItems_.size());
        return {std::clamp(contextX_, 0.f, std::max(0.f, rect_.w - Metrics::kContextMenuW)),
                std::clamp(contextY_, 0.f, std::max(0.f, rect_.h - h)), Metrics::kContextMenuW, h};
    }

    // Caixas V e L no lado direito de uma linha. Retorna o indice da caixa (0 = V, 1 = L) ou -1.
    [[nodiscard]] int toggleAt(float lx) const {
        const float listW = listBox().w;
        const float lockX = listW - Metrics::kToggle - Metrics::kPad;
        const float eyeX = lockX - Metrics::kToggle - Metrics::kToggleGap;
        if (lx >= lockX && lx < lockX + Metrics::kToggle) return 1;
        if (lx >= eyeX && lx < eyeX + Metrics::kToggle) return 0;
        return -1;
    }

    // ---------- Sincronia com o Editor ----------
    void syncSelection(hm::Id scenePick) {
        if (scenePick == lastScenePick_) return;
        lastScenePick_ = scenePick;
        (void)model_.Select(scenePick);
    }

    // Rola a lista ate a linha do objeto selecionado aparecer.
    void ensureSelectedVisible() {
        const hm::Id id = model_.Selected();
        for (std::size_t row = 0; row < model_.RowCount(); ++row) {
            if (model_.IdOfRow(row) != id) continue;
            const float top = static_cast<float>(row) * Metrics::kListRowH;
            const float bottom = top + Metrics::kListRowH;
            if (top < scrollY_) scrollY_ = top;
            if (bottom > scrollY_ + listBox().h) scrollY_ = bottom - listBox().h;
            clampScroll();
            return;
        }
    }

    // Converte o resultado do modelo (selecao mudou, duplo clique, reparent...) em pedidos para o Editor.
    void applyOutcome(const hm::Outcome& out) {
        if (out.selectionChanged) pending_.push_back(Action{Act::Select, model_.Selected()});
        if (out.activate != hm::NoId && !hm::IsElemId(out.activate)) pending_.push_back(Action{Act::Activate, out.activate});
    }

    // ---------- Clique, arraste e teclado ----------
    bool onDown(float x, float y, std::uint64_t ticks) {
        if (!contains(x, y)) {
            focused_ = false;
            if (renaming_) commitRename();
            closeMenus();
            return false;
        }
        focused_ = true;
        if (renaming_ && !renameField().contains(x - rect_.x, y - rect_.y)) commitRename();
        if (bar_.pointerDown(x - rect_.x, y - rect_.y, scrollY_)) {   // barra de rolagem unica: polegar arrasta, trilho pagina
            clampScroll();
            return true;
        }
        if (renaming_) {
            moveRenameCaret(x, y);
            renameDragging_ = true;
            return true;
        }
        if (subOpen_ != 0) {   // submenu de tipos: escolher um adiciona o elemento (Create: ao canvas selecionado)
            const int sub = subItemAt(x, y);
            if (sub >= 0) {
                pending_.push_back(Action{Act::AddElem, subOpen_ == 1 ? contextTarget_ : hm::NoId, hm::NoId, sub, {}});
                closeMenus();
                hoverCreateItem_ = -1;
                return true;
            }
        }
        if (contextOpen_) {
            const int item = contextItemAt(x, y);
            if (item >= 0 && contextItems_[static_cast<std::size_t>(item)].cmd == kCtxAddElem) {   // abre o submenu e mantem o menu
                subOpen_ = 1;
                return true;
            }
            closeMenus();
            if (item >= 0) applyContextAction(contextItems_[static_cast<std::size_t>(item)].cmd);
            else return onDown(x, y, ticks);
            return true;
        }
        if (menuOpen_) {
            const int item = menuItemAt(x, y);
            if (item == kCreateCount) {   // "Canvas element >": abre o submenu e mantem o menu
                subOpen_ = 2;
                return true;
            }
            closeMenus();
            hoverCreateItem_ = -1;
            if (item >= 0) pending_.push_back(Action{Act::Create, hm::NoId, hm::NoId, item, {}});
            return true;
        }
        switch (buttonAt(x, y)) {
            case Button::Create: menuOpen_ = true; hoverCreateItem_ = -1; return true;
            case Button::Duplicate:
                if (model_.Selected() != hm::NoId) pending_.push_back(Action{Act::Duplicate, model_.Selected()});
                return true;
            case Button::Delete:
                if (model_.Selected() != hm::NoId) pending_.push_back(Action{Act::Delete, model_.Selected()});
                return true;
            case Button::None: break;
        }
        const std::size_t row = rowAt(x, y);
        if (row == npos) return true;
        const hm::Id id = model_.IdOfRow(row);
        const float lx = x - rect_.x;
        const float expanderX = static_cast<float>(model_.DepthOfRow(row)) * Metrics::kIndent + Metrics::kPad;
        if (model_.RowHasChildren(row) && lx >= expanderX && lx < expanderX + Metrics::kExpander) {
            applyOutcome(model_.Toggle(id));
            return true;
        }
        const bool isElem = hm::IsElemId(id);   // linha de elemento de UI: sem olho, cadeado, foco nem arraste
        const int toggle = isElem ? -1 : toggleAt(lx);
        if (toggle == 0) { pending_.push_back(Action{Act::ToggleHidden, id}); return true; }
        if (toggle == 1) { pending_.push_back(Action{Act::ToggleLocked, id}); return true; }
        const bool doubleClick = id == lastClickId_ && ticks - lastClickMs_ < Metrics::kDoubleClickMs;
        lastClickId_ = id;
        lastClickMs_ = ticks;
        applyOutcome(model_.Select(id));
        if (isElem) {   // clicar no elemento sempre pede a selecao (mesmo se a linha ja estava marcada no modelo)
            pending_.push_back(Action{Act::Select, id});
            return true;
        }
        if (doubleClick) pending_.push_back(Action{Act::Activate, id});
        (void)drag_.PointerDown(sui::handleFor(static_cast<std::uint32_t>(row + 1)), SU::Vec2{x - rect_.x, y - rect_.y},
                                Dd::DragPayload{"hier-entity", std::to_string(id)});
        return true;
    }

    // Clique direito: seleciona a linha e abre o menu de contexto; fora da janela so tira o foco.
    bool onRightDown(float x, float y) {
        if (!contains(x, y)) {
            focused_ = false;
            if (renaming_) commitRename();
            closeMenus();
            return false;
        }
        focused_ = true;
        closeMenus();
        if (renaming_ && !renameField().contains(x - rect_.x, y - rect_.y)) commitRename();
        const std::size_t row = rowAt(x, y);
        if (row == npos) return true;
        contextTarget_ = model_.IdOfRow(row);
        contextX_ = x - rect_.x;
        contextY_ = y - rect_.y;
        hoverContextItem_ = -1;
        if (model_.Selected() != contextTarget_) applyOutcome(model_.Select(contextTarget_));
        if (hm::IsElemId(contextTarget_)) pending_.push_back(Action{Act::Select, contextTarget_});
        buildContextItems();
        contextOpen_ = true;
        return true;
    }

    // Monta a lista do menu de contexto conforme o alvo: elemento de UI, canvas (com "Add element") ou objeto comum.
    void buildContextItems() {
        contextItems_.clear();
        if (hm::IsElemId(contextTarget_)) {
            contextItems_ = {{"Rename", kCtxRename}, {"Duplicate", kCtxDuplicate}, {"Move up", kCtxMoveUp},
                             {"Move down", kCtxMoveDown}, {"Delete", kCtxDelete}};
            return;
        }
        contextItems_ = {{"Focus", kCtxFocus}, {"Edit individual", kCtxEditIndividual}, {"Rename", kCtxRename}, {"Duplicate", kCtxDuplicate},
                         {"Delete", kCtxDelete}, {"Hide / Show", kCtxHide}, {"Lock / Unlock", kCtxLock}};
        const hm::Item* item = model_.Find(contextTarget_);
        if (item && item->kind == hm::Kind::Canvas) contextItems_.push_back({"Add element", kCtxAddElem});
    }

    // Executa o item do menu de contexto no objeto alvo (ativar, renomear, duplicar, apagar, ocultar, travar).
    void applyContextAction(int cmd) {
        if (contextTarget_ == hm::NoId) return;
        switch (cmd) {
            case kCtxFocus: pending_.push_back(Action{Act::Activate, contextTarget_}); break;
            case kCtxEditIndividual: pending_.push_back(Action{Act::EditIndividual, contextTarget_}); break;
            case kCtxRename: beginRename(contextTarget_); break;
            case kCtxDuplicate: pending_.push_back(Action{Act::Duplicate, contextTarget_}); break;
            case kCtxDelete: pending_.push_back(Action{Act::Delete, contextTarget_}); break;
            case kCtxHide: pending_.push_back(Action{Act::ToggleHidden, contextTarget_}); break;
            case kCtxLock: pending_.push_back(Action{Act::ToggleLocked, contextTarget_}); break;
            case kCtxMoveUp: pending_.push_back(Action{Act::MoveUp, contextTarget_}); break;
            case kCtxMoveDown: pending_.push_back(Action{Act::MoveDown, contextTarget_}); break;
            default: break;
        }
    }

    // Mouse movendo: arrasto da barra de rolagem, selecao de texto da renomeacao e hover/arraste de linhas.
    bool onMotion(float x, float y) {
        if (bar_.pointerMove(x - rect_.x, y - rect_.y, scrollY_)) clampScroll();
        if (bar_.dragging()) return true;   // o arraste segue com o mouse fora da lista
        if (renameDragging_ && renaming_) {
            moveRenameCaret(x, y);
            return true;
        }
        if (menuOpen_) {
            const int over = menuItemAt(x, y);
            if (over >= 0) subOpen_ = over == kCreateCount ? 2 : (subOpen_ == 2 ? 0 : subOpen_);   // passar no "Canvas element" abre o submenu
            if (over >= 0 || subItemAt(x, y) < 0) hoverCreateItem_ = over;
        }
        if (subOpen_ != 0) hoverSub_ = subItemAt(x, y);
        const bool dragging = drag_.Phase() != Dd::DragPhase::Idle;
        if (dragging) (void)drag_.PointerMove(SU::Vec2{x - rect_.x, y - rect_.y}, buildTargets());
        if (contains(x, y)) hoverRow_ = rowAt(x, y);
        if (contextOpen_) {
            const int over = contextItemAt(x, y);
            if (over >= 0) subOpen_ = contextItems_[static_cast<std::size_t>(over)].cmd == kCtxAddElem ? 1 : (subOpen_ == 1 ? 0 : subOpen_);
            if (over >= 0 || subItemAt(x, y) < 0) hoverContextItem_ = over;
        }
        return dragging || contains(x, y);
    }

    // Soltar o botao: encerra arrasto (barra, selecao de texto ou linha) e aplica o resultado.
    bool onUp(float x, float y) {
        if (bar_.pointerUp()) return true;
        if (renameDragging_) {
            renameDragging_ = false;
            return true;
        }
        if (drag_.Phase() == Dd::DragPhase::Idle) return contains(x, y);
        const auto result = drag_.PointerUp(SU::Vec2{x - rect_.x, y - rect_.y}, buildTargets());
        if (result) dropOn(*result);
        return true;
    }

    // Soltar: o alvo do vazio (handle 0) tira o objeto do pai; uma linha vira o novo pai.
    void dropOn(const Dd::DropResult& result) {
        const hm::Id child = std::stoull(result.payload.data);
        if (result.target.index == 0) {
            pending_.push_back(Action{Act::Detach, child});
            return;
        }
        const hm::Id parent = model_.IdOfRow(result.target.index - 1);
        if (model_.CanReparent(child, parent)) pending_.push_back(Action{Act::Reparent, child, parent});
    }

    // Alvos do arraste: o vazio da lista (handle 0) e cada linha visivel (handle = linha + 1). Linha vence o vazio.
    [[nodiscard]] std::vector<Dd::DropTarget> buildTargets() const {
        std::vector<Dd::DropTarget> targets;
        const sui::Box list = listBox();
        targets.push_back({sui::handleFor(0), {{0.f, Metrics::kToolbarH}, {list.w, list.h}}, "hier-entity"});
        const auto range = visibleRange();
        for (std::size_t row = range.first; row < range.first + range.count; ++row) {
            targets.push_back({sui::handleFor(static_cast<std::uint32_t>(row + 1)), {{0.f, rowTop(row)}, {list.w, Metrics::kListRowH}}, "hier-entity"});
        }
        return targets;
    }

    // Faixa de linhas realmente visiveis (so essas sao montadas).
    [[nodiscard]] hm::RowRange visibleRange() const {
        return hm::VisibleRange(model_.RowCount(), scrollY_, Metrics::kListRowH, listBox().h);
    }

    // Teclas: durante renomear vao para o campo; senao setas/Enter navegam a arvore, F2 renomeia e Esc cancela arrasto e menus.
    bool onKey(SDL_Keycode key, SDL_Keymod mod) {
        if (renaming_) {
            renameKey(key, mod);
            return true;
        }
        if (!focused_) return false;
        switch (key) {
            case SDLK_UP: return navigate(Dd::TreeKey::Up);
            case SDLK_DOWN: return navigate(Dd::TreeKey::Down);
            case SDLK_LEFT: return navigate(Dd::TreeKey::Left);
            case SDLK_RIGHT: return navigate(Dd::TreeKey::Right);
            case SDLK_RETURN:
            case SDLK_KP_ENTER: return navigate(Dd::TreeKey::Enter);
            case SDLK_F2:
                beginRename(model_.Selected());
                return true;
            case SDLK_ESCAPE:
                if (drag_.Phase() != Dd::DragPhase::Idle) drag_.Cancel();
                closeMenus();
                return true;
            default: return false;
        }
    }

    // Navegacao da arvore por teclado (setas, Home/End...) mantendo a selecao visivel.
    bool navigate(Dd::TreeKey key) {
        applyOutcome(model_.Key(key));
        ensureSelectedVisible();
        return true;
    }

    // ---------- Renomear (F2 ou clique fora do campo termina) ----------
    void beginRename(hm::Id id) {
        const hm::Item* item = model_.Find(id);
        if (!item) return;
        renaming_ = true;
        renameDragging_ = false;
        renameId_ = id;
        rename_.setText(hm::IsElemId(id) ? item->editText : item->label);
        if (window_) SDL_StartTextInput(window_);
    }

    // Confirma a renomeacao (se o nome nao ficou vazio) e sai do modo.
    void commitRename() {
        if (!renaming_) return;
        if (!rename_.empty()) pending_.push_back(Action{Act::Rename, renameId_, hm::NoId, 0, rename_.text()});
        stopRename();
    }

    // Sai do modo de renomear sem aplicar.
    void stopRename() {
        renaming_ = false;
        renameDragging_ = false;
        rename_.clear();
        if (window_) SDL_StopTextInput(window_);
    }

    // Teclas no campo de renomear: Ctrl+A/C/V/X, setas, Enter confirma, Esc cancela.
    void renameKey(SDL_Keycode key, SDL_Keymod mod) {
        const bool extend = (mod & SDL_KMOD_SHIFT) != 0;
        if ((mod & SDL_KMOD_CTRL) != 0) {
            switch (key) {
                case SDLK_A: rename_.selectAll(); return;
                case SDLK_C:
                    if (rename_.hasSelection()) SDL_SetClipboardText(rename_.selectedText().c_str());
                    return;
                case SDLK_X:
                    if (rename_.hasSelection()) {
                        SDL_SetClipboardText(rename_.selectedText().c_str());
                        rename_.eraseSelection();
                    }
                    return;
                case SDLK_V: {
                    char* text = SDL_GetClipboardText();
                    if (!text) break;
                    const std::string pasted(text);
                    SDL_free(text);
                    rename_.insert(pasted.substr(0, pasted.find_first_of("\r\n")));
                    return;
                }
                default: break;
            }
        }
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: commitRename(); break;
            case SDLK_ESCAPE: stopRename(); break;
            case SDLK_BACKSPACE: rename_.backspace(); break;
            case SDLK_DELETE: rename_.del(); break;
            case SDLK_LEFT: rename_.left(extend); break;
            case SDLK_RIGHT: rename_.right(extend); break;
            case SDLK_HOME: rename_.home(extend); break;
            case SDLK_END: rename_.end(extend); break;
            default: break;
        }
    }

    // Move o cursor/selecao do campo de renomear para o ponto do mouse.
    void moveRenameCaret(float x, float y) {
        const sui::Box field = renameField();
        const ST::TextLayoutResult layout = list_.layout(rename_.text(), Metrics::kListFont);
        const std::size_t caret = ST::FindInsertionOffset(layout, {std::max(0.f, x - rect_.x - field.x),
                                                                  std::max(0.f, y - rect_.y - field.y)});
        rename_.moveCaretTo(caret, renameDragging_);
    }

    // Campo do renomear: na linha do objeto em edicao, depois do icone.
    [[nodiscard]] sui::Box renameField() const {
        const std::size_t row = findRow(renameId_);
        if (row == npos) return {};
        const float x = static_cast<float>(model_.DepthOfRow(row)) * Metrics::kIndent + Metrics::kPad + labelOffset();
        return {x, rowTop(row) + 2.f, listBox().w - x - Metrics::kToggle * 2.f - Metrics::kPad * 2.f, Metrics::kListRowH - 4.f};
    }

    // Linha do objeto com esse id (npos se nao aparece).
    [[nodiscard]] std::size_t findRow(hm::Id id) const {
        for (std::size_t row = 0; row < model_.RowCount(); ++row) if (model_.IdOfRow(row) == id) return row;
        return npos;
    }

    [[nodiscard]] static float labelOffset() { return Metrics::kExpander + 4.f + Metrics::kIcon + 6.f; }

    // ---------- Icones (atlas de sprites) ----------
    // Carrega os PNGs e monta o atlas: bolinha (gerada aqui), olho e cadeado. Se faltar um PNG, a janela segue sem icones.
    void loadIcons(SDL_Renderer* renderer) {
        tool::Texture eye;
        tool::Texture lock;
        if (!loadImageFile(kEyePath, eye, nullptr) || !loadImageFile(kLockPath, lock, nullptr)) return;
        if (!squareMultiple(eye) || !squareMultiple(lock)) return;
        std::vector<std::uint8_t> atlas(static_cast<std::size_t>(kAtlasW) * kIconCell * 4, 0);
        packDot(atlas);
        packSquare(atlas);
        packTriangle(atlas);
        packRing(atlas);
        packCell(eye, kEyeCell, atlas);
        packCell(lock, kLockCell, atlas);
        iconsReady_ = sdl_.uploadIcons(renderer, kAtlasW, kIconCell, atlas.data());
    }

    // A textura serve de atlas de icones? Quadrada e multipla do tamanho da celula.
    static bool squareMultiple(const tool::Texture& t) {
        return t.w == t.h && t.w % static_cast<std::uint32_t>(kIconCell) == 0;
    }

    // Quadrado com cantos suaves (Canvas). Como a bolinha, branco: a cor vem do tipo na hora de desenhar.
    static void packSquare(std::vector<std::uint8_t>& atlas) {
        const float half = kIconCell * 0.5f, side = half - 2.f, radius = 4.f;
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                const float dx = std::fabs(x + 0.5f - half) - side + radius, dy = std::fabs(y + 0.5f - half) - side + radius;
                const float outside = std::hypot(std::max(dx, 0.f), std::max(dy, 0.f)) + std::min(std::max(dx, dy), 0.f) - radius;
                const float cover = std::clamp(0.5f - outside, 0.f, 1.f);
                std::uint8_t* px = &atlas[(static_cast<std::size_t>(y) * kAtlasW + (kSquareCell * kIconCell) + x) * 4];
                px[0] = px[1] = px[2] = 255;
                px[3] = static_cast<std::uint8_t>(cover * 255.f + 0.5f);
            }
        }
    }

    // Triangulo apontando para cima (Luz). Mesma regra de cor da bolinha.
    static void packTriangle(std::vector<std::uint8_t>& atlas) {
        const float apexX = kIconCell * 0.5f, apexY = 3.f, baseY = kIconCell - 4.f, halfBase = kIconCell * 0.4f;
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                const float py = y + 0.5f, px0 = x + 0.5f;
                float cover = 0.f;
                if (py >= apexY && py <= baseY) {
                    const float t = (py - apexY) / (baseY - apexY);       // 0 na ponta, 1 na base
                    const float half = halfBase * t;
                    cover = std::clamp(std::min(px0 - (apexX - half), (apexX + half) - px0) + 0.5f, 0.f, 1.f);
                }
                std::uint8_t* p = &atlas[(static_cast<std::size_t>(y) * kAtlasW + (kTriangleCell * kIconCell) + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<std::uint8_t>(cover * 255.f + 0.5f);
            }
        }
    }

    // Anel branco (so a borda de um circulo): sobre a bolinha tingida, faz a camera ter borda branca.
    static void packRing(std::vector<std::uint8_t>& atlas) {
        const float half = kIconCell * 0.5f, radius = half - 2.f, width = 2.2f;
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                const float d = std::hypot(x + 0.5f - half, y + 0.5f - half);
                const float cover = std::clamp(width * 0.5f - std::fabs(d - radius) + 0.5f, 0.f, 1.f);
                std::uint8_t* p = &atlas[(static_cast<std::size_t>(y) * kAtlasW + (kRingCell * kIconCell) + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<std::uint8_t>(cover * 255.f + 0.5f);
            }
        }
    }

    // Bolinha: disco branco com borda suave. A cor vem do tipo, na hora de desenhar.
    static void packDot(std::vector<std::uint8_t>& atlas) {
        const float half = kIconCell * 0.5f;
        const float radius = half - 1.f;
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                const float d = std::hypot(x + 0.5f - half, y + 0.5f - half);
                const float cover = std::clamp(radius + 0.5f - d, 0.f, 1.f);
                std::uint8_t* px = &atlas[(static_cast<std::size_t>(y) * kAtlasW + x) * 4];
                px[0] = px[1] = px[2] = 255;
                px[3] = static_cast<std::uint8_t>(cover * 255.f + 0.5f);
            }
        }
    }

    // Reduz a imagem (quadrada, multipla de kIconCell) por media de blocos, so no alfa: o formato do icone.
    // A cor nao vem do PNG (o olho e preto e o cadeado tem cor propria): e a cor do tema, na hora de desenhar.
    static void packCell(const tool::Texture& src, int cell, std::vector<std::uint8_t>& atlas) {
        const int f = static_cast<int>(src.w) / kIconCell;
        const double blocks = static_cast<double>(f) * f;
        for (int y = 0; y < kIconCell; ++y) {
            for (int x = 0; x < kIconCell; ++x) {
                double alpha = 0.0;
                for (int j = 0; j < f; ++j) {
                    for (int i = 0; i < f; ++i) {
                        alpha += src.rgba[(static_cast<std::size_t>(y * f + j) * src.w + static_cast<std::size_t>(x * f + i)) * 4 + 3];
                    }
                }
                std::uint8_t* px = &atlas[(static_cast<std::size_t>(y) * kAtlasW + cell * kIconCell + x) * 4];
                px[0] = px[1] = px[2] = 255;
                px[3] = static_cast<std::uint8_t>(alpha / blocks + 0.5);
            }
        }
    }

    // Icone deste quadro, em coordenadas locais do canvas da lista (ver appendIcons).
    void addIcon(float x, float y, float size, IconCell cell, SU::Vec4 tint) { icons_.push_back({x, y, size, cell, tint}); }
    // Icone do tipo: quadrado (Canvas), triangulo (Luz), bolinha com borda branca (Camera), bolinha (demais).
    void addKindIcon(float x, float y, hm::Kind kind) {
        const SU::Vec4 tint = colors_.kinds[static_cast<std::size_t>(kind)];
        switch (kind) {
            case hm::Kind::Canvas: addIcon(x, y, Metrics::kIcon, kSquareCell, tint); break;
            case hm::Kind::Light: addIcon(x, y, Metrics::kIcon, kTriangleCell, tint); break;
            case hm::Kind::Camera:
                addIcon(x, y, Metrics::kIcon, kDotCell, tint);
                addIcon(x, y, Metrics::kIcon, kRingCell, sui::Srgb(1.f, 1.f, 1.f));
                break;
            default: addIcon(x, y, Metrics::kIcon, kDotCell, tint); break;
        }
    }

    // Todos os icones do quadro viram sprites do atlas: um lote so. Cada sprite fica no seu lugar na janela.
    void appendIcons(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint) const {
        if (!iconsReady_) return;
        const SU::UIHandle owner = sui::handleFor(0);
        for (const IconDraw& d : icons_) {
            SR::UISprite sprite;
            sprite.atlasId = sui::kIconSlice;
            sprite.uvRect = {static_cast<float>(d.cell * kIconCell) / kAtlasW, 0.f, static_cast<float>(kIconCell) / kAtlasW, 1.f};
            sprite.sizePixels = {d.size, d.size};
            const float cx = rect_.x + d.x + d.size * 0.5f;
            const float cy = rect_.y + d.y + d.size * 0.5f;
            const auto item = SR::MakeSpriteItem(owner, sprite, {cx, cy}, 0.f, 1u, 0.3f, paint++);
            if (!item) continue;
            out.push_back(*item);
            out.back().tintPremultipliedLinear = d.tint;
        }
    }

    // ---------- Desenho ----------
    // Moldura SpriteUI (corpo, aba e X), titulo e grip. Por tras do conteudo (zOrder -2).
    void appendFrame(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint, SU::UIHandle owner) {
        SW::WindowFrameState state;
        state.active = true;
        state.dragging = frame_.dragging();
        state.docked = frame_.docked();
        frameColors_ = editor::window_palette::Standard();
        auto frameItems = SW::BuildWindowFrameItems(owner, frame_.rect(), frame_.metrics(), state, frameColors_, -2.f, paint);
        paint += frameItems.size() + 1;
        out.insert(out.end(), frameItems.begin(), frameItems.end());

        // "?" (circulo) logo depois do grip; o nome em negrito (duas passadas, 0,7 px).
        const SU::Rect title = frame_.titleRect();
        const SU::Rect help = frame_.helpRect();
        auto helpItems = window_help_icon::BuildCircleItems(owner, help, sui::PaletteColor(palette::active().accent), -1.9f, paint);
        out.insert(out.end(), helpItems.begin(), helpItems.end());
        const float size = Metrics::kFont + 1.f;
        const float titleX = frame_.titleTextX();
        const float titleW = title.position.x + title.size.x - titleX;
        const float ty = title.position.y + (title.size.y - chrome_.lineHeight(size)) * 0.5f;
        chrome_.clear();
        chrome_.addText(help.position.x + help.size.x * 0.5f - 4.f,
                        help.position.y + (help.size.y - chrome_.lineHeight(Metrics::kFont)) * 0.5f, std::string("?"), Metrics::kFont,
                        theme_.text, help.size.x);
        chrome_.addText(titleX, ty, std::string(T("Hierarchy")), size, theme_.text, titleW);
        chrome_.addText(titleX + 0.7f, ty, std::string(T("Hierarchy")), size, theme_.text, titleW);
        chrome_.appendItems(out, 0.f, 0.f, 0.f, -1e9f, 1e9f, owner, 2u, paint);

        // Grip: 2 colunas x 3 pontos, no centro do icone.
        const SU::Rect grip = frame_.gripRect();
        const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
        for (int c = 0; c < 2; ++c) {
            for (int r = 0; r < 3; ++r) {
                out.push_back(sui::Canvas::solidItem(cx - 3.f + 4.f * static_cast<float>(c), cy - 4.f + 4.f * static_cast<float>(r),
                                                     2.f, 2.f, theme_.textDim, 0.95f, owner, 2u, paint++));
            }
        }
    }

    // Monta a barra do topo (botoes Create/Duplicate/Delete) e guarda as caixas para clique.
    void buildToolbar() {
        toolbar_.clear();
        buttons_.clear();
        createX_ = Metrics::kPad;
        float x = addButton(Button::Create, T("+ Create"), createX_);
        x = addButton(Button::Duplicate, T("Duplicate"), x);
        (void)addButton(Button::Delete, T("Delete"), x);
    }

    // Desenha um botao da barra e devolve o X do proximo.
    float addButton(Button which, const char* label, float x) {
        const float w = toolbar_.layout(label, Metrics::kSmallFont).size.x + 2.f * Metrics::kBtnPad;
        const float h = Metrics::kToolbarBtnH;
        const float y = (Metrics::kToolbarH - h) * 0.5f;
        toolbar_.addSolid(x, y, w, h, colors_.btn, 0.2f);
        const float textY = y + (h - toolbar_.lineHeight(Metrics::kToolbarBtnFont)) * 0.5f;
        toolbar_.addText(x + Metrics::kBtnPad, textY, label, Metrics::kToolbarBtnFont, theme_.text, w - 2.f * Metrics::kBtnPad);
        buttons_.push_back({sui::Box{x, y, w, h}, which});
        return x + w + Metrics::kBtnGap;
    }

    // Monta so as linhas visiveis da arvore e a barra de rolagem.
    void buildList() {
        list_.clear();
        icons_.clear();
        const sui::Box list = listBox();
        const auto range = visibleRange();
        for (std::size_t row = range.first; row < range.first + range.count; ++row) addRow(row, list);
        addScrollThumb(list);
    }

    // Desenha uma linha: expansor +/-, icone do tipo, nome, olho e cadeado, com selecao/hover/alvo de soltar.
    void addRow(std::size_t row, const sui::Box& list) {
        const float y = rowTop(row);
        const hm::Id id = model_.IdOfRow(row);
        const hm::Item* item = model_.Find(id);
        if (!item) return;
        const bool selected = id == model_.Selected();
        const float rowX = static_cast<float>(model_.DepthOfRow(row)) * Metrics::kIndent;
        if (selected) list_.addSolid(0.f, y, list.w, Metrics::kListRowH, colors_.rowSel, 0.15f);
        else if (row == hoverRow_) list_.addSolid(0.f, y, list.w, Metrics::kListRowH, colors_.rowHover, 0.15f);
        if (drag_.Phase() == Dd::DragPhase::Dragging && drag_.HoveredTarget() && drag_.HoveredTarget()->index == row + 1) {
            list_.addSolid(0.f, y, list.w, Metrics::kListRowH, colors_.dropOn, 0.16f);
        }
        const float textY = y + (Metrics::kListRowH - list_.lineHeight(Metrics::kListFont)) * 0.5f;
        const float expanderX = rowX + Metrics::kPad;
        if (model_.RowHasChildren(row)) {
            list_.addText(expanderX, textY, model_.RowExpanded(row) ? "-" : "+", Metrics::kListFont, theme_.text, Metrics::kExpander);
        }
        const float iconX = expanderX + Metrics::kExpander + 4.f;
        const float iconY = y + (Metrics::kListRowH - Metrics::kIcon) * 0.5f;
        addKindIcon(iconX, iconY, item->kind);
        const float labelX = iconX + Metrics::kIcon + 6.f;
        const float lockX = list.w - Metrics::kToggle - Metrics::kPad;
        const bool isElem = item->kind == hm::Kind::Element;   // linha de acesso a elemento de UI: sem olho nem cadeado
        const float labelW = (isElem ? list.w - Metrics::kPad : lockX - Metrics::kToggleGap - Metrics::kToggle - Metrics::kPad) - labelX;
        const bool editing = renaming_ && id == renameId_;
        if (!editing) list_.addText(labelX, textY, item->label, Metrics::kListFont, item->hidden ? theme_.textDim : theme_.text, labelW);
        if (editing) addRenameText(labelX, y);
        if (isElem) return;
        const float toggleY = y + (Metrics::kListRowH - Metrics::kToggle) * 0.5f;
        const float eyeX = lockX - Metrics::kToggle - Metrics::kToggleGap;
        addIcon(eyeX, toggleY, Metrics::kToggle, kEyeCell, item->hidden ? colors_.toggleOff : colors_.toggleOn);
        addIcon(lockX, toggleY, Metrics::kToggle, kLockCell, item->locked ? colors_.lockOn : colors_.toggleOff);
    }

    // Campo do renomear: fundo, texto e caret (a posicao do caret e medida pelo texto antes dele).
    void addRenameText(float labelX, float y) {
        const sui::Box field = renameField();
        list_.addSolid(field.x, field.y, field.w, field.h, colors_.field, 0.3f);
        const float textY = y + (Metrics::kListRowH - list_.lineHeight(Metrics::kListFont)) * 0.5f;
        const std::string& text = rename_.text();
        const ST::TextLayoutResult layout = list_.layout(text, Metrics::kListFont);
        if (rename_.hasSelection()) {
            const auto [begin, end] = rename_.selectionRange();
            const float firstX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(begin)).position.x;
            const float lastX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(end)).position.x;
            list_.addSolid(labelX + std::min(firstX, lastX), textY, std::abs(lastX - firstX), layout.lineHeight, colors_.rowSel,
                           0.4f);
        }
        list_.addText(labelX, textY, text, Metrics::kListFont, theme_.text, field.w);
        const float caretX = labelX + ST::GetCaretRect(layout, static_cast<std::uint32_t>(rename_.caret())).position.x;
        list_.addSolid(caretX, y + 4.f, 1.f, Metrics::kListRowH - 8.f, colors_.caret, 0.4f);
    }

    // Barra de rolagem unica do editor (ui_scrollbar.hpp), na faixa reservada a direita da lista.
    void addScrollThumb(const sui::Box& list) {
        bar_.layout({list.w + 1.f, list.y, ui_scrollbar::kWidth, list.h}, contentH(), list.h);
        for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), scrollY_)) {
            if (q.rect.w > 0.f && q.rect.h > 0.f) list_.addSolid(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.5f);
        }
    }

    // Menu "+ Create": so existe com o botao aberto. Fica acima da lista (canvas proprio).
    void buildOverlay() {
        overlay_.clear();
        if (menuOpen_) {
            const sui::Box menu = menuBox();
            overlay_.addSolid(menu.x, menu.y, menu.w, menu.h, colors_.menu, Metrics::kMenuZ);
            for (int i = 0; i < kMenuRows; ++i) {
                const float y = menu.y + static_cast<float>(i) * Metrics::kRowH;
                if (i == hoverCreateItem_) {
                    overlay_.addSolid(menu.x + 2.f, y + 1.f, menu.w - 4.f, Metrics::kRowH - 2.f, colors_.rowHover, Metrics::kMenuHoverZ);
                }
                const float textY = y + (Metrics::kRowH - overlay_.lineHeight(Metrics::kFont)) * 0.5f;
                const std::string text = i < kCreateCount ? std::string(T(kCreateLabels[i])) : std::string(T("Canvas element")) + "  >";
                overlay_.addText(menu.x + Metrics::kPad, textY, text, Metrics::kFont, theme_.text, menu.w - 2.f * Metrics::kPad);
            }
        }
        if (contextOpen_) {
            const sui::Box menu = contextMenuBox();
            overlay_.addSolid(menu.x, menu.y, menu.w, menu.h, colors_.menu, Metrics::kMenuZ);
            for (int i = 0; i < static_cast<int>(contextItems_.size()); ++i) {
                const CtxItem& it = contextItems_[static_cast<std::size_t>(i)];
                const float y = menu.y + static_cast<float>(i) * Metrics::kRowH;
                if (i == hoverContextItem_) {
                    overlay_.addSolid(menu.x + 2.f, y + 1.f, menu.w - 4.f, Metrics::kRowH - 2.f, colors_.rowHover, Metrics::kMenuHoverZ);
                }
                const float textY = y + (Metrics::kRowH - overlay_.lineHeight(Metrics::kFont)) * 0.5f;
                const std::string text = std::string(T(it.label)) + (it.cmd == kCtxAddElem ? "  >" : "");
                overlay_.addText(menu.x + Metrics::kPad, textY, text, Metrics::kFont, theme_.text, menu.w - 2.f * Metrics::kPad);
            }
        }
        if (subOpen_ != 0 && (menuOpen_ || contextOpen_)) {   // submenu com os tipos de elemento de UI
            const sui::Box sub = subMenuBox();
            overlay_.addSolid(sub.x, sub.y, sub.w, sub.h, colors_.menu, Metrics::kMenuZ);
            for (int i = 0; i < hm::kElemKindCount; ++i) {
                const float y = sub.y + static_cast<float>(i) * Metrics::kRowH;
                if (i == hoverSub_) {
                    overlay_.addSolid(sub.x + 2.f, y + 1.f, sub.w - 4.f, Metrics::kRowH - 2.f, colors_.rowHover, Metrics::kMenuHoverZ);
                }
                const float textY = y + (Metrics::kRowH - overlay_.lineHeight(Metrics::kFont)) * 0.5f;
                overlay_.addText(sub.x + Metrics::kPad, textY, T(hm::kElemKindNames[i]), Metrics::kFont, theme_.text,
                                 sub.w - 2.f * Metrics::kPad);
            }
        }
    }

    // Comandos do menu de contexto (a lista muda conforme o alvo: objeto, canvas ou elemento de UI).
    enum Ctx : int { kCtxFocus, kCtxRename, kCtxDuplicate, kCtxDelete, kCtxHide, kCtxLock, kCtxMoveUp, kCtxMoveDown, kCtxAddElem, kCtxEditIndividual };
    // Item do menu de contexto: texto e comando.
    struct CtxItem {
        const char* label;
        int cmd;
    };
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr int kMenuRows = kCreateCount + 1;   // itens do Create + "Canvas element >"

    sui::Canvas toolbar_;
    sui::Canvas list_;
    sui::Canvas overlay_;
    sui::Canvas chrome_;   // titulo da moldura
    sui::SpriteUiSdlRenderer& sdl_;
    sui::Theme theme_;
    Colors colors_;
    hm::HierarchyModel model_;
    Dd::DragDropControl drag_;
    cm::LineEdit rename_;
    SR::RenderExtractor extractor_;
    SR::FramePacket packet_;
    std::uint64_t frameId_ = 0;

    static constexpr float kFrameW = 288.f;   // tamanho inicial da janela (px)
    static constexpr float kFrameH = 520.f;
    wf::Frame frame_;
    std::optional<SU::Rect> dock_;
    SW::WindowFrameColors frameColors_{};
    bool closeRequested_ = false;
    bool helpRequested_ = false;

    sui::Box rect_;
    bool visible_ = false;
    SDL_Window* window_ = nullptr;
    float scrollY_ = 0.f;
    ui_scrollbar::Scrollbar bar_;
    std::size_t hoverRow_ = npos;
    std::vector<std::pair<sui::Box, Button>> buttons_;
    float createX_ = 0.f;
    bool menuOpen_ = false;
    int hoverCreateItem_ = -1;
    bool contextOpen_ = false;
    hm::Id contextTarget_ = hm::NoId;
    float contextX_ = 0.f;
    float contextY_ = 0.f;
    int hoverContextItem_ = -1;
    std::vector<CtxItem> contextItems_;   // itens do menu de contexto aberto
    int subOpen_ = 0;                     // submenu de tipos: 0 fechado, 1 do menu de contexto, 2 do Create
    int hoverSub_ = -1;
    bool focused_ = false;
    bool renaming_ = false;
    bool renameDragging_ = false;
    hm::Id renameId_ = hm::NoId;
    hm::Id lastScenePick_ = hm::NoId;
    hm::Id lastClickId_ = hm::NoId;
    std::uint64_t lastClickMs_ = 0;
    Requests pending_;

    // Pedido de desenho de um icone do atlas (posicao, celula e cor), desenhado depois como sprite.
    struct IconDraw {
        float x, y, size;   // canto superior esquerdo, em coordenadas locais do canvas da lista
        IconCell cell;
        SU::Vec4 tint;
    };
    std::vector<IconDraw> icons_;
    bool iconsTried_ = false;
    bool iconsReady_ = false;
};

}  // namespace editor::hierarchy_panel
