#pragma once

// Identificador estavel de uma fonte no FontRegistry. Fica num arquivo proprio para que os estilos
// (Widgets/Style.hpp) o usem sem depender do atlas.
//
// Regras:
//  - Zero e invalido. Os ids validos comecam em 1 e nunca sao reaproveitados.
//  - O id nao muda quando outras fontes sao registradas.

#include <cstdint>

namespace ToolEngine::SpriteUi::Text {

struct FontId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(FontId lhs, FontId rhs) noexcept { return lhs.value == rhs.value; }
    friend constexpr bool operator!=(FontId lhs, FontId rhs) noexcept { return !(lhs == rhs); }
};

} // namespace ToolEngine::SpriteUi::Text
