#include "SpriteUi/Text/TextShaper.hpp"

namespace ToolEngine::SpriteUi::Text {

std::vector<GlyphInstance> CodepointShaper::Shape(std::string_view utf8, const TruetypeAtlas& atlas) const {
    // Mesma regra do caminho atual: delega para ShapeTruetype, que ja trata UTF-8 invalido, tab e controles.
    return ShapeTruetype(utf8, atlas);
}

} // namespace ToolEngine::SpriteUi::Text
