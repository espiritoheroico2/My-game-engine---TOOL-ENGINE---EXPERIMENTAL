#pragma once

// Janela Hierarchy feita com SpriteUI. Substitui drawHierarchy do ImGui (ver kHierarchyUsesSpriteUi em main.cpp).
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
// Eventos dentro da janela nao chegam ao ImGui. Teclas so sao consumidas com o foco na janela (clique dentro).
//
// Icones: a bolinha do tipo, o olho (V) e o cadeado (L) sao sprites de um atlas. Os PNGs assets/icons/eye.png e
// lock.png sobem uma vez, na primeira vez que a janela desenha, e todos os icones saem num lote so.
// Cor cheia = ligado; cor apagada = desligado. Exemplo: com a Parede oculta, o olho fica apagado; com a Parede
// travada, o cadeado fica laranja.

#include "console_model.hpp"   // LineEdit: o campo de renomear usa o mesmo editor de linha do Console
#include "hierarchy_model.hpp"
#include "i18n.hpp"
#include "image_io.hpp"
#include "sui_panel.hpp"
#include "window_frame.hpp"
#include "window_help_icon.hpp"

#include "SpriteUi/Rendering/Sprite.hpp"
#include "SpriteUi/Widgets/DragDrop.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

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
enum class Act : std::uint8_t { Select, Activate, Reparent, Detach, ToggleHidden, ToggleLocked, Rename, Duplicate, Delete, Create };

struct Action {
    Act act;
    hm::Id id;
    hm::Id parent;   // Reparent: novo pai
    int kind;        // Create: indice em kCreateLabels
    std::string text;   // Rename: novo nome

    Action(Act a, hm::Id i = hm::NoId, hm::Id p = hm::NoId, int k = 0, std::string t = {})
        : act{a}, id{i}, parent{p}, kind{k}, text{std::move(t)} {}
};
using Requests = std::vector<Action>;

// O que a janela mostra: a lista de objetos deste quadro e a selecao do Editor.
struct Source {
    std::vector<hm::Item> items;
    hm::Id selected = hm::NoId;
};

// Itens do menu "+ Create", na ordem do menu do ImGui. O main.cpp cria o objeto pelo indice.
inline constexpr const char* kCreateLabels[] = {"Empty object", "Cube", "Sphere", "Plane", "Camera",
                                                "Main Camera (Principal)", "Global Light", "Point Light", "Audio Source"};
inline constexpr int kCreateCount = 9;

struct Metrics {
    static constexpr float kToolbarH = 30.f;
    static constexpr float kRowH = 22.f;
    static constexpr float kIndent = 14.f;        // recuo por nivel da arvore
    static constexpr float kPad = sui::kPanelPadding;
    static constexpr float kExpander = 10.f;      // area do sinal + / -
    static constexpr float kIcon = 10.f;
    static constexpr float kToggle = 16.f;        // olho V (visivel) e cadeado L (travado), em px
    static constexpr float kToggleGap = 4.f;
    static constexpr float kScrollLane = 10.f;    // faixa da barra de rolagem a direita
    static constexpr float kBtnPad = 10.f;
    static constexpr float kBtnGap = 6.f;
    static constexpr float kMenuW = 200.f;
    static constexpr float kContextMenuW = 190.f;
    static constexpr float kFont = 13.f;
    static constexpr float kSmallFont = 11.f;
    static constexpr float kWheelRows = 3.f;      // linhas por giro da roda
    static constexpr std::uint64_t kDoubleClickMs = 400;
};

struct Colors {
    SU::Vec4 rowSel = sui::Srgb(0.12f, 0.31f, 0.52f, 0.9f);
    SU::Vec4 rowHover = sui::Srgb(0.12f, 0.15f, 0.20f);
    SU::Vec4 dropOn = sui::Srgb(0.20f, 0.45f, 0.30f);
    SU::Vec4 btn = sui::Srgb(0.13f, 0.16f, 0.21f);
    SU::Vec4 toggleOn = sui::Srgb(0.80f, 0.82f, 0.86f);
    SU::Vec4 toggleOff = sui::Srgb(0.22f, 0.24f, 0.29f);
    SU::Vec4 lockOn = sui::Srgb(0.95f, 0.70f, 0.30f);
    SU::Vec4 scroll = sui::Srgb(0.45f, 0.48f, 0.55f, 0.75f);
    SU::Vec4 menu = sui::Srgb(0.09f, 0.11f, 0.15f);
    SU::Vec4 field = sui::Srgb(0.08f, 0.10f, 0.14f);
    SU::Vec4 caret = sui::Srgb(0.91f, 0.918f, 0.933f);
    // Cor do icone por tipo, na ordem de hm::Kind.
    SU::Vec4 kinds[6] = {sui::Srgb(0.62f, 0.68f, 0.78f), sui::Srgb(0.42f, 0.85f, 0.52f), sui::Srgb(1.f, 0.85f, 0.35f),
                         sui::Srgb(0.85f, 0.55f, 0.95f), sui::Srgb(1.f, 0.6f, 0.3f), sui::Srgb(0.4f, 0.85f, 0.95f)};
};

