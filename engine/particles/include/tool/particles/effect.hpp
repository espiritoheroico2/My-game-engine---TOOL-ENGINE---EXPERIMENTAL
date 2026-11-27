// ToolEngine - Particulas: efeito = dado (emissor + inicializacao + lista ordenada de modulos), carregado de JSON.
// Formato (todos os campos sao opcionais; cores sao [r,g,b,a] de 0 a 1):
// {
//   "name": "faiscas",
//   "emitter": { "rate": 100, "shape": "point"|"sphere", "radius": 1, "seed": 7, "duration": 0, "loop": true,
//                "bursts": [ {"time": 0, "count": 50} ] },
//   "init":    { "life": [0.5, 1.5], "speed": [2, 5], "direction": [0,1,0], "coneAngle": 30,
//                "size": [0.1, 0.2], "color": [1,0.8,0.2,1] },
//   "modules": [ {"type":"gravity","value":[0,-9.8,0]}, {"type":"drag","value":0.5},
//                {"type":"colorOverLife","from":[1,1,0,1],"to":[1,0,0,0]}, {"type":"sizeOverLife","from":1,"to":0} ]
// }
#pragma once
#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "tool/core/json.hpp"
#include "tool/particles/modules.hpp"

namespace tool::particles {

// Efeito de particulas (dado puro, sem estado de execucao).
struct Effect {
  std::string name;
  EmitterDesc emitter;
  InitParams init;
  std::vector<Module> modules;   // aplicados na ordem da lista (por fase)
};

namespace detail {
// Le um numero do JSON (ou o padrao d).
inline f32 num(const Json& j, f32 d) { return j.isNumber() ? static_cast<f32>(j.asNumber()) : d; }
// Le um par [min, max] (ou um numero unico, que vale para os dois).
inline void pair(const Json& j, f32& lo, f32& hi) {
  if (j.isNumber()) { lo = hi = static_cast<f32>(j.asNumber()); return; }
  const auto& a = j.items();
  if (a.size() >= 2) { lo = num(a[0], lo); hi = num(a[1], hi); }
}
// Le um vetor de 3 floats.
inline void vec3(const Json& j, f32 out[3]) {
  const auto& a = j.items();
  if (a.size() >= 3) for (int k = 0; k < 3; ++k) out[k] = num(a[static_cast<size_t>(k)], out[k]);
}
// Le uma cor [r,g,b,a] (0..1) em RGBA8; mantem d se ausente.
inline u32 color(const Json& j, u32 d) {
  const auto& a = j.items();
  if (a.size() < 3) return d;
  return packColor(num(a[0], 1), num(a[1], 1), num(a[2], 1), a.size() > 3 ? num(a[3], 1) : 1.0f);
}
} // namespace detail

// Monta um efeito a partir de um valor JSON. Devolve nullopt se a raiz nao for objeto ou um modulo for desconhecido.
inline std::optional<Effect> effectFromJson(const Json& j) {
  if (!j.isObject()) return std::nullopt;
  Effect fx;
  fx.name = j["name"].asString();
  const Json& em = j["emitter"];
  fx.emitter.rate = detail::num(em["rate"], fx.emitter.rate);
  fx.emitter.radius = detail::num(em["radius"], fx.emitter.radius);
  fx.emitter.duration = detail::num(em["duration"], fx.emitter.duration);
  fx.emitter.loop = em["loop"].asBool(fx.emitter.loop);
  if (em["seed"].isNumber()) fx.emitter.seed = static_cast<u64>(em["seed"].asNumber());
  fx.emitter.shape = em["shape"].asString() == "sphere" ? EmitShape::Sphere : EmitShape::Point;
  for (const Json& b : em["bursts"].items())
    fx.emitter.bursts.push_back({detail::num(b["time"], 0), static_cast<u32>(std::max(0.0, b["count"].asNumber()))});
  const Json& in = j["init"];
  detail::pair(in["life"], fx.init.lifeMin, fx.init.lifeMax);
  detail::pair(in["speed"], fx.init.speedMin, fx.init.speedMax);
  detail::pair(in["size"], fx.init.sizeMin, fx.init.sizeMax);
  detail::vec3(in["direction"], fx.init.dir);
  fx.init.coneDeg = detail::num(in["coneAngle"], fx.init.coneDeg);
  fx.init.color = detail::color(in["color"], fx.init.color);
  for (const Json& m : j["modules"].items()) {
    Module mod;
    const std::string& t = m["type"].asString();
    if (t == "gravity") { mod.kind = ModuleKind::Gravity; detail::vec3(m["value"], mod.g); }
    else if (t == "drag") { mod.kind = ModuleKind::Drag; mod.drag = detail::num(m["value"], 0); }
    else if (t == "colorOverLife") {
      mod.kind = ModuleKind::ColorOverLife;
      mod.c0 = detail::color(m["from"], 0xFFFFFFFFu); mod.c1 = detail::color(m["to"], 0xFFFFFFFFu);
    } else if (t == "sizeOverLife") {
      mod.kind = ModuleKind::SizeOverLife;
      mod.s0 = detail::num(m["from"], 1); mod.s1 = detail::num(m["to"], 1);
    } else return std::nullopt;   // modulo desconhecido: falha fechada
    fx.modules.push_back(mod);
  }
  return fx;
}

// Carrega um efeito de um texto JSON.
inline std::optional<Effect> effectFromText(const std::string& text) {
  auto j = Json::parse(text);
  if (!j) return std::nullopt;
  return effectFromJson(*j);
}

// Carrega um efeito de um arquivo .json.
inline std::optional<Effect> loadEffectFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return effectFromText(ss.str());
}

} // namespace tool::particles
