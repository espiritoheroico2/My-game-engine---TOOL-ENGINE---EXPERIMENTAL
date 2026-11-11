#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Layout/Layout.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

struct UITab {
    std::string title;
    UIHandle contentHandle{};
    bool closeable = false;
};

// Tab grouping for editor panels and multi-document windows (Section 24).
class UITabGroupComponent {
public:
    UITabGroupComponent() = default;

    bool AddTab(std::string title, UIHandle contentHandle, bool closeable = false);
    bool RemoveTab(std::size_t index);
    bool SelectTab(std::size_t index) noexcept;

    [[nodiscard]] std::size_t GetActiveTabIndex() const noexcept { return activeTabIndex_; }
    [[nodiscard]] UIHandle GetActiveContent() const noexcept;
    [[nodiscard]] std::size_t TabCount() const noexcept { return tabs_.size(); }
    [[nodiscard]] const std::vector<UITab>& GetTabs() const noexcept { return tabs_; }

private:
    std::vector<UITab> tabs_;
    std::size_t activeTabIndex_ = 0;
};

enum class DockSlot : std::uint8_t {
    None,
    Left,
    Right,
    Top,
    Bottom,
    Center
};

// Binary tree split node for UIDockSpace (Section 24).
class UIDockNode {
public:
    UIDockNode() = default;
    explicit UIDockNode(UIHandle initialContent) : content{initialContent} {}

    Rect bounds{};
    UIHandle content{};
    // Preenchido quando uma janela e solta em DockSlot::Center de um no folha ocupado.
    // Nesse caso o conteudo visivel e tabs.GetActiveContent().
    UITabGroupComponent tabs;
    float splitRatio = 0.5f;
    bool isSplit = false;
    LayoutDirection splitDirection = LayoutDirection::Row;

    std::unique_ptr<UIDockNode> firstChild;
    std::unique_ptr<UIDockNode> secondChild;

    // Conteudo efetivamente visivel num no folha (aba ativa, se houver grupo de abas).
    [[nodiscard]] UIHandle VisibleContent() const noexcept {
        return tabs.TabCount() > 0 ? tabs.GetActiveContent() : content;
    }

    // Splits this node into two children based on the chosen slot and ratio.
    // Retorna false para nos ja divididos (divida um filho folha) ou parametros invalidos.
    bool Split(DockSlot slot, float ratio, UIHandle newContent);

    // Recursively recalculates rectangles for all dock splits
    void Layout(Rect availableBounds);
};

} // namespace ToolEngine::SpriteUi::Widgets

