// ToolEngine Editor - leitura de teclado, mouse e gamepads do SDL3 para um tool::RawInput (o InputSystem faz o resto).
// Gamepads: SDL3 abre controles Xbox pelo XInput/WGI no Windows (e DualShock/DualSense/Switch por HIDAPI): nada a configurar.
// Os botoes sao por POSICAO (South = A do Xbox), como no resto da engine.
#pragma once
#include "tool/input/input.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace editor {

class InputSampler {
 public:
  ~InputSampler() { for (auto& g : pads_) if (g.pad) SDL_CloseGamepad(g.pad); }
  // `allowKeyboard` = false quando a UI esta recebendo texto (nao pular enquanto digita um nome).
  void sample(tool::RawInput& raw, bool allowKeyboard) {
    raw = tool::RawInput{};
    if (allowKeyboard) {
      int n = 0; const bool* ks = SDL_GetKeyboardState(&n);
      for (int i = 0; i < n && i < static_cast<int>(tool::kMaxKeys); ++i) raw.keys[i] = ks[i];
      const SDL_MouseButtonFlags mb = SDL_GetMouseState(nullptr, nullptr);
      for (int b = 0; b < 5; ++b) raw.mouse[b] = (mb & SDL_BUTTON_MASK(b + 1)) != 0;
    }
    refresh();
    size_t slot = 0;
    for (auto& g : pads_) {
      if (slot >= tool::kMaxPads) break;
      tool::PadRaw& r = raw.pads[slot++]; r.connected = true;
      for (size_t b = 0; b < static_cast<size_t>(tool::PadButton::Count); ++b) r.buttons[b] = SDL_GetGamepadButton(g.pad, kButton[b]);
      for (size_t a = 0; a < static_cast<size_t>(tool::PadAxis::Count); ++a) {
        const float v = static_cast<float>(SDL_GetGamepadAxis(g.pad, kAxis[a])) / 32767.f;
        r.axes[a] = std::max(-1.f, std::min(1.f, v)); if (a >= 4 && r.axes[a] < 0.f) r.axes[a] = 0.f; // gatilhos 0..1
      }
    }
  }
  int connected() const { return static_cast<int>(pads_.size()); }
  std::string padName(size_t i) const { return i < pads_.size() ? std::string(SDL_GetGamepadName(pads_[i].pad) ? SDL_GetGamepadName(pads_[i].pad) : "Gamepad") : std::string(); }

 private:
  struct Pad { SDL_JoystickID id; SDL_Gamepad* pad; };
  // Abre controles novos / fecha os desconectados (barato: so olha a lista de IDs).
  void refresh() {
    int count = 0; SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (size_t i = 0; i < pads_.size();) {
      bool present = false; for (int k = 0; k < count; ++k) if (ids && ids[k] == pads_[i].id) present = true;
      if (!present) { SDL_CloseGamepad(pads_[i].pad); pads_.erase(pads_.begin() + static_cast<long>(i)); } else ++i;
    }
    for (int k = 0; ids && k < count && pads_.size() < tool::kMaxPads; ++k) {
      bool have = false; for (auto& p : pads_) if (p.id == ids[k]) have = true;
      if (!have) if (SDL_Gamepad* g = SDL_OpenGamepad(ids[k])) pads_.push_back({ids[k], g});
    }
    if (ids) SDL_free(ids);
  }
  static constexpr SDL_GamepadButton kButton[15] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
  static constexpr SDL_GamepadAxis kAxis[6] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
  std::vector<Pad> pads_;
};

} // namespace editor
