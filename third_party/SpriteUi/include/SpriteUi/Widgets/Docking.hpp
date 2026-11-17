#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Layout/Layout.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

struct UITab {
    std::string title;
    UIHandle contentHandle{};
    bool closeable = false;
};

// Tab group of a dock leaf (editor panels and multi-document windows, Section 24). Each tab shows one window
// (its content handle), and a window appears in at most one tab of the group.
class UITabGroupComponent {
  public:
    UITabGroupComponent() = default;

    // Appends a tab. The first tab becomes the active one; later tabs leave the active tab unchanged.
    // Returns false, and changes nothing, for an invalid handle or a window that already has a tab.
    bool AddTab(std::string title, UIHandle contentHandle, bool closeable = false);

    // Removes a tab. When the active tab is removed, the tab that takes its index becomes active (or the
    // last tab, when the removed tab was the last one). Removing an earlier tab keeps the active window.
    bool RemoveTab(std::size_t index);
    bool RemoveContent(UIHandle contentHandle);

    bool SelectTab(std::size_t index) noexcept;
    bool SelectContent(UIHandle contentHandle) noexcept;

    [[nodiscard]] std::optional<std::size_t> IndexOf(UIHandle contentHandle) const noexcept;
    [[nodiscard]] std::size_t GetActiveTabIndex() const noexcept { return activeTabIndex_; }
    [[nodiscard]] UIHandle GetActiveContent() const noexcept;
    [[nodiscard]] std::size_t TabCount() const noexcept { return tabs_.size(); }
    [[nodiscard]] const std::vector<UITab>& GetTabs() const noexcept { return tabs_; }

  private:
    std::vector<UITab> tabs_;
    std::size_t activeTabIndex_ = 0;
};

enum class DockSlot : std::uint8_t { None, Left, Right, Top, Bottom, Center };

// Binary tree split node of a dock area (Section 24).
class UIDockNode {
  public:
    UIDockNode() = default;
    explicit UIDockNode(UIHandle initialContent) : content{initialContent} {}

    Rect bounds{};
    UIHandle content{};
    // Preenchido quando uma janela e solta em DockSlot::Center de um no folha ocupado.
    // Nesse caso as janelas ficam so em tabs (content fica invalido) e o conteudo visivel e
    // tabs.GetActiveContent().
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
