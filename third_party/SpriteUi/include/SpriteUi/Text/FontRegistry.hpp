#pragma once

// Registro de varias fontes. Cada fonte guarda o seu atlas (TruetypeAtlas) e o slice de textura em que o
// backend enxerga esse atlas. Os itens de texto usam o slice da fonte do layout (GenerateTextRenderItems
// recebe entry->textureSlice), entao duas fontes geram itens com slices diferentes.
//
// Regras:
//  - FontId estavel (FontId.hpp): nunca reaproveitado e nao muda ao registrar outras fontes.
//  - A primeira fonte registrada vira a padrao; SetDefault troca a padrao.
//  - Resolve(id) devolve a fonte do id, ou a padrao quando o id e invalido ou desconhecido. Assim um id
//    guardado em um estilo nunca deixa o texto sem fonte.
//  - Os ponteiros de Find/Resolve continuam validos ao registrar mais fontes (armazenamento em deque).
//  - Sem remocao: o registro so cresce. Uma fonte nao usada continua ocupando memoria ate o fim.

#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/FontId.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace ToolEngine::SpriteUi::Text {

struct FontEntry {
    FontId id{};
    std::string name;                                                    // Nome livre, para depuracao e listas.
    TruetypeAtlas atlas;                                                 // Atlas de glifos da fonte.
    std::uint32_t textureSlice = Rendering::WhiteTextureSlice;           // Slice do atlas no backend.
};

class FontRegistry {
public:
    // Registra uma fonte e devolve o id. Se for a primeira, vira a padrao.
    FontId Add(std::string name, TruetypeAtlas atlas, std::uint32_t textureSlice);

    // Define a fonte padrao. Retorna false (sem mudar nada) se o id nao existir.
    bool SetDefault(FontId id) noexcept;
    [[nodiscard]] FontId Default() const noexcept { return default_; }

    // Busca exata pelo id. nullptr se o id for invalido ou desconhecido.
    [[nodiscard]] const FontEntry* Find(FontId id) const noexcept;

    // Busca com fallback: id invalido ou desconhecido cai na fonte padrao. nullptr so se o registro estiver vazio.
    [[nodiscard]] const FontEntry* Resolve(FontId id) const noexcept;

    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

private:
    std::deque<FontEntry> entries_;
    FontId default_{};
    std::uint32_t nextId_ = 1;
};

} // namespace ToolEngine::SpriteUi::Text
