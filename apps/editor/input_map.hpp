// ToolEngine Editor - MAPA DE BOTOES (Project/input_map.json). Sem SDL aqui: so dados, JSON e a ligacao com tool::InputSystem.
//
// MODELO
//   Mapa de controle ("scheme"): um por jogador/controle. Ex.: "Player 1" (teclado + gamepad 1), "Player 2" (outras teclas + gamepad 2).
//   Cada mapa tem varias ACOES. Uma acao pode ser:
//     Botao (kind 0): "jump"  -> tecla (Space)  OU  botao do gamepad (A/South). Os dois funcionam ao mesmo tempo.
//     Eixo  (kind 1): "moveX" -> tecla negativa + tecla positiva (A / D)  OU  analogico do gamepad (stick esquerdo X).
//   Os botoes do gamepad sao por POSICAO (South = A no Xbox = X no PlayStation). No Windows o SDL3 le controles Xbox pelo XInput.
//
// USO NOS TOOLS/SCRIPTS: ed.input.down("Player 1/jump"), ed.input.pressed(...), ed.axis(scheme, "moveX").
// SEGURANCA: arquivo de texto editavel -> limites de tamanho/quantidade e codigos validados na leitura.
#pragma once
#include "project.hpp"
#include "tool/input/input.hpp"

#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace editor {

