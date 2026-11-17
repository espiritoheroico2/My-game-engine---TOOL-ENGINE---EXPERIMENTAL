#pragma once

// Separador visual: uma linha fina centrada numa area, na horizontal ou na vertical. Modelo puro: so calcula
// o retangulo da linha. Nao desenha e nao guarda estado de interacao.
//
// Regras:
//  - A linha ocupa todo o comprimento da area e fica centrada na espessura.
//  - Espessura nao finita ou nao positiva resulta em retangulo vazio (nada e desenhado).

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

enum class SeparatorAxis : std::uint8_t { Horizontal, Vertical };

struct SeparatorControl {
    SeparatorAxis axis = SeparatorAxis::Horizontal;
    float thickness = 1.0f;

    [[nodiscard]] Rect LineRect(Rect area) const noexcept {
        if (!IsFinite(area.position) || !IsFinite(area.size) || !IsFinite(thickness) || thickness <= 0.0f) {
            return {};
        }
        if (axis == SeparatorAxis::Horizontal) {
            const float y = area.position.y + (area.size.y - thickness) * 0.5f;
            return {{area.position.x, y}, {area.size.x, thickness}};
        }
        const float x = area.position.x + (area.size.x - thickness) * 0.5f;
        return {{x, area.position.y}, {thickness, area.size.y}};
    }
};

} // namespace ToolEngine::SpriteUi::Widgets
