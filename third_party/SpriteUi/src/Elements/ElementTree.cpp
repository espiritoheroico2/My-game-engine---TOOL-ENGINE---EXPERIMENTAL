#include "SpriteUi/Elements/ElementTree.hpp"

#include <algorithm>
#include <limits>

namespace ToolEngine::SpriteUi::Elements {

UIHandle ElementTree::Adopt(std::unique_ptr<Element> element, UIHandle parent) {
    Element* parentElement = Get(parent);
    if (parent.IsValid() && parentElement == nullptr) return InvalidUIHandle;

    std::uint32_t index = 0;
    if (!freeSlots_.empty()) {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    Slot& slot = slots_[index];
    const UIHandle handle{index, slot.generation};
    element->handle = handle;
    element->parent = parentElement != nullptr ? parent : InvalidUIHandle;
    if (parentElement != nullptr) parentElement->children.push_back(handle);
    slot.element = std::move(element);
    return handle;
}

void ElementTree::Destroy(UIHandle handle) {
    Element* element = Get(handle);
    if (element == nullptr) return;

    DetachFromParent(*element);
    const std::vector<UIHandle> children = element->children;
    for (UIHandle child : children) Destroy(child);
    DestroySlot(handle.index);
}

Element* ElementTree::Get(UIHandle handle) noexcept {
    if (handle.index >= slots_.size()) return nullptr;
    Slot& slot = slots_[handle.index];
    return slot.generation == handle.generation ? slot.element.get() : nullptr;
}

const Element* ElementTree::Get(UIHandle handle) const noexcept {
    if (handle.index >= slots_.size()) return nullptr;
    const Slot& slot = slots_[handle.index];
    return slot.generation == handle.generation ? slot.element.get() : nullptr;
}

void ElementTree::BringToFront(UIHandle handle) {
    const Element* element = Get(handle);
    if (element == nullptr) return;
    Element* parent = Get(element->parent);
    if (parent == nullptr) return;

    auto& siblings = parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
    siblings.push_back(handle);
}

void ElementTree::PaintOrder(UIHandle root, std::vector<UIHandle>& out) const {
    const Element* element = Get(root);
    if (element == nullptr) return;
    out.push_back(root);
    for (UIHandle child : element->children) PaintOrder(child, out);
}

void ElementTree::Arrange(UIHandle root, Rect area) {
    Element* element = Get(root);
    if (element == nullptr) return;
    element->border = element->ArrangeIn(area);
    const Rect content = element->ContentRect();
    for (UIHandle child : element->children) Arrange(child, content);
}

std::size_t ElementTree::LiveCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(),
                                                  [](const Slot& slot) { return slot.element != nullptr; }));
}

void ElementTree::DetachFromParent(const Element& element) {
    Element* parent = Get(element.parent);
    if (parent == nullptr) return;
    auto& siblings = parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), element.handle), siblings.end());
}

void ElementTree::DestroySlot(std::uint32_t index) {
    Slot& slot = slots_[index];
    slot.element.reset();
    // Geração nunca vale zero: zero e o valor de handle invalido.
    slot.generation = slot.generation == std::numeric_limits<std::uint32_t>::max() ? 1u : slot.generation + 1u;
    freeSlots_.push_back(index);
}

} // namespace ToolEngine::SpriteUi::Elements
