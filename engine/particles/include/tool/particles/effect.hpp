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
#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "tool/core/json.hpp"
#include "tool/particles/modules.hpp"

namespace tool::particles {

// Shader do efeito (nomes fixos no JSON): "additive", "alpha", "bloom". A biblioteca so carrega e expoe; o renderer desenha.
enum class ShaderKind : u8 { Alpha, Additive, Bloom };

// Efeito de particulas (dado puro, sem estado de execucao).
struct Effect {
  std::string name;
  ShaderKind shader = ShaderKind::Alpha;
  f32 emissive = 0;     // ganho de emissivo para o pos-processo de bloom (>= 0)
  u32 material = 0;     // indice do material do efeito (copiado para cada quad)
  f32 soft = 0;         // distancia (m) do fade suave perto de superficies ("soft particles"); 0 = desligado
  u32 form = 0;         // forma do sprite: 0 bolinha, 1 quadrado, 2 triangulo, 3 estrela, 4 anel, 5 risco, 6 fumaca (so a vista por software a desenha; o shader da GPU usa o sprite padrao)
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
  if (em.has("shape")) {
    const std::string& s = em["shape"].asString();
    if (s == "point") fx.emitter.shape = EmitShape::Point;
    else if (s == "sphere") fx.emitter.shape = EmitShape::Sphere;
    else if (s == "box") fx.emitter.shape = EmitShape::Box;
    else if (s == "cone") fx.emitter.shape = EmitShape::Cone;
    else if (s == "disc") fx.emitter.shape = EmitShape::Disc;
    else if (s == "line") fx.emitter.shape = EmitShape::Line;
    else return std::nullopt;   // formato desconhecido: falha fechada
  }
  detail::vec3(em["box"], fx.emitter.boxHalf);
  if (fx.emitter.radius < 0 || fx.emitter.boxHalf[0] < 0 || fx.emitter.boxHalf[1] < 0 || fx.emitter.boxHalf[2] < 0)
    return std::nullopt;
  if (j.has("shader")) {
    const std::string& s = j["shader"].asString();
    if (s == "alpha") fx.shader = ShaderKind::Alpha;
    else if (s == "additive") fx.shader = ShaderKind::Additive;
    else if (s == "bloom") fx.shader = ShaderKind::Bloom;
    else return std::nullopt;
  }
  fx.emissive = detail::num(j["emissive"], 0);
  if (!(fx.emissive >= 0 && fx.emissive <= 1000)) return std::nullopt;   // tambem rejeita NaN
  fx.soft = detail::num(j["soft"], 0);
  if (!(fx.soft >= 0 && fx.soft <= 100)) return std::nullopt;            // distancia do fade suave: 0..100 m
  if (j.has("form")) {
    const double fm = j["form"].asNumber(0);
    if (!(fm >= 0 && fm <= 6)) return std::nullopt;
    fx.form = static_cast<u32>(fm);
  }
  if (j["material"].isNumber()) fx.material = static_cast<u32>(std::max(0.0, j["material"].asNumber()));
  for (const Json& b : em["bursts"].items())
    fx.emitter.bursts.push_back({detail::num(b["time"], 0), static_cast<u32>(std::max(0.0, b["count"].asNumber()))});
  const Json& in = j["init"];
  detail::pair(in["life"], fx.init.lifeMin, fx.init.lifeMax);
  detail::pair(in["speed"], fx.init.speedMin, fx.init.speedMax);
  detail::pair(in["size"], fx.init.sizeMin, fx.init.sizeMax);
  detail::vec3(in["direction"], fx.init.dir);
  fx.init.coneDeg = detail::num(in["coneAngle"], fx.init.coneDeg);
  fx.init.color = detail::color(in["color"], fx.init.color);
  fx.init.temp0 = detail::num(in["temperature"], fx.init.temp0);
  detail::pair(in["rotation"], fx.init.rotMinDeg, fx.init.rotMaxDeg);
  if (in.has("velocity")) {
    const std::string& s = in["velocity"].asString();
    if (s == "cone") fx.init.velMode = VelMode::Cone;
    else if (s == "radial") fx.init.velMode = VelMode::Radial;
    else if (s == "random") fx.init.velMode = VelMode::Random;
    else return std::nullopt;
  }
  for (const Json& m : j["modules"].items()) {
    Module mod;
    const std::string& t = m["type"].asString();
    if (t == "gravity") { mod.kind = ModuleKind::Gravity; detail::vec3(m["value"], mod.g); }
    else if (t == "drag") { mod.kind = ModuleKind::Drag; mod.drag = detail::num(m["value"], 0); }
    else if (t == "colorOverLife") {
      mod.kind = ModuleKind::ColorOverLife;
      mod.c0 = detail::color(m["from"], 0xFFFFFFFFu); mod.c1 = detail::color(m["to"], 0xFFFFFFFFu);
      // Paradas intermediarias opcionais (1/3 e 2/3 da vida): com as duas presentes a cor passa por 4 paradas.
      if (m["mid1"].items().size() >= 3 && m["mid2"].items().size() >= 3) {
        mod.c2 = detail::color(m["mid1"], mod.c0); mod.c3 = detail::color(m["mid2"], mod.c1); mod.stops = 4;
      }
    } else if (t == "sizeOverLife") {
      mod.kind = ModuleKind::SizeOverLife;
      mod.s0 = detail::num(m["from"], 1); mod.s1 = detail::num(m["to"], 1);
    } else if (t == "turbulence") {
      mod.kind = ModuleKind::Turbulence;
      mod.amp = detail::num(m["amplitude"], 1); mod.freq = detail::num(m["frequency"], 0.5f);
      mod.scroll = detail::num(m["scroll"], 0.3f);
      mod.noiseSeed = static_cast<u32>(std::max(0.0, m["seed"].asNumber()));
      if (mod.freq <= 0) return std::nullopt;
      // Taxa de amostragem: quadros entre atualizacoes do ruido. Ausente = 1 (todo quadro, qualidade maxima).
      if (m.has("every")) {
        const double ev = m["every"].asNumber(1);
        if (!(ev >= 1 && ev <= 64)) return std::nullopt;
        mod.every = static_cast<u32>(ev);
      }
    } else if (t == "buoyancy") {
      mod.kind = ModuleKind::Buoyancy;
      mod.amp = detail::num(m["value"], 1); mod.cooling = detail::num(m["cooling"], 0);
      if (mod.cooling < 0) return std::nullopt;
    } else if (t == "alphaOverLife") {
      mod.kind = ModuleKind::AlphaOverLife;
      mod.a0 = detail::num(m["from"], 1); mod.a1 = detail::num(m["to"], 0);
      mod.delay = detail::num(m["delay"], 0);
      if (!(mod.delay >= 0 && mod.delay < 1)) return std::nullopt;
    } else if (t == "floor") {
      mod.kind = ModuleKind::Floor;
      mod.floorY = detail::num(m["y"], 0); mod.bounce = detail::num(m["bounce"], 0.5f); mod.friction = detail::num(m["friction"], 0.9f);
      if (!(mod.bounce >= 0 && mod.bounce <= 1) || !(mod.friction >= 0 && mod.friction <= 1) || !(std::fabs(mod.floorY) <= 1e6f)) return std::nullopt;
    } else if (t == "route") {
      mod.kind = ModuleKind::Route;
      mod.follow = m["follow"].asBool(true);
      const auto& pts = m["points"].items();
      if (pts.size() > kMaxRoutePoints) return std::nullopt;
      mod.routeCount = static_cast<u32>(pts.size());
      for (size_t k = 0; k < pts.size(); ++k) {
        for (int c = 0; c < 3; ++c) mod.pts[k][c] = 0;
        detail::vec3(pts[k], mod.pts[k]);
        for (int c = 0; c < 3; ++c) if (!(std::fabs(mod.pts[k][c]) <= 1e6f)) return std::nullopt;
      }
    } else if (t == "swirl") {
      mod.kind = ModuleKind::Swirl;
      mod.follow = m["follow"].asBool(true);
      detail::vec3(m["axis"], mod.axis);
      mod.swirlTurns = detail::num(m["turns"], 1.5f); mod.swirlRadius = detail::num(m["radius"], 0.6f); mod.swirlHeight = detail::num(m["height"], 2.0f);
      if (!(std::fabs(mod.swirlTurns) <= 100 && mod.swirlRadius >= 0 && mod.swirlRadius <= 1e4f && std::fabs(mod.swirlHeight) <= 1e4f)) return std::nullopt;
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

namespace detail {
inline Json jvec(const f32* v, int n) { Json a = Json::array(); for (int k = 0; k < n; ++k) a.push(static_cast<double>(v[k])); return a; }
inline Json jpair(f32 lo, f32 hi) { return jvec(std::array<f32, 2>{lo, hi}.data(), 2); }
// Cor RGBA8 -> [r,g,b,a] de 0 a 1.
inline Json jcolor(u32 c) {
  Json a = Json::array();
  for (u32 ch = 0; ch < 4; ++ch) a.push(static_cast<double>((c >> (ch * 8)) & 0xFFu) / 255.0);
  return a;
}
} // namespace detail

// Serializa o efeito no mesmo formato lido por effectFromJson (ida e volta sem perda alem do arredondamento das cores).
inline Json effectToJson(const Effect& fx) {
  Json j = Json::object();
  j.set("name", fx.name);
  j.set("shader", fx.shader == ShaderKind::Additive ? "additive" : fx.shader == ShaderKind::Bloom ? "bloom" : "alpha");
  j.set("emissive", static_cast<double>(fx.emissive));
  j.set("material", static_cast<double>(fx.material));
  j.set("soft", static_cast<double>(fx.soft));
  if (fx.form != 0) j.set("form", static_cast<double>(fx.form));
  Json em = Json::object();
  em.set("rate", static_cast<double>(fx.emitter.rate));
  static const char* kShapes[] = {"point", "sphere", "box", "cone", "disc", "line"};
  em.set("shape", kShapes[std::min<int>(static_cast<int>(fx.emitter.shape), 5)]);
  em.set("radius", static_cast<double>(fx.emitter.radius));
  em.set("box", detail::jvec(fx.emitter.boxHalf, 3));
  em.set("seed", static_cast<double>(fx.emitter.seed));
  em.set("duration", static_cast<double>(fx.emitter.duration));
  em.set("loop", fx.emitter.loop);
  Json bs = Json::array();
  for (const Burst& b : fx.emitter.bursts) { Json o = Json::object(); o.set("time", static_cast<double>(b.time)); o.set("count", static_cast<double>(b.count)); bs.push(o); }
  em.set("bursts", bs);
  j.set("emitter", em);
  Json in = Json::object();
  in.set("life", detail::jpair(fx.init.lifeMin, fx.init.lifeMax));
  in.set("speed", detail::jpair(fx.init.speedMin, fx.init.speedMax));
  in.set("size", detail::jpair(fx.init.sizeMin, fx.init.sizeMax));
  in.set("direction", detail::jvec(fx.init.dir, 3));
  in.set("coneAngle", static_cast<double>(fx.init.coneDeg));
  in.set("color", detail::jcolor(fx.init.color));
  in.set("temperature", static_cast<double>(fx.init.temp0));
  in.set("rotation", detail::jpair(fx.init.rotMinDeg, fx.init.rotMaxDeg));
  in.set("velocity", fx.init.velMode == VelMode::Radial ? "radial" : fx.init.velMode == VelMode::Random ? "random" : "cone");
  j.set("init", in);
  Json ms = Json::array();
  for (const Module& m : fx.modules) {
    Json o = Json::object();
    switch (m.kind) {
    case ModuleKind::Gravity: o.set("type", "gravity"); o.set("value", detail::jvec(m.g, 3)); break;
    case ModuleKind::Drag: o.set("type", "drag"); o.set("value", static_cast<double>(m.drag)); break;
    case ModuleKind::ColorOverLife:
      o.set("type", "colorOverLife"); o.set("from", detail::jcolor(m.c0)); o.set("to", detail::jcolor(m.c1));
      if (m.stops >= 4) { o.set("mid1", detail::jcolor(m.c2)); o.set("mid2", detail::jcolor(m.c3)); }
      break;
    case ModuleKind::Floor:
      o.set("type", "floor"); o.set("y", static_cast<double>(m.floorY)); o.set("bounce", static_cast<double>(m.bounce)); o.set("friction", static_cast<double>(m.friction)); break;
    case ModuleKind::Route: {
      o.set("type", "route"); o.set("follow", m.follow);
      Json pts = Json::array();
      for (u32 k = 0; k < m.routeCount && k < kMaxRoutePoints; ++k) pts.push(detail::jvec(m.pts[k], 3));
      o.set("points", pts);
      break;
    }
    case ModuleKind::Swirl:
      o.set("type", "swirl"); o.set("follow", m.follow); o.set("axis", detail::jvec(m.axis, 3)); o.set("turns", static_cast<double>(m.swirlTurns));
      o.set("radius", static_cast<double>(m.swirlRadius)); o.set("height", static_cast<double>(m.swirlHeight)); break;
    case ModuleKind::SizeOverLife: o.set("type", "sizeOverLife"); o.set("from", static_cast<double>(m.s0)); o.set("to", static_cast<double>(m.s1)); break;
    case ModuleKind::Turbulence:
      o.set("type", "turbulence"); o.set("amplitude", static_cast<double>(m.amp)); o.set("frequency", static_cast<double>(m.freq));
      o.set("scroll", static_cast<double>(m.scroll)); o.set("seed", static_cast<double>(m.noiseSeed)); o.set("every", static_cast<double>(m.every)); break;
    case ModuleKind::Buoyancy: o.set("type", "buoyancy"); o.set("value", static_cast<double>(m.amp)); o.set("cooling", static_cast<double>(m.cooling)); break;
    case ModuleKind::AlphaOverLife:
      o.set("type", "alphaOverLife"); o.set("from", static_cast<double>(m.a0)); o.set("to", static_cast<double>(m.a1));
      if (m.delay > 0) o.set("delay", static_cast<double>(m.delay));
      break;
    }
    ms.push(o);
  }
  j.set("modules", ms);
  return j;
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
