#include "SpriteUi/Text/FontAtlasCache.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace ToolEngine::SpriteUi::Text {

namespace {

// Mistura um campo inteiro no hash. Constante da familia de mistura de 64 bits (golden ratio).
template <typename T>
void HashCombine(std::size_t& seed, T value) noexcept {
    seed ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

std::size_t FontAtlasKeyHash::operator()(const FontAtlasKey& key) const noexcept {
    std::size_t seed = 0;
    HashCombine(seed, key.file);
    HashCombine(seed, key.faceIndex);
    HashCombine(seed, key.weight);
    HashCombine(seed, key.style);
    HashCombine(seed, key.mode);
    HashCombine(seed, key.rasterBucket);
    HashCombine(seed, key.pxRange);
    HashCombine(seed, key.charsetId);
    return seed;
}

FontFileHash HashFontBytes(std::span<const std::uint8_t> bytes) noexcept {
    // FNV-1a 64 bits: simples, sem dependencias e suficiente para distinguir fontes diferentes em um cache.
    constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t hash = kOffsetBasis;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= kPrime;
    }
    return hash;
}

FontAtlasCache::FontAtlasCache(Config config) : config_(std::move(config)) {}

std::size_t FontAtlasCache::EstimateBytes(const TruetypeAtlas& atlas) noexcept {
    // Estimativa de memoria CPU: pixels do atlas mais o mapa de glifos. Nao conta a textura GPU.
    return atlas.pixels.size() + atlas.glyphs.size() * sizeof(TruetypeGlyph);
}

FontAtlasCache::AtlasPtr FontAtlasCache::Find(const FontAtlasKey& key) {
    const auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    it->second.lastUse = ++clock_;
    return it->second.atlas;
}

FontAtlasCache::AtlasPtr FontAtlasCache::GetOrBuild(const FontAtlasKey& key,
                                                    const std::function<bool(TruetypeAtlas&)>& build) {
    if (AtlasPtr cached = Find(key)) return cached;
    if (!build) return nullptr;

    auto fresh = std::make_shared<TruetypeAtlas>();
    if (!build(*fresh)) return nullptr;

    Entry entry;
    entry.atlas = fresh;
    entry.bytes = EstimateBytes(*fresh);
    entry.lastUse = ++clock_;
    bytes_ += entry.bytes;
    entries_.emplace(key, entry);

    // O resultado ja e devolvido com uma referencia viva: o Trim nao derruba o que acabou de ser construido.
    AtlasPtr result = fresh;
    EvictToBudget();
    return result;
}

void FontAtlasCache::Remove(std::unordered_map<FontAtlasKey, Entry, FontAtlasKeyHash>::iterator it) {
    bytes_ -= it->second.bytes;
    const FontAtlasKey key = it->first;
    entries_.erase(it);
    if (config_.onEvict) config_.onEvict(key);
}

void FontAtlasCache::EvictToBudget() {
    while (entries_.size() > config_.maxEntries || bytes_ > config_.maxBytes) {
        // Candidata: sem uso externo (use_count == 1, so o cache segura) e a menos usada recentemente.
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.atlas.use_count() != 1) continue;
            if (victim == entries_.end() || it->second.lastUse < victim->second.lastUse) victim = it;
        }
        if (victim == entries_.end()) break;   // Tudo em uso: fica acima do orcamento ate alguem soltar.
        Remove(victim);
    }
}

void FontAtlasCache::Trim() {
    EvictToBudget();
}

void FontAtlasCache::Clear() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.atlas.use_count() == 1) {
            auto next = std::next(it);
            Remove(it);
            it = next;
        } else {
            ++it;
        }
    }
}

} // namespace ToolEngine::SpriteUi::Text
