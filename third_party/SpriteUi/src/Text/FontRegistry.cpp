#include "SpriteUi/Text/FontRegistry.hpp"

#include <utility>

namespace ToolEngine::SpriteUi::Text {

FontId FontRegistry::Add(std::string name, TruetypeAtlas atlas, std::uint32_t textureSlice) {
    FontEntry entry;
    entry.id = FontId{nextId_++};
    entry.name = std::move(name);
    entry.atlas = std::move(atlas);
    entry.textureSlice = textureSlice;
    const FontId id = entry.id;
    entries_.push_back(std::move(entry));
    if (!default_.IsValid()) default_ = id;
    return id;
}

bool FontRegistry::SetDefault(FontId id) noexcept {
    if (Find(id) == nullptr) return false;
    default_ = id;
    return true;
}

const FontEntry* FontRegistry::Find(FontId id) const noexcept {
    if (!id.IsValid()) return nullptr;
    for (const auto& entry : entries_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

const FontEntry* FontRegistry::Resolve(FontId id) const noexcept {
    if (const FontEntry* found = Find(id)) return found;
    return Find(default_);
}

} // namespace ToolEngine::SpriteUi::Text