// Nomes dos botoes/eixos do gamepad na ordem de tool::PadButton / tool::PadAxis (usados nas listas da UI).
inline constexpr std::array<const char*, 15> kPadButtonNames = {"A / Cross (South)", "B / Circle (East)", "X / Square (West)", "Y / Triangle (North)", "Left bumper (LB)", "Right bumper (RB)",
                                                                  "Back / Select", "Start", "Guide", "Left stick press (L3)", "Right stick press (R3)", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"};
inline constexpr std::array<const char*, 6> kPadAxisNames = {"Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "Left trigger (LT)", "Right trigger (RT)"};

struct MapRow {
  char action[32] = "action";
  int kind = 0;                 // 0 = botao, 1 = eixo
  int key = 0, keyNeg = 0;      // codigos SDL_Scancode (0 = nenhum). Botao: `key`. Eixo: `keyNeg` (-) e `key` (+)
  int padButton = -1;           // 0..14 (tool::PadButton) ou -1
  int padAxis = -1;             // 0..5 (tool::PadAxis) ou -1 (so eixo)
  bool padInvert = false;       // inverte o eixo do gamepad (stick para cima = negativo no SDL)
};
struct Scheme {
  char name[24] = "Player 1";
  int pad = 0;                  // qual gamepad (0..3) este mapa usa; -1 = so teclado
  std::vector<MapRow> rows;
};
constexpr size_t kMaxSchemes = 16, kMaxRows = 64;

class InputMap {
 public:
  std::vector<Scheme> schemes;

  // Mapa inicial: um jogador com pular, mover, olhar. Teclas = SDL_Scancode (Space 44, A 4, D 7, W 26, S 22, E 8, Q 20).
  static InputMap defaults() {
    InputMap m; Scheme s;
    auto btn = [](const char* n, int key, int pad) { MapRow r; std::snprintf(r.action, sizeof r.action, "%s", n); r.kind = 0; r.key = key; r.padButton = pad; return r; };
    auto axis = [](const char* n, int neg, int pos, int padAxis, bool inv) { MapRow r; std::snprintf(r.action, sizeof r.action, "%s", n); r.kind = 1; r.keyNeg = neg; r.key = pos; r.padAxis = padAxis; r.padInvert = inv; return r; };
    s.rows.push_back(btn("jump", 44, 0));            // Space / A
    s.rows.push_back(btn("fire", 224, 2));           // Left Ctrl / X
    s.rows.push_back(axis("moveX", 4, 7, 0, false)); // A D / stick esquerdo X
    s.rows.push_back(axis("moveY", 22, 26, 1, true)); // S W / stick esquerdo Y (para frente = positivo)
    m.schemes.push_back(s);
    return m;
  }

  // Chave da acao no InputSystem: "Player 1/jump". Eixos viram duas acoes: "Player 1/moveX-" e "Player 1/moveX+".
  static std::string key(const Scheme& s, const MapRow& r, char sign = 0) { return std::string(s.name) + "/" + r.action + (sign ? std::string(1, sign) : std::string()); }

  // Reconstroi os vinculos no InputSystem (chame depois de editar o mapa).
  void apply(tool::InputSystem& in) {
    for (auto& k : bound_) in.clearBindings(k);
    bound_.clear();
    for (const Scheme& s : schemes) {
      const uint8_t pad = static_cast<uint8_t>(s.pad < 0 ? 0 : s.pad);
      for (const MapRow& r : s.rows) {
        auto bindKey = [&](const std::string& k, int sc, float scale = 1.f) { if (sc > 0 && sc < static_cast<int>(tool::kMaxKeys)) { in.bind(k, {tool::DeviceKind::Key, static_cast<uint16_t>(sc), 0, scale}); bound_.insert(k); } };
        if (r.kind == 0) {
          const std::string k = key(s, r); bindKey(k, r.key);
          if (s.pad >= 0 && r.padButton >= 0 && r.padButton < static_cast<int>(tool::PadButton::Count)) { in.bind(k, {tool::DeviceKind::PadButton, static_cast<uint16_t>(r.padButton), pad, 1.f}); bound_.insert(k); }
        } else {
          const std::string kp = key(s, r, '+'), kn = key(s, r, '-');
          bindKey(kp, r.key); bindKey(kn, r.keyNeg);
          if (s.pad >= 0 && r.padAxis >= 0 && r.padAxis < static_cast<int>(tool::PadAxis::Count)) {
            in.bind(kp, {r.padInvert ? tool::DeviceKind::PadAxisNegative : tool::DeviceKind::PadAxisPositive, static_cast<uint16_t>(r.padAxis), pad, 1.f});
            in.bind(kn, {r.padInvert ? tool::DeviceKind::PadAxisPositive : tool::DeviceKind::PadAxisNegative, static_cast<uint16_t>(r.padAxis), pad, 1.f});
            bound_.insert(kp); bound_.insert(kn);
          }
        }
      }
    }
  }

  // Valor de um eixo (-1..1) do mapa `scheme` (0 = primeiro).
  float axis(const tool::InputSystem& in, size_t scheme, const std::string& action) const {
    if (scheme >= schemes.size()) return 0.f;
    return in.value(std::string(schemes[scheme].name) + "/" + action + "+") - in.value(std::string(schemes[scheme].name) + "/" + action + "-");
  }
  bool down(const tool::InputSystem& in, size_t scheme, const std::string& action) const { return scheme < schemes.size() && in.down(std::string(schemes[scheme].name) + "/" + action); }
  bool pressed(const tool::InputSystem& in, size_t scheme, const std::string& action) const { return scheme < schemes.size() && in.pressed(std::string(schemes[scheme].name) + "/" + action); }

  tool::Json toJson() const {
    tool::Json root = tool::Json::object(), arr = tool::Json::array();
    for (const Scheme& s : schemes) {
      tool::Json o = tool::Json::object(), rows = tool::Json::array();
      o.set("name", std::string(s.name)); o.set("pad", s.pad);
      for (const MapRow& r : s.rows) {
        tool::Json q = tool::Json::object();
        q.set("action", std::string(r.action)); q.set("kind", r.kind); q.set("key", r.key); q.set("keyNeg", r.keyNeg);
        q.set("padButton", r.padButton); q.set("padAxis", r.padAxis); q.set("padInvert", r.padInvert); rows.push(q);
      }
      o.set("actions", rows); arr.push(o);
    }
    root.set("version", 1); root.set("schemes", arr); return root;
  }
  bool fromJson(const tool::Json& j) {
    if (!j.isObject()) return false;
    std::vector<Scheme> out;
    for (auto& o : j["schemes"].items()) {
      if (out.size() >= kMaxSchemes || !o.isObject()) break;
      Scheme s; ser::copyStr(s.name, sizeof s.name, o["name"].asString().empty() ? "Player" : o["name"].asString());
      s.pad = std::clamp(static_cast<int>(o["pad"].asNumber(0)), -1, 3);
      for (auto& q : o["actions"].items()) {
        if (s.rows.size() >= kMaxRows || !q.isObject()) break;
        MapRow r; ser::copyStr(r.action, sizeof r.action, q["action"].asString().empty() ? "action" : q["action"].asString());
        r.kind = std::clamp(static_cast<int>(q["kind"].asNumber(0)), 0, 1);
        r.key = std::clamp(static_cast<int>(q["key"].asNumber(0)), 0, static_cast<int>(tool::kMaxKeys) - 1); r.keyNeg = std::clamp(static_cast<int>(q["keyNeg"].asNumber(0)), 0, static_cast<int>(tool::kMaxKeys) - 1);
        r.padButton = std::clamp(static_cast<int>(q["padButton"].asNumber(-1)), -1, static_cast<int>(tool::PadButton::Count) - 1);
        r.padAxis = std::clamp(static_cast<int>(q["padAxis"].asNumber(-1)), -1, static_cast<int>(tool::PadAxis::Count) - 1); r.padInvert = ser::flag(q, "padInvert", false);
        s.rows.push_back(r);
      }
      out.push_back(s);
    }
    if (out.empty()) return false;
    schemes = std::move(out); return true;
  }
  bool save(const Project& p) const { return writeTextFile(p.root / "input_map.json", toJson().dump()); }
  bool load(const Project& p) {
    std::string text; if (!readTextFile(p.root / "input_map.json", text, 1u << 20)) return false;
    auto j = tool::Json::parse(text); return j && fromJson(*j);
  }

 private:
  std::set<std::string> bound_;
};

} // namespace editor
