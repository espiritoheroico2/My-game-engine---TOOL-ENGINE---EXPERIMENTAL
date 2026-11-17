#pragma once

// Evento de ponteiro entregue pelo chamador a um elemento. O chamador faz o hit test e informa se o ponteiro
// esta dentro do elemento (inside); o elemento nao consulta a geometria por conta propria.

#include "SpriteUi/Core/Math.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Elements {

enum class PointerPhase : std::uint8_t { Enter, Leave, Down, Move, Up, Cancel };

struct PointerEvent {
    PointerPhase phase = PointerPhase::Move;
    Vec2 position{0.0f, 0.0f}; // Coordenadas logicas.
    std::uint32_t pointerId = 0;
    bool inside = false;       // O ponteiro esta sobre o elemento neste evento.
};

} // namespace ToolEngine::SpriteUi::Elements
