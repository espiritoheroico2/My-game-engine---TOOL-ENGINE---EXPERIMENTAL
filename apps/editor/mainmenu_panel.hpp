#pragma once

#include "mainmenu_model.hpp"
#include "sui_panel.hpp"

#include "SpriteUi/Widgets/Legacy/MenuBar.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::mainmenu {

namespace SU = ToolEngine::SpriteUi;
// Menu da barra principal: a API antiga do MenuBar fica em Widgets::Legacy na SpriteUI atual.
namespace SW = ToolEngine::SpriteUi::Widgets::Legacy;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

class MainMenuPanel {
public:
    MainMenuPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& renderer)
        : canvas_{text}, renderer_{renderer} {
        bar_.popupWidth = 260.0f;
        bar_.rowHeight = 24.0f;
        bar_.separatorHeight = 8.0f;
        style_.bar = sui::Srgb(0.055f, 0.075f, 0.11f);
        style_.headerOpen = sui::Srgb(0.16f, 0.2f, 0.28f);
        style_.popup = sui::Srgb(0.07f, 0.09f, 0.12f);
        style_.highlight = sui::Srgb(0.12f, 0.3f, 0.6f);
    }

    MainMenuPanel(const MainMenuPanel&) = delete;
    MainMenuPanel& operator=(const MainMenuPanel&) = delete;

    void setState(const MenuState& state, Translate translate) {
        if (state_ == state && translate_ == translate) return;
        state_ = state;
        translate_ = translate;
        std::vector<SW::MenuBarEntry> menus;
        for (const MenuSpec& menu : buildMenus(state_, translate_)) {
            SW::MenuBarEntry entry;
            entry.label = menu.title;
            for (const ItemSpec& item : menu.items) {
                SW::MenuItem menuItem;
                menuItem.label = item.label;
                menuItem.shortcut = item.shortcut;
                menuItem.kind = item.separator ? SW::MenuItemKind::Separator : SW::MenuItemKind::Action;
                menuItem.disabled = item.disabled;
                entry.items.push_back(std::move(menuItem));
            }
            menus.push_back(std::move(entry));
        }
        bar_.SetMenus(std::move(menus));
    }

    bool handleEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                return onPress(event.button.button, event.button.x, event.button.y);
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return onRelease(event.button.button, event.button.x, event.button.y);
            case SDL_EVENT_MOUSE_MOTION:
                return onMove(event.motion.x, event.motion.y);
            case SDL_EVENT_KEY_DOWN:
                return onKey(event.key.key, event.key.repeat);
            default:
                return false;
        }
    }

    bool draw(SDL_Renderer* renderer, SDL_Window* window, float barHeight, std::vector<Request>& requests) {
        requests.insert(requests.end(), queue_.begin(), queue_.end());
        queue_.clear();
        if (!renderer || !window) return false;

        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height)) return false;
        if (width <= 0 || height <= 0) return true;

        barHeight_ = barHeight;
        std::vector<float> headerWidths;
        headerWidths.reserve(bar_.Menus().size());
        const float lineHeight = canvas_.lineHeight(kFontSize);
        for (const SW::MenuBarEntry& menu : bar_.Menus()) {
            headerWidths.push_back(canvas_.layout(menu.label, kFontSize).size.x + 2.0f * kHeaderPadding);
        }
        bar_.Layout(SU::Rect{{0.0f, 0.0f}, {static_cast<float>(width), static_cast<float>(height)}},
                    barHeight_, headerWidths);
        buildCanvas(static_cast<float>(width), lineHeight);

        std::vector<SR::UIRenderItem> items;
        std::uint64_t paintOrder = 1;
        canvas_.appendItems(items, 0.0f, 0.0f, 0.0f, 0.0f, static_cast<float>(height),
                            sui::handleFor(0x4D454E55u), 0u, paintOrder);
        const std::vector<SR::ClipRectGPU> clips{{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)}};
        packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
        if (!SR::IsFramePacketValid(packet_)) return true;
        return renderer_.draw(renderer, packet_);
    }

  // Estado do menu para o roteador de paineis: se a lista de um menu esta aberta e onde (pixels da janela).
  [[nodiscard]] bool popupOpen() const { return bar_.IsOpen(); }
  [[nodiscard]] sui::Box popupBox() const { return toBox(bar_.PopupRect()); }

