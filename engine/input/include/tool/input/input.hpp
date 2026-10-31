// ToolEngine - entrada: gamepad por POSICAO (Sul/Leste/Oeste/Norte), teclado, mouse,
// zonas mortas e mapa de acoes nomeadas. Backends (SDL3, Web Gamepad, UWP) apenas
// preenchem um RawInput; toda a logica abaixo e independente de plataforma e testavel.
#pragma once
#include "tool/core/types.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace tool {

// Botao por posicao fisica: "A" do Xbox == "Cruz" do PlayStation == South.
enum class PadButton : u8 {
  South, East, West, North, LeftShoulder, RightShoulder, Back, Start, Guide,
  LeftStick, RightStick, DPadUp, DPadDown, DPadLeft, DPadRight, Count
};
enum class PadAxis : u8 { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

constexpr size_t kMaxPads = 4;
constexpr size_t kMaxKeys = 512;

struct Vec2 { f32 x = 0, y = 0; };

struct PadRaw {
  bool connected = false;
  bool buttons[static_cast<size_t>(PadButton::Count)] = {};
  f32 axes[static_cast<size_t>(PadAxis::Count)] = {}; // sticks -1..1, gatilhos 0..1
};

// Dados crus de um quadro, entregues pelo backend da plataforma.
struct RawInput {
  bool keys[kMaxKeys] = {};
  bool mouse[5] = {};
  Vec2 mouseDelta, mouseWheel;
  PadRaw pads[kMaxPads];
};

struct DeadzoneConfig {
  f32 stick = 0.15f;   // zona morta radial
  f32 trigger = 0.05f;
};

// Zona morta radial com reescala: sai de 0 em `dz` e chega a 1 em 1.
inline Vec2 applyRadialDeadzone(Vec2 v, f32 dz) {
  const f32 mag = std::sqrt(v.x * v.x + v.y * v.y);
  if (mag <= dz) return {};
  const f32 scaled = std::min(1.f, (mag - dz) / (1.f - dz));
  return {v.x / mag * scaled, v.y / mag * scaled};
}

enum class DeviceKind : u8 { Key, Mouse, PadButton, PadAxisPositive, PadAxisNegative };
struct Binding {
  DeviceKind kind = DeviceKind::Key;
  u16 code = 0;     // tecla / botao / eixo
  u8 pad = 0;       // indice do controle (ignorado em teclado)
  f32 scale = 1.f;
};

struct ActionState {
  f32 value = 0;      // 0..1 (analogico) ou 0/1
  bool down = false, pressed = false, released = false;
};

class InputSystem {
public:
  void setDeadzones(DeadzoneConfig d) { dz_ = d; }

  // Acoes: varias vinculacoes por acao (teclado + controle ao mesmo tempo).
  void bind(const std::string& action, Binding b) { bindings_[action].push_back(b); }
  void clearBindings(const std::string& action) { bindings_.erase(action); }
  // Par de eixos nomeado (ex.: "move" = esquerda/direita/cima/baixo) lido por vec2().
  void bindVec2(const std::string& name, const std::string& left, const std::string& right, const std::string& up,
                const std::string& down) {
    vec2_[name] = {left, right, up, down};
  }

  // Chamado uma vez por quadro com os dados crus do backend.
  void update(const RawInput& raw) {
    raw_ = raw;
    for (size_t p = 0; p < kMaxPads; ++p) {
      Vec2 l = applyRadialDeadzone({raw.pads[p].axes[0], raw.pads[p].axes[1]}, dz_.stick);
      Vec2 r = applyRadialDeadzone({raw.pads[p].axes[2], raw.pads[p].axes[3]}, dz_.stick);
      shaped_[p].axes[0] = l.x; shaped_[p].axes[1] = l.y;
      shaped_[p].axes[2] = r.x; shaped_[p].axes[3] = r.y;
      for (size_t t = 4; t < 6; ++t) shaped_[p].axes[t] = raw.pads[p].axes[t] < dz_.trigger ? 0.f : raw.pads[p].axes[t];
    }
    for (auto& [name, binds] : bindings_) {
      f32 best = 0;
      for (const Binding& b : binds) best = std::max(best, std::abs(read(b)));
      ActionState& s = actions_[name];
      const bool wasDown = s.down;
      s.value = std::min(1.f, best);
      s.down = s.value > 0.5f;
      s.pressed = s.down && !wasDown;
      s.released = !s.down && wasDown;
    }
  }

  ActionState action(const std::string& name) const {
    auto it = actions_.find(name);
    return it == actions_.end() ? ActionState{} : it->second;
  }
  bool down(const std::string& n) const { return action(n).down; }
  bool pressed(const std::string& n) const { return action(n).pressed; }
  bool released(const std::string& n) const { return action(n).released; }
  f32 value(const std::string& n) const { return action(n).value; }

  Vec2 vec2(const std::string& name) const {
    auto it = vec2_.find(name);
    if (it == vec2_.end()) return {};
    const auto& v = it->second;
    Vec2 out{value(v.right) - value(v.left), value(v.down) - value(v.up)};
    const f32 mag = std::sqrt(out.x * out.x + out.y * out.y);
    if (mag > 1.f) { out.x /= mag; out.y /= mag; }
    return out;
  }

  bool padConnected(size_t p) const { return p < kMaxPads && raw_.pads[p].connected; }
  Vec2 mouseDelta() const { return raw_.mouseDelta; }

private:
  struct Shaped { f32 axes[6] = {}; };
  struct Vec2Names { std::string left, right, up, down; };

  f32 read(const Binding& b) const {
    switch (b.kind) {
    case DeviceKind::Key: return b.code < kMaxKeys && raw_.keys[b.code] ? b.scale : 0.f;
    case DeviceKind::Mouse: return b.code < 5 && raw_.mouse[b.code] ? b.scale : 0.f;
    case DeviceKind::PadButton:
      return b.pad < kMaxPads && raw_.pads[b.pad].connected && raw_.pads[b.pad].buttons[b.code] ? b.scale : 0.f;
    case DeviceKind::PadAxisPositive:
    case DeviceKind::PadAxisNegative: {
      if (b.pad >= kMaxPads || !raw_.pads[b.pad].connected || b.code >= 6) return 0.f;
      const f32 a = shaped_[b.pad].axes[b.code];
      const f32 signedA = b.kind == DeviceKind::PadAxisPositive ? a : -a;
      return signedA > 0 ? signedA * b.scale : 0.f;
    }
    }
    return 0.f;
  }

  DeadzoneConfig dz_;
  RawInput raw_;
  Shaped shaped_[kMaxPads];
  std::unordered_map<std::string, std::vector<Binding>> bindings_;
  std::unordered_map<std::string, Vec2Names> vec2_;
  std::unordered_map<std::string, ActionState> actions_;
};

} // namespace tool
