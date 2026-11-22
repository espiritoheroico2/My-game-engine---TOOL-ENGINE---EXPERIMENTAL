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
// Eixos do gamepad: sticks (-1..1) e gatilhos (0..1).
enum class PadAxis : u8 { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

constexpr size_t kMaxPads = 4;
constexpr size_t kMaxKeys = 512;

// Vetor 2D simples.
struct Vec2 { f32 x = 0, y = 0; };

// Estado cru de um gamepad no quadro: conectado, botoes e eixos.
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

// Zonas mortas: radial para os sticks e linear para os gatilhos.
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

// Tipo de entrada de uma vinculacao: tecla, botao do mouse, botao ou eixo de gamepad.
enum class DeviceKind : u8 { Key, Mouse, PadButton, PadAxisPositive, PadAxisNegative };
// Vinculacao de uma acao a uma entrada fisica, com escala (scale).
struct Binding {
  DeviceKind kind = DeviceKind::Key;
  u16 code = 0;     // tecla / botao / eixo
  u8 pad = 0;       // indice do controle (ignorado em teclado)
  f32 scale = 1.f;
};

// Estado de uma acao no quadro: valor 0..1 e se esta pressionada, acabou de ser pressionada ou solta.
struct ActionState {
  f32 value = 0;      // 0..1 (analogico) ou 0/1
  bool down = false, pressed = false, released = false;
};

// Recebe o RawInput de cada quadro e resolve acoes nomeadas, eixos e zonas mortas.
class InputSystem {
public:
  // Define as zonas mortas usadas em update().
  void setDeadzones(DeadzoneConfig d) { dz_ = d; }

  // Acoes: varias vinculacoes por acao (teclado + controle ao mesmo tempo).
  void bind(const std::string& action, Binding b) { bindings_[action].push_back(b); }
  // Remove todas as vinculacoes da acao.
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

  // Estado atual da acao. Acao desconhecida devolve estado zerado.
  ActionState action(const std::string& name) const {
    auto it = actions_.find(name);
    return it == actions_.end() ? ActionState{} : it->second;
  }
  // Verdadeiro se a acao esta ativa (valor acima de 0.5).
  bool down(const std::string& n) const { return action(n).down; }
  // Verdadeiro so no quadro em que a acao foi acionada (borda de subida).
  bool pressed(const std::string& n) const { return action(n).pressed; }
  // Verdadeiro so no quadro em que a acao foi solta (borda de descida).
  bool released(const std::string& n) const { return action(n).released; }
  // Valor da acao em 0..1 (analogico, ou 0/1 para digital).
  f32 value(const std::string& n) const { return action(n).value; }

  // Eixo nomeado (criado por bindVec2), com magnitude limitada a 1. Nome desconhecido devolve zero.
  Vec2 vec2(const std::string& name) const {
    auto it = vec2_.find(name);
    if (it == vec2_.end()) return {};
    const auto& v = it->second;
    Vec2 out{value(v.right) - value(v.left), value(v.down) - value(v.up)};
    const f32 mag = std::sqrt(out.x * out.x + out.y * out.y);
    if (mag > 1.f) { out.x /= mag; out.y /= mag; }
    return out;
  }

  // Verdadeiro se o gamepad p esta conectado neste quadro.
  bool padConnected(size_t p) const { return p < kMaxPads && raw_.pads[p].connected; }
  // Deslocamento do mouse neste quadro.
  Vec2 mouseDelta() const { return raw_.mouseDelta; }

private:
  // Eixos do gamepad ja com zona morta aplicada (6 valores).
  struct Shaped { f32 axes[6] = {}; };
  // Nomes das acoes que formam um eixo: esquerda, direita, cima e baixo.
  struct Vec2Names { std::string left, right, up, down; };

  // Le o valor bruto de uma vinculacao, multiplicado pela escala.
  // Atenção: o caso PadButton nao confere b.code com PadButton::Count; buttons[] tem 15 posicoes.
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