private:
    struct Style {
        SU::Vec4 bar = sui::Srgb(0.055f, 0.075f, 0.11f);
        SU::Vec4 headerOpen = sui::Srgb(0.16f, 0.2f, 0.28f);
        SU::Vec4 popup = sui::Srgb(0.07f, 0.09f, 0.12f);
        SU::Vec4 highlight = sui::Srgb(0.12f, 0.3f, 0.6f);
    };

    struct Button {
        sui::Box box;
        Command command = Command::None;
    };

    static constexpr float kFontSize = 13.0f;
    static constexpr float kHeaderPadding = 10.0f;
    static constexpr float kRowPadding = 12.0f;
    static constexpr float kButtonPadding = 10.0f;
    static constexpr float kButtonGap = 8.0f;
    static constexpr float kButtonInset = 2.0f;
    static constexpr float kButtonsShift = 30.0f;

    static sui::Box toBox(SU::Rect rect) {
        return {rect.position.x, rect.position.y, rect.size.x, rect.size.y};
    }

    bool onPress(std::uint8_t button, float x, float y) {
        if (button != SDL_BUTTON_LEFT) return false;
        if (const Button* hit = buttonAt(x, y)) {
            queue_.push_back(Request{hit->command, 0});
            return true;
        }
        const SW::MenuBarEvents events = bar_.PointerDown({x, y});
        pointerCaptured_ = events.handled;
        applyEvents(events);
        if (!events.handled) barActive_ = false;
        return events.handled;
    }

    bool onRelease(std::uint8_t button, float x, float y) {
        if (button != SDL_BUTTON_LEFT || !pointerCaptured_) return false;
        pointerCaptured_ = false;
        const SW::MenuBarEvents events = bar_.PointerUp({x, y});
        applyEvents(events);
        return true;
    }

    bool onMove(float x, float y) {
        const SW::MenuBarEvents events = bar_.PointerMove({x, y});
        return events.handled;
    }

    bool onKey(SDL_Keycode key, bool repeat) {
        if (key == SDLK_LALT || key == SDLK_RALT) {
            if (!repeat) {
                const SW::MenuBarEvents events = bar_.PressAlt();
                barActive_ = bar_.IsOpen() && !events.closed;
                applyEvents(events);
            }
            return false;
        }
        if (!barActive_ && !bar_.IsOpen()) return false;

        const std::optional<SW::MenuKey> menuKey = menuKeyOf(key);
        if (!menuKey) return false;
        const SW::MenuBarEvents events = bar_.KeyPressed(*menuKey);
        applyEvents(events);
        return events.handled;
    }

    static std::optional<SW::MenuKey> menuKeyOf(SDL_Keycode key) {
        switch (key) {
            case SDLK_UP: return SW::MenuKey::Up;
            case SDLK_DOWN: return SW::MenuKey::Down;
            case SDLK_LEFT: return SW::MenuKey::Left;
            case SDLK_RIGHT: return SW::MenuKey::Right;
            case SDLK_HOME: return SW::MenuKey::Home;
            case SDLK_END: return SW::MenuKey::End;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: return SW::MenuKey::Enter;
            case SDLK_ESCAPE: return SW::MenuKey::Escape;
            default: return std::nullopt;
        }
    }

    void applyEvents(const SW::MenuBarEvents& events) {
        if (events.activated) {
            const Request request = requestAt(events.menuIndex, events.itemIndex);
            if (request.command != Command::None) queue_.push_back(request);
        }
        if (events.closed) barActive_ = false;
    }

    void buildCanvas(float windowWidth, float lineHeight) {
        canvas_.clear();
        canvas_.addSolid(0.0f, 0.0f, windowWidth, barHeight_, style_.bar, 0.1f);
        for (std::size_t i = 0; i < bar_.Menus().size(); ++i) {
            const SU::Rect header = bar_.HeaderRect(i);
            if (bar_.ActiveMenu() == i) {
                canvas_.addSolid(header.position.x, header.position.y, header.size.x, header.size.y,
                                 style_.headerOpen, 0.2f);
            }
            canvas_.addText(header.position.x + kHeaderPadding,
                            header.position.y + (header.size.y - lineHeight) * 0.5f,
                            bar_.Menus()[i].label, kFontSize, theme_.text, header.size.x - 2.0f * kHeaderPadding);
        }
        addButtons(windowWidth, lineHeight);
        if (!bar_.IsOpen()) return;

        const SU::Rect popup = bar_.PopupRect();
        canvas_.addSolid(popup.position.x, popup.position.y, popup.size.x, popup.size.y, style_.popup, 0.3f);
        canvas_.addSolid(popup.position.x, popup.position.y, popup.size.x, 1.0f, theme_.divider, 0.4f);
        canvas_.addSolid(popup.position.x, popup.position.y + popup.size.y - 1.0f, popup.size.x, 1.0f, theme_.divider, 0.4f);
        canvas_.addSolid(popup.position.x, popup.position.y, 1.0f, popup.size.y, theme_.divider, 0.4f);
        canvas_.addSolid(popup.position.x + popup.size.x - 1.0f, popup.position.y, 1.0f, popup.size.y, theme_.divider, 0.4f);

        const std::size_t menuIndex = *bar_.ActiveMenu();
        const auto& items = bar_.Menus()[menuIndex].items;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const SU::Rect row = bar_.RowRect(menuIndex, i);
            if (items[i].kind == SW::MenuItemKind::Separator) {
                canvas_.addSolid(row.position.x + kRowPadding, row.position.y + row.size.y * 0.5f,
                                 std::max(0.0f, row.size.x - 2.0f * kRowPadding), 1.0f, theme_.divider, 0.5f);
                continue;
            }
            if (bar_.ActiveItem() == i) {
                canvas_.addSolid(row.position.x + 1.0f, row.position.y, std::max(0.0f, row.size.x - 2.0f),
                                 row.size.y, style_.highlight, 0.5f);
            }
            const float shortcutWidth = items[i].shortcut.empty()
                                            ? 0.0f
                                            : canvas_.layout(items[i].shortcut, kFontSize).size.x + kRowPadding;
            const SU::Vec4 textColor = items[i].disabled ? theme_.textDim : theme_.text;
            canvas_.addText(row.position.x + kRowPadding,
                            row.position.y + (row.size.y - lineHeight) * 0.5f,
                            items[i].label, kFontSize, textColor, row.size.x - 2.0f * kRowPadding - shortcutWidth);
            if (shortcutWidth > 0.0f) {
                canvas_.addText(row.position.x + row.size.x - kRowPadding,
                                row.position.y + (row.size.y - lineHeight) * 0.5f,
                                items[i].shortcut, kFontSize, theme_.textDim, shortcutWidth, true);
            }
        }
    }

    void addButtons(float windowWidth, float lineHeight) {
        buttons_.clear();
        float x = windowWidth * 0.5f - kButtonsShift;
        for (const BarButton& button : barButtons(state_, translate_)) {
            const float textWidth = canvas_.layout(button.label, kFontSize).size.x;
            const float width = textWidth + 2.0f * kButtonPadding;
            const sui::Box box{x, kButtonInset, width, barHeight_ - 2.0f * kButtonInset};
            canvas_.addSolid(box.x, box.y, box.w, box.h, theme_.divider, 0.2f);
            canvas_.addText(box.x + kButtonPadding, box.y + (box.h - lineHeight) * 0.5f,
                            button.label, kFontSize, theme_.text, width);
            buttons_.push_back(Button{box, button.command});
            x += width + kButtonGap;
        }
    }

    [[nodiscard]] const Button* buttonAt(float x, float y) const {
        for (const Button& button : buttons_) {
            if (button.box.contains(x, y)) return &button;
        }
        return nullptr;
    }

    sui::Canvas canvas_;
    sui::SpriteUiSdlRenderer& renderer_;
    sui::Theme theme_;
    Style style_;
    SW::MenuBar bar_;
    MenuState state_;
    Translate translate_ = [](const char* text) { return text; };
    float barHeight_ = 0.0f;
    bool pointerCaptured_ = false;
    bool barActive_ = false;
    std::vector<Request> queue_;
    std::vector<Button> buttons_;
    SR::RenderExtractor extractor_;
    SR::FramePacket packet_;
    std::uint64_t frameId_ = 0;
};

} // namespace editor::mainmenu