enum class Button : std::uint8_t { None, Create, Duplicate, Delete };

// Atlas de icones: celulas de kIconCell px, lado a lado. A ordem e a do IconCell.
inline constexpr int kIconCell = 32;
inline constexpr int kIconCellCount = 3;
inline constexpr int kAtlasW = kIconCell * kIconCellCount;
enum IconCell : int { kDotCell, kEyeCell, kLockCell };
inline constexpr const char* kEyePath = "assets/icons/eye.png";
inline constexpr const char* kLockPath = "assets/icons/lock.png";

class HierarchyPanel {
 public:
    HierarchyPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl)
        : toolbar_{text}, list_{text}, overlay_{text}, chrome_{text}, sdl_{sdl} {
        // Moldura: mesmo visual do Console e do Profiler.
        frameColors_.body = sui::Srgb(0.055f, 0.075f, 0.110f);
        frameColors_.border = sui::Srgb(0.160f, 0.220f, 0.330f);
        frameColors_.borderDragging = sui::Srgb(0.420f, 0.600f, 0.980f);
        frameColors_.headerActive = sui::Srgb(0.120f, 0.160f, 0.230f);
        frameColors_.headerInactive = sui::Srgb(0.090f, 0.120f, 0.180f);
        frameColors_.headerDragging = sui::Srgb(0.160f, 0.210f, 0.300f);
        frameColors_.glyph = sui::Srgb(0.910f, 0.918f, 0.933f);
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

    // true quando o evento e da janela (o editor nao o passa ao ImGui).
    bool handleEvent(const SDL_Event& e) {
        if (!visible_) return false;
        // Moldura primeiro: X fecha, so o grip arrasta. Soltar o botao encerra o arraste.
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            const wf::Event ev = frame_.pointerDown({e.button.x, e.button.y});
            if (ev == wf::Event::Close) closeRequested_ = true;
            if (ev == wf::Event::Help) helpRequested_ = true;
            if (ev != wf::Event::None) return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) frame_.pointerUp();
        if (e.type == SDL_EVENT_MOUSE_MOTION && frame_.dragging()) {
            frame_.pointerMove({e.motion.x, e.motion.y});
            return true;
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
                scrollBy(-e.wheel.y * Metrics::kRowH * Metrics::kWheelRows);
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

    // Desenha a janela (moldura SpriteUI, barra, lista e menu). Os pedidos saem em `out`.
    bool draw(SDL_Renderer* renderer, SDL_Window* window, bool visible, Source src, Requests& out) {
        visible_ = visible;
        window_ = window;
        if (!visible) return true;
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        if (winW <= 0 || winH <= 0) return true;
        // Primeira posicao: a esquerda, na parte de baixo, com a altura da lista (cabe ~10 linhas).
        frame_.place(static_cast<float>(winW), static_cast<float>(winH), kFrameW, kFrameH, 16.f);
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

    [[nodiscard]] bool wantsTextCursor(float x, float y) const {
        return visible_ && renaming_ &&
               (renameDragging_ || renameField().contains(x - rect_.x, y - rect_.y));
    }

 private:
    // ---------- Geometria (coordenadas locais da janela: origem no canto da janela) ----------
    [[nodiscard]] sui::Box listBox() const {
        return {0.f, Metrics::kToolbarH, rect_.w - Metrics::kScrollLane, rect_.h - Metrics::kToolbarH};
    }
    [[nodiscard]] float rowTop(std::size_t row) const {
        return Metrics::kToolbarH + static_cast<float>(row) * Metrics::kRowH - scrollY_;
    }
    [[nodiscard]] float contentH() const { return static_cast<float>(model_.RowCount()) * Metrics::kRowH; }
    [[nodiscard]] float maxScroll() const { return std::max(0.f, contentH() - listBox().h); }
    void clampScroll() { scrollY_ = std::clamp(scrollY_, 0.f, maxScroll()); }
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
        const std::size_t row = static_cast<std::size_t>((ly - Metrics::kToolbarH + scrollY_) / Metrics::kRowH);
        return row < model_.RowCount() ? row : npos;
    }

    [[nodiscard]] Button buttonAt(float x, float y) const {
        if (!contains(x, y)) return Button::None;
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        for (const auto& [box, which] : buttons_) if (box.contains(lx, ly)) return which;
        return Button::None;
    }

    [[nodiscard]] int menuItemAt(float x, float y) const {
        if (!menuOpen_) return -1;
        const sui::Box menu = menuBox();
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!menu.contains(lx, ly)) return -1;
        const int index = static_cast<int>((ly - menu.y) / Metrics::kRowH);
        return index < kCreateCount ? index : -1;
    }

    [[nodiscard]] int contextItemAt(float x, float y) const {
        if (!contextOpen_) return -1;
        const sui::Box menu = contextMenuBox();
        const float lx = x - rect_.x;
        const float ly = y - rect_.y;
        if (!menu.contains(lx, ly)) return -1;
        const int index = static_cast<int>((ly - menu.y) / Metrics::kRowH);
        return index < kContextCount ? index : -1;
    }

    [[nodiscard]] sui::Box menuBox() const {
        return {createX_, Metrics::kToolbarH, Metrics::kMenuW, Metrics::kRowH * kCreateCount};
    }

    [[nodiscard]] sui::Box contextMenuBox() const {
        const float h = Metrics::kRowH * kContextCount;
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

    void ensureSelectedVisible() {
        const hm::Id id = model_.Selected();
        for (std::size_t row = 0; row < model_.RowCount(); ++row) {
            if (model_.IdOfRow(row) != id) continue;
            const float top = static_cast<float>(row) * Metrics::kRowH;
            const float bottom = top + Metrics::kRowH;
            if (top < scrollY_) scrollY_ = top;
            if (bottom > scrollY_ + listBox().h) scrollY_ = bottom - listBox().h;
            clampScroll();
            return;
        }
    }

    void applyOutcome(const hm::Outcome& out) {
        if (out.selectionChanged) pending_.push_back(Action{Act::Select, model_.Selected()});
        if (out.activate != hm::NoId) pending_.push_back(Action{Act::Activate, out.activate});
    }

    // ---------- Clique, arraste e teclado ----------
    bool onDown(float x, float y, std::uint64_t ticks) {
        if (!contains(x, y)) {
            focused_ = false;
            if (renaming_) commitRename();
            menuOpen_ = false;
            contextOpen_ = false;
            return false;
        }
        focused_ = true;
        if (renaming_ && !renameField().contains(x - rect_.x, y - rect_.y)) commitRename();
        if (renaming_) {
            moveRenameCaret(x, y);
            renameDragging_ = true;
            return true;
        }
        if (contextOpen_) {
            const int item = contextItemAt(x, y);
            contextOpen_ = false;
            if (item >= 0) applyContextAction(item);
            else return onDown(x, y, ticks);
            return true;
        }
        if (menuOpen_) {
            const int item = menuItemAt(x, y);
            menuOpen_ = false;
            if (item >= 0) pending_.push_back(Action{Act::Create, hm::NoId, hm::NoId, item, {}});
            return true;
        }
        switch (buttonAt(x, y)) {
            case Button::Create: menuOpen_ = true; return true;
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
        const int toggle = toggleAt(lx);
        if (toggle == 0) { pending_.push_back(Action{Act::ToggleHidden, id}); return true; }
        if (toggle == 1) { pending_.push_back(Action{Act::ToggleLocked, id}); return true; }
        const bool doubleClick = id == lastClickId_ && ticks - lastClickMs_ < Metrics::kDoubleClickMs;
        lastClickId_ = id;
        lastClickMs_ = ticks;
        applyOutcome(model_.Select(id));
        if (doubleClick) pending_.push_back(Action{Act::Activate, id});
        (void)drag_.PointerDown(sui::handleFor(static_cast<std::uint32_t>(row + 1)), SU::Vec2{x - rect_.x, y - rect_.y},
                                Dd::DragPayload{"hier-entity", std::to_string(id)});
        return true;
    }

    bool onRightDown(float x, float y) {
        if (!contains(x, y)) {
            focused_ = false;
            if (renaming_) commitRename();
            menuOpen_ = false;
            contextOpen_ = false;
            return false;
        }
        focused_ = true;
        menuOpen_ = false;
        contextOpen_ = false;
        if (renaming_ && !renameField().contains(x - rect_.x, y - rect_.y)) commitRename();
        const std::size_t row = rowAt(x, y);
        if (row == npos) return true;
        contextTarget_ = model_.IdOfRow(row);
        contextX_ = x - rect_.x;
        contextY_ = y - rect_.y;
        hoverContextItem_ = -1;
        if (model_.Selected() != contextTarget_) applyOutcome(model_.Select(contextTarget_));
        contextOpen_ = true;
        return true;
    }

    void applyContextAction(int item) {
        if (contextTarget_ == hm::NoId) return;
        switch (item) {
            case 0: pending_.push_back(Action{Act::Activate, contextTarget_}); break;
            case 1: beginRename(contextTarget_); break;
            case 2: pending_.push_back(Action{Act::Duplicate, contextTarget_}); break;
            case 3: pending_.push_back(Action{Act::Delete, contextTarget_}); break;
            case 4: pending_.push_back(Action{Act::ToggleHidden, contextTarget_}); break;
            case 5: pending_.push_back(Action{Act::ToggleLocked, contextTarget_}); break;
            default: break;
        }
    }

    bool onMotion(float x, float y) {
        if (renameDragging_ && renaming_) {
            moveRenameCaret(x, y);
            return true;
        }
        const bool dragging = drag_.Phase() != Dd::DragPhase::Idle;
        if (dragging) (void)drag_.PointerMove(SU::Vec2{x - rect_.x, y - rect_.y}, buildTargets());
        if (contains(x, y)) hoverRow_ = rowAt(x, y);
        if (contextOpen_) hoverContextItem_ = contextItemAt(x, y);
        return dragging || contains(x, y);
    }

    bool onUp(float x, float y) {
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
            targets.push_back({sui::handleFor(static_cast<std::uint32_t>(row + 1)), {{0.f, rowTop(row)}, {list.w, Metrics::kRowH}}, "hier-entity"});
        }
        return targets;
    }

    [[nodiscard]] hm::RowRange visibleRange() const {
        return hm::VisibleRange(model_.RowCount(), scrollY_, Metrics::kRowH, listBox().h);
    }

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
                menuOpen_ = false;
                contextOpen_ = false;
                return true;
            default: return false;
        }
    }

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
        rename_.setText(item->label);
        if (window_) SDL_StartTextInput(window_);
    }

    void commitRename() {
        if (!renaming_) return;
        if (!rename_.empty()) pending_.push_back(Action{Act::Rename, renameId_, hm::NoId, 0, rename_.text()});
        stopRename();
    }

    void stopRename() {
        renaming_ = false;
        renameDragging_ = false;
        rename_.clear();
        if (window_) SDL_StopTextInput(window_);
    }

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

    void moveRenameCaret(float x, float y) {
        const sui::Box field = renameField();
        const ST::TextLayoutResult layout = list_.layout(rename_.text(), Metrics::kFont);
        const std::size_t caret = ST::FindInsertionOffset(layout, {std::max(0.f, x - rect_.x - field.x),
                                                                  std::max(0.f, y - rect_.y - field.y)});
        rename_.moveCaretTo(caret, renameDragging_);
    }

    // Campo do renomear: na linha do objeto em edicao, depois do icone.
    [[nodiscard]] sui::Box renameField() const {
        const std::size_t row = findRow(renameId_);
        if (row == npos) return {};
        const float x = static_cast<float>(model_.DepthOfRow(row)) * Metrics::kIndent + Metrics::kPad + labelOffset();
        return {x, rowTop(row) + 2.f, listBox().w - x - Metrics::kToggle * 2.f - Metrics::kPad * 2.f, Metrics::kRowH - 4.f};
    }

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
        packCell(eye, kEyeCell, atlas);
        packCell(lock, kLockCell, atlas);
        iconsReady_ = sdl_.uploadIcons(renderer, kAtlasW, kIconCell, atlas.data());
    }

    static bool squareMultiple(const tool::Texture& t) {
        return t.w == t.h && t.w % static_cast<std::uint32_t>(kIconCell) == 0;
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
        state.docked = false;
        auto frameItems = SW::BuildWindowFrameItems(owner, frame_.rect(), frame_.metrics(), state, frameColors_, -2.f, paint);
        paint += frameItems.size() + 1;
        out.insert(out.end(), frameItems.begin(), frameItems.end());

        // "?" (circulo) logo depois do grip; o nome em negrito (duas passadas, 0,7 px).
        const SU::Rect title = frame_.titleRect();
        const SU::Rect help = frame_.helpRect();
        auto helpItems = window_help_icon::BuildCircleItems(owner, help, SU::Vec4{0.250f, 0.400f, 0.700f, 1.f}, -1.9f, paint);
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

    void buildToolbar() {
        toolbar_.clear();
        buttons_.clear();
        createX_ = Metrics::kPad;
        float x = addButton(Button::Create, T("+ Create"), createX_);
        x = addButton(Button::Duplicate, T("Duplicate"), x);
        (void)addButton(Button::Delete, T("Delete"), x);
    }

    float addButton(Button which, const char* label, float x) {
        const float w = toolbar_.layout(label, Metrics::kSmallFont).size.x + 2.f * Metrics::kBtnPad;
        const float h = Metrics::kToolbarH - 2.f * Metrics::kPad;
        toolbar_.addSolid(x, Metrics::kPad, w, h, colors_.btn, 0.2f);
        const float textY = Metrics::kPad + (h - toolbar_.lineHeight(Metrics::kSmallFont)) * 0.5f;
        toolbar_.addText(x + Metrics::kBtnPad, textY, label, Metrics::kSmallFont, theme_.text, w);
        buttons_.push_back({sui::Box{x, Metrics::kPad, w, h}, which});
        return x + w + Metrics::kBtnGap;
    }

    void buildList() {
        list_.clear();
        icons_.clear();
        const sui::Box list = listBox();
        const auto range = visibleRange();
        for (std::size_t row = range.first; row < range.first + range.count; ++row) addRow(row, list);
        addScrollThumb(list);
    }

    void addRow(std::size_t row, const sui::Box& list) {
        const float y = rowTop(row);
        const hm::Id id = model_.IdOfRow(row);
        const hm::Item* item = model_.Find(id);
        if (!item) return;
        const bool selected = id == model_.Selected();
        const float rowX = static_cast<float>(model_.DepthOfRow(row)) * Metrics::kIndent;
        if (selected) list_.addSolid(0.f, y, list.w, Metrics::kRowH, colors_.rowSel, 0.15f);
        else if (row == hoverRow_) list_.addSolid(0.f, y, list.w, Metrics::kRowH, colors_.rowHover, 0.15f);
        if (drag_.Phase() == Dd::DragPhase::Dragging && drag_.HoveredTarget() && drag_.HoveredTarget()->index == row + 1) {
            list_.addSolid(0.f, y, list.w, Metrics::kRowH, colors_.dropOn, 0.16f);
        }
        const float textY = y + (Metrics::kRowH - list_.lineHeight(Metrics::kFont)) * 0.5f;
        const float expanderX = rowX + Metrics::kPad;
        if (model_.RowHasChildren(row)) {
            list_.addText(expanderX, textY, model_.RowExpanded(row) ? "-" : "+", Metrics::kFont, theme_.text, Metrics::kExpander);
        }
        const float iconX = expanderX + Metrics::kExpander + 4.f;
        const float iconY = y + (Metrics::kRowH - Metrics::kIcon) * 0.5f;
        addIcon(iconX, iconY, Metrics::kIcon, kDotCell, colors_.kinds[static_cast<std::size_t>(item->kind)]);
        const float labelX = iconX + Metrics::kIcon + 6.f;
        const float lockX = list.w - Metrics::kToggle - Metrics::kPad;
        const float labelW = lockX - Metrics::kToggleGap - Metrics::kToggle - Metrics::kPad - labelX;
        const bool editing = renaming_ && id == renameId_;
        if (!editing) list_.addText(labelX, textY, item->label, Metrics::kFont, item->hidden ? theme_.textDim : theme_.text, labelW);
        if (editing) addRenameText(labelX, y);
        const float toggleY = y + (Metrics::kRowH - Metrics::kToggle) * 0.5f;
        const float eyeX = lockX - Metrics::kToggle - Metrics::kToggleGap;
        addIcon(eyeX, toggleY, Metrics::kToggle, kEyeCell, item->hidden ? colors_.toggleOff : colors_.toggleOn);
        addIcon(lockX, toggleY, Metrics::kToggle, kLockCell, item->locked ? colors_.lockOn : colors_.toggleOff);
    }

    // Campo do renomear: fundo, texto e caret (a posicao do caret e medida pelo texto antes dele).
    void addRenameText(float labelX, float y) {
        const sui::Box field = renameField();
        list_.addSolid(field.x, field.y, field.w, field.h, colors_.field, 0.3f);
        const float textY = y + (Metrics::kRowH - list_.lineHeight(Metrics::kFont)) * 0.5f;
        const std::string& text = rename_.text();
        const ST::TextLayoutResult layout = list_.layout(text, Metrics::kFont);
        if (rename_.hasSelection()) {
            const auto [begin, end] = rename_.selectionRange();
            const float firstX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(begin)).position.x;
            const float lastX = ST::GetCaretRect(layout, static_cast<std::uint32_t>(end)).position.x;
            list_.addSolid(labelX + std::min(firstX, lastX), textY, std::abs(lastX - firstX), layout.lineHeight, colors_.rowSel,
                           0.4f);
        }
        list_.addText(labelX, textY, text, Metrics::kFont, theme_.text, field.w);
        const float caretX = labelX + ST::GetCaretRect(layout, static_cast<std::uint32_t>(rename_.caret())).position.x;
        list_.addSolid(caretX, y + 4.f, 1.f, Metrics::kRowH - 8.f, colors_.caret, 0.4f);
    }

    void addScrollThumb(const sui::Box& list) {
        if (contentH() <= list.h) return;
        const float thumbH = std::max(20.f, list.h * list.h / contentH());
        const float thumbY = list.y + (list.h - thumbH) * (scrollY_ / maxScroll());
        list_.addSolid(list.w + 3.f, thumbY, 4.f, thumbH, colors_.scroll, 0.5f);
    }

    // Menu "+ Create": so existe com o botao aberto. Fica acima da lista (canvas proprio).
    void buildOverlay() {
        overlay_.clear();
        if (menuOpen_) {
            const sui::Box menu = menuBox();
            overlay_.addSolid(menu.x, menu.y, menu.w, menu.h, colors_.menu, 0.6f);
            for (int i = 0; i < kCreateCount; ++i) {
                const float y = menu.y + static_cast<float>(i) * Metrics::kRowH;
                const float textY = y + (Metrics::kRowH - overlay_.lineHeight(Metrics::kFont)) * 0.5f;
                overlay_.addText(menu.x + Metrics::kPad, textY, T(kCreateLabels[i]), Metrics::kFont, theme_.text,
                                 menu.w - 2.f * Metrics::kPad);
            }
        }
        if (contextOpen_) {
            static constexpr const char* labels[kContextCount] = {"Focus", "Rename", "Duplicate", "Delete", "Hide / Show",
                                                                  "Lock / Unlock"};
            const sui::Box menu = contextMenuBox();
            overlay_.addSolid(menu.x, menu.y, menu.w, menu.h, colors_.menu, 0.7f);
            for (int i = 0; i < kContextCount; ++i) {
                const float y = menu.y + static_cast<float>(i) * Metrics::kRowH;
                if (i == hoverContextItem_) {
                    overlay_.addSolid(menu.x + 2.f, y + 1.f, menu.w - 4.f, Metrics::kRowH - 2.f, colors_.rowHover, 0.72f);
                }
                const float textY = y + (Metrics::kRowH - overlay_.lineHeight(Metrics::kFont)) * 0.5f;
                overlay_.addText(menu.x + Metrics::kPad, textY, T(labels[i]), Metrics::kFont, theme_.text,
                                 menu.w - 2.f * Metrics::kPad);
            }
        }
    }

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr int kContextCount = 6;

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
    SW::WindowFrameColors frameColors_{};
    bool closeRequested_ = false;
    bool helpRequested_ = false;

    sui::Box rect_;
    bool visible_ = false;
    SDL_Window* window_ = nullptr;
    float scrollY_ = 0.f;
    std::size_t hoverRow_ = npos;
    std::vector<std::pair<sui::Box, Button>> buttons_;
    float createX_ = 0.f;
    bool menuOpen_ = false;
    bool contextOpen_ = false;
    hm::Id contextTarget_ = hm::NoId;
    float contextX_ = 0.f;
    float contextY_ = 0.f;
    int hoverContextItem_ = -1;
    bool focused_ = false;
    bool renaming_ = false;
    bool renameDragging_ = false;
    hm::Id renameId_ = hm::NoId;
    hm::Id lastScenePick_ = hm::NoId;
    hm::Id lastClickId_ = hm::NoId;
    std::uint64_t lastClickMs_ = 0;
    Requests pending_;

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
