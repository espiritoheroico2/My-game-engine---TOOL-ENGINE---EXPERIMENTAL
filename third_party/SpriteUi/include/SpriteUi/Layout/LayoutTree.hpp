#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Layout/Layout.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi {

struct LayoutElement {
    UIHandle id{};
    UIHandle parent{};
    LayoutNode node{};
    std::vector<UIHandle> children{};
    bool visible = true;
};

class LayoutTree {
public:
    [[nodiscard]] UIHandle Create(UIHandle parent = {}, FlexItem style = {});
    bool Remove(UIHandle id);

    [[nodiscard]] LayoutElement* Get(UIHandle id);
    [[nodiscard]] const LayoutElement* Get(UIHandle id) const;

    bool SetStyle(UIHandle id, FlexItem style);
    bool SetVisible(UIHandle id, bool visible);

    void Layout(Rect viewport, LayoutStats* stats = nullptr);
    [[nodiscard]] const std::vector<UIHandle>& Roots() const noexcept { return roots_; }

private:
    [[nodiscard]] LayoutNode Build(UIHandle id);
    void Commit(UIHandle id, const LayoutNode& node);
    void RemoveSubtree(UIHandle id);

    UIHandle next_{0, 1};
    std::unordered_map<std::uint64_t, LayoutElement> elements_;
    std::vector<UIHandle> roots_;
};

} // namespace ToolEngine::SpriteUi
