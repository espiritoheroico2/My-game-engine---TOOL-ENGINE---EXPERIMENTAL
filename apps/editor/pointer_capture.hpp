#pragma once
// Captura do ponteiro do mouse para os paineis que usam SpriteUI (Settings, etc.).
//
// Problema que isto resolve: o SDL manda MOUSE_MOTION o tempo todo, com ou sem botao. Antes, todo movimento era
// entregue aos controles como "arrasto". Se o botao era solto fora da area do painel, o Up nunca chegava e o
// slider/gizmo continuava "pressionado" sozinho.
//
// Regras:
//   - Down com o botao esquerdo dentro do painel abre a captura.
//   - Up com o botao esquerdo solto em qualquer lugar fecha a captura (mesmo fora do painel).
//   - Movimento sem o botao esquerdo segurado, com captura aberta, perde a captura: o chamador deve entregar um
//     Up forcado aos controles para que nenhum fique preso em "pressionado".
//   - Movimento sem captura nunca vira arrasto: so serve para o hover.

namespace editor::pointer_capture {

class PointerCapture {
public:
    // Botao esquerdo pressionado dentro da area do painel.
    void down() noexcept { active_ = true; }

    // Botao esquerdo solto. True se havia captura (o chamador deve entregar o Up aos controles).
    [[nodiscard]] bool up() noexcept {
        const bool was = active_;
        active_ = false;
        return was;
    }

    // Movimento do mouse. 'leftHeld' = botao esquerdo segurado segundo o SDL (e.motion.state & SDL_BUTTON_LMASK).
    // True se a captura foi perdida agora: o chamador deve soltar os controles com um Up forcado.
    [[nodiscard]] bool move(bool leftHeld) noexcept {
        if (active_ && !leftHeld) {
            active_ = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    void reset() noexcept { active_ = false; }

private:
    bool active_ = false;
};

} // namespace editor::pointer_capture
