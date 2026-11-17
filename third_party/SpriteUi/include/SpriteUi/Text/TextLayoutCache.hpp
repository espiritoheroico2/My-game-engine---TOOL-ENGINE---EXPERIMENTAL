#pragma once

// Cache de layout de texto. Layout e shaping de um rotulo que nao muda sao o trabalho mais caro do quadro: sem cache,
// o host refaz os mesmos glifos e a mesma quebra a cada quadro, para cada botao.
//
// Chave: texto, identidade da fonte (fontId, por exemplo FontAtlasKey::file), tamanho logico, largura maxima,
// multiplicador de linha, alinhamento, quebra e escala de pixel (devicePixelScale). Mudar qualquer um deles
// gera outra entrada. A escala de interface entra pela devicePixelScale (ver Text/UIScale.hpp).
//
// Exemplo de jogo: 200 botoes com o mesmo rotulo "Comprar" em 16 px geram uma entrada; cada quadro so consulta.
//
// Validade: a referencia devolvida por GetOrLayout vale ate a proxima chamada ao cache (ela pode remover entradas
// antigas). Limite: maxEntries; ao estourar, sai a entrada usada ha mais tempo.

#include "SpriteUi/Text/TextLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ToolEngine::SpriteUi::Text {

struct TextLayoutKey {
    std::string text;
    std::uint64_t fontId = 0;
    float fontSize = 16.0f;
    float maxWidth = 0.0f;
    float lineHeightMultiplier = 1.0f;
    TextAlignment alignment = TextAlignment::Left;
    TextWrap wrap = TextWrap::None;
    float devicePixelScale = 0.0f;

    [[nodiscard]] bool operator==(const TextLayoutKey& other) const noexcept = default;
};

struct TextLayoutKeyHash {
    [[nodiscard]] std::size_t operator()(const TextLayoutKey& key) const noexcept {
        auto mix = [](std::size_t seed, std::size_t value) {
            return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
        };
        std::size_t h = std::hash<std::string>{}(key.text);
        h = mix(h, static_cast<std::size_t>(key.fontId));
        h = mix(h, std::hash<float>{}(key.fontSize));
        h = mix(h, std::hash<float>{}(key.maxWidth));
        h = mix(h, std::hash<float>{}(key.lineHeightMultiplier));
        h = mix(h, static_cast<std::size_t>(key.alignment));
        h = mix(h, static_cast<std::size_t>(key.wrap));
        h = mix(h, std::hash<float>{}(key.devicePixelScale));
        return h;
    }
};

// Monta a chave a partir da configuracao usada pelo layout.
[[nodiscard]] inline TextLayoutKey MakeTextLayoutKey(std::string_view text, const TextLayoutConfig& config,
                                                     std::uint64_t fontId) {
    TextLayoutKey key;
    key.text.assign(text);
    key.fontId = fontId;
    key.fontSize = config.fontSize;
    key.maxWidth = config.maxWidth;
    key.lineHeightMultiplier = config.lineHeightMultiplier;
    key.alignment = config.alignment;
    key.wrap = config.wrap;
    key.devicePixelScale = config.devicePixelScale;
    return key;
}

class TextLayoutCache {
  public:
    explicit TextLayoutCache(std::size_t maxEntries = 512) noexcept : maxEntries_{maxEntries == 0 ? 1 : maxEntries} {}

    // Devolve o layout guardado para a chave. Se faltar, chama build() (que devolve TextLayoutResult) e guarda o
    // resultado. Ver a validade da referencia no topo do arquivo.
    template <class Build>
    [[nodiscard]] const TextLayoutResult& GetOrLayout(const TextLayoutKey& key, Build&& build) {
        const auto it = entries_.find(key);
        if (it != entries_.end()) {
            ++hits_;
            it->second.lastUse = ++clock_;
            return it->second.layout;
        }
        ++misses_;
        if (entries_.size() >= maxEntries_) EvictOldest();
        Entry entry{std::forward<Build>(build)(), ++clock_};
        return entries_.emplace(key, std::move(entry)).first->second.layout;
    }

    void Clear() noexcept {
        entries_.clear();
    }

    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t MaxEntries() const noexcept { return maxEntries_; }
    [[nodiscard]] std::uint64_t Hits() const noexcept { return hits_; }
    [[nodiscard]] std::uint64_t Misses() const noexcept { return misses_; }

  private:
    struct Entry {
        TextLayoutResult layout;
        std::uint64_t lastUse = 0;
    };

    void EvictOldest() {
        if (entries_.empty()) return;
        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        }
        entries_.erase(oldest);
    }

    std::unordered_map<TextLayoutKey, Entry, TextLayoutKeyHash> entries_;
    std::size_t maxEntries_;
    std::uint64_t clock_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};

} // namespace ToolEngine::SpriteUi::Text
