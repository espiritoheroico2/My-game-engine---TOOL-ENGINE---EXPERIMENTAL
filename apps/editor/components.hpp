// ToolEngine Editor - componentes dos objetos da cena e sua (de)serializacao em JSON.
//
// Todo objeto do editor e uma Entity do ECS com alguns destes componentes:
//   Name, Transform            -> todo objeto
//   MeshRenderer               -> desenha uma forma (cubo/esfera/plano) ou um modelo importado (glTF/FBX)
//   LightComp                  -> luz pontual/spot/direcional comum
//   GlobalLight                -> "sol" do mundo: cor + direcao + ciclo dinamico (so um e usado)
//   CameraComp                 -> camera do jogo; com a tag "Principal" ela e a camera ativa
//   AudioSource                -> toca um som (wav/mp3) do projeto
//   PhysicsComp                -> corpo fisico Newtoon (estatico/dinamico/cinematico) + collider + "vector physics"
//   ToolsComp                  -> "Tools": comportamentos prontos acoplados ao objeto (Jump, Move, Follow, Spin, OnCollision, Script lib)
//   ScriptComp                 -> script ToolObject (arquivo .tool.js em Assets/Scripts): start/update/OnDelete/DeleteAndReuse
// A mesma funcao (saveEntity/loadEntity) grava e le cena inteira, entao o formato do arquivo .scene.json
// e documentado aqui: ver saveEntity().
#pragma once
#include "tool/audio/audio.hpp"
#include "tool/core/json.hpp"
#include "tool/math/math.hpp"
#include "tool/render/lighting.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/soft_renderer.hpp"
#include "tool/scene/scene.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

constexpr float kDeg2Rad = 3.14159265f / 180.f;

struct Name { char text[40] = "Object"; };
struct Transform { float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1}; };

// shape: 0 cubo, 1 esfera, 2 plano, 3 modelo importado (caminho relativo ao projeto em `model`).
struct MeshRenderer {
  int shape = 0;
  float color[3] = {0.8f, 0.3f, 0.2f};
  float roughness = 0.5f, metallic = 0.f;
  char model[160] = "";
  char texture[160] = "";     // textura de albedo (PNG/JPG/BMP/TGA em Assets); vazio = sem textura
  // Sombras, independentes entre si: um objeto pode projetar sem receber (ex.: um tronco) ou receber sem projetar (ex.: chao).
  bool castShadow = true, receiveShadow = true;
};
// Hierarquia: o Transform de um objeto com Parent e LOCAL (relativo ao pai). Sem Parent, e o mundo. Ver editor_hier.hpp.
struct Parent { tool::Entity e; };
struct LightComp { tool::Light light; };

// Luz global (sol). direcao = (azimute, elevacao) em graus: elevacao 90 = sol a pino, 0 = horizonte.
// Se `dynamic`, durante o Play o azimute gira `speed` graus/segundo e a intensidade cai quando o sol
// passa abaixo do horizonte (ciclo dia/noite simples). O valor salvo na cena nao muda.
struct GlobalLight {
  float color[3] = {1.f, 0.95f, 0.85f};
  float intensity = 2.2f;
  float azimuth = 35.f, elevation = 55.f;
  float ambient[3] = {0.03f, 0.03f, 0.04f};
  bool dynamic = false;
  float speed = 10.f;
  bool castShadow = true;  // o sol projeta sombras (se Settings > Lighting tambem estiver ligado)
};

// Camera do jogo. A camera ativa e a que tem a tag "Principal". Duas Principais = tela dividida.
struct CameraComp {
  float fovDeg = 55.f, nearZ = 0.1f, farZ = 200.f;
  bool ortho2D = false;    // 2D: projecao ortografica (sem perspectiva). orthoSize = altura visivel em unidades do mundo
  float orthoSize = 10.f;
  bool soundEar = false;   // "ouvido": esta camera e o ouvinte do audio 3D
  float earVolume = 1.f;
  tool::PostFx post;       // pos-processamento LOCAL (so desta camera); combina com o GLOBAL da cena (ver postfx.hpp)
};

// Script do objeto. `path` = arquivo relativo ao projeto, sempre dentro de "Assets/" (ver script.hpp).
// Todo objeto criado no editor ja nasce com um script ToolObject (start/update/OnDelete/DeleteAndReuse).
struct ScriptComp {
  char path[160] = "";
  bool enabled = true;
};

// Corpo fisico (Newtoon). body: 0 estatico (parede/chao), 1 dinamico (cai e colide), 2 cinematico (so move por ordem sua).
// collider: 0 = automatico (pela forma do MeshRenderer), 1 = caixa, 2 = esfera. O tamanho vem da ESCALA do objeto.
// VECTOR PHYSICS: com `overrideGravity` o objeto usa `gravity` (m/s^2, qualquer direcao) no lugar da gravidade do mundo.
struct PhysicsComp {
  int body = 1, collider = 0;
  float mass = 1.f, friction = 0.5f, bounciness = 0.f;
  float linearDamping = 0.01f, angularDamping = 0.05f, gravityScale = 1.f;
  bool useGravity = true, freezeRotation = false, ccd = false, trigger = false;
  bool overrideGravity = false;
  float gravity[3] = {0.f, -9.81f, 0.f};
};

// Tool = comportamento pronto que se "acopla" ao objeto (como adicionar um addon ao codigo principal dele).
enum class ToolKind : int { Jump = 0, Move = 1, FollowTarget = 2, Spin = 3, OnCollision = 4, ScriptLib = 5, Count };
struct ToolItem {
  int kind = 0; bool enabled = true;
  char action[40] = "";   // Jump/Move: nome da acao do Input Map (ex.: "jump"); ScriptLib: caminho do arquivo .js
  char target[40] = "";   // FollowTarget: nome do objeto; OnCollision: tag do outro objeto (vazio = qualquer)
  float a = 0.f, b = 0.f, c = 0.f, d = 0.f;  // parametros (ver a janela Help e docs/PHYSICS.md)
  int player = 1;         // qual "controle" do Input Map (1 = primeiro)
  int mode = 0;           // OnCollision: 0 registrar no Console, 1 destruir o outro, 2 destruir este, 3 quicar
};
constexpr int kMaxTools = 8;
struct ToolsComp { int count = 0; ToolItem items[kMaxTools]; };

// UI de jogo: camada 2D desenhada por cima da imagem de uma camera. Posicao/tamanho em FRACAO da tela (0..1), entao funciona em qualquer resolucao.
enum class UiKind : int { Text = 0, Panel, Button, Bar, Count };
struct UiElem {
  int kind = 0; char text[64] = "Text"; float x = 0.05f, y = 0.05f, w = 0.2f, h = 0.08f; float color[4] = {1.f, 1.f, 1.f, 1.f};
  char action[40] = "";  // Button: nome do evento enviado ao clicar. Bar: nome da variavel mostrada.
  float maxValue = 100.f; // Bar: valor que enche a barra
  float fontScale = 1.f;
};
constexpr int kMaxUi = 16;
// Objeto desativado (Inspector > Active, ou acao "setActive" do Narrative): nao desenha, nao simula, nao executa Tools, sem luz/sombra.
struct Inactive {};

struct UiCanvas { char camera[40] = ""; bool visible = true; int count = 0; UiElem items[kMaxUi]; }; // camera vazia = todas as cameras Principais

// Particulas: emissor simples (CPU). Gravidade e quique vem das configuracoes de fisica (Newtoon) da cena.
// Teste de ECS (comando "test ECS: N" no Console). Estado de execucao: nao vai para o arquivo de cena.
// Fica no objeto "ECScall": cada esfera criada e filha dele e cai com a fisica Newtoon.
struct SpawnerComp {
  long long total = 0, spawned = 0; int perFrame = 1;
  float height = 15.f, area = 20.f;   // altura de queda e lado do quadrado de queda (metros)
  bool armed = false, logged = false;
  double startedAt = -1.0;            // segundos (relogio real) do inicio da criacao
  uint32_t seed = 2463534242u;
};

struct ParticleComp {
  bool emitting = true, loop = true; int maxParticles = 200; float rate = 40.f, lifetime = 1.5f, speed = 3.f, spread = 25.f; // spread em graus em torno do eixo +Y do objeto
  float size = 0.12f, sizeEnd = 0.02f, gravityScale = 1.f, bounce = 0.f, floorY = -1000.f; // bounce>0: quica no plano y=floorY
  float color[3] = {1.f, 0.6f, 0.2f}, colorEnd[3] = {0.8f, 0.1f, 0.05f}; float alpha = 0.9f; bool worldSpace = true;
};

struct AudioSource {
  char clip[160] = "";     // caminho relativo ao projeto (ex.: "Assets/passos.mp3")
  float volume = 1.f;
  bool loop = true, playOnStart = true, spatial = true;
  float minDistance = 1.f, maxDistance = 30.f;
  tool::VoiceId voice{};   // so em execucao (nao e salvo)
};

inline tool::Mat4 worldMatrix(const Transform& t) {
  return tool::Mat4::translation({t.pos[0], t.pos[1], t.pos[2]}) * tool::Mat4::rotationY(t.rot[1] * kDeg2Rad) * tool::Mat4::rotationX(t.rot[0] * kDeg2Rad) *
         tool::Mat4::rotationZ(t.rot[2] * kDeg2Rad) * tool::Mat4::scale({t.scale[0], t.scale[1], t.scale[2]});
}

// Direcao "para frente" de um objeto (-Z local girado pela rotacao do Transform).
inline tool::Vec3 forwardOf(const Transform& t) {
  Transform r = t; r.scale[0] = r.scale[1] = r.scale[2] = 1.f; r.pos[0] = r.pos[1] = r.pos[2] = 0.f;
  const tool::Vec4 f = worldMatrix(r).mul({0, 0, -1, 0});
  return tool::normalize({f.x, f.y, f.z});
}
inline tool::Vec3 upOf(const Transform& t) {
  Transform r = t; r.scale[0] = r.scale[1] = r.scale[2] = 1.f; r.pos[0] = r.pos[1] = r.pos[2] = 0.f;
  const tool::Vec4 f = worldMatrix(r).mul({0, 1, 0, 0});
  return tool::normalize({f.x, f.y, f.z});
}

// Camera do renderizador a partir do Transform + CameraComp do objeto.
inline tool::Camera cameraOf(const Transform& t, const CameraComp& c) {
  tool::Camera cam;
  cam.eye = {t.pos[0], t.pos[1], t.pos[2]};
  cam.target = cam.eye + forwardOf(t);
  cam.up = upOf(t);
  cam.fovY = c.fovDeg * kDeg2Rad; cam.nearZ = c.nearZ; cam.farZ = c.farZ;
  cam.ortho = c.ortho2D; cam.orthoHeight = c.orthoSize;
  return cam;
}

// Direcao em que a luz VIAJA (do sol para o chao) a partir de azimute/elevacao.
inline tool::Vec3 sunDirection(float azimuthDeg, float elevationDeg) {
  const float az = azimuthDeg * kDeg2Rad, el = elevationDeg * kDeg2Rad;
  return tool::normalize({-std::cos(el) * std::sin(az), -std::sin(el), -std::cos(el) * std::cos(az)});
}

// Luz efetiva do sol neste instante (aplica o ciclo dinamico, se ligado). `runtime` = graus acumulados no Play.
inline tool::Light sunLight(const GlobalLight& g, float runtimeDeg) {
  tool::Light l; l.type = tool::LightType::Directional;
  const float az = g.azimuth + (g.dynamic ? runtimeDeg : 0.f);
  float el = g.elevation, k = 1.f;
  if (g.dynamic) { // sobe e desce: elevacao varia com o seno do angulo acumulado
    el = 15.f + 60.f * std::sin(runtimeDeg * kDeg2Rad * 0.5f + g.elevation * kDeg2Rad);
    k = std::clamp((el + 5.f) / 20.f, 0.f, 1.f); // anoitece perto do horizonte
  }
  l.direction = sunDirection(az, el);
  l.color = {g.color[0], g.color[1], g.color[2]};
  l.intensity = g.intensity * k;
  return l;
}

// ---------------------------------------------------------------------------------------------------------------
// Serializacao. Formato (um objeto por entrada em "entities"):
//   { "name": "Cube", "tags": ["Principal"],
//     "transform": {"pos":[x,y,z], "rot":[x,y,z], "scale":[x,y,z]},
//     "mesh": {"shape":0, "color":[r,g,b], "roughness":0.5, "metallic":0, "model":"Assets/x.glb", "castShadow":true, "receiveShadow":true},
//     "light": {...}, "globalLight": {...}, "camera": {..., "post": {...}}, "audio": {...}, "script": {"path": "Assets/Scripts/x.tool.js"} }
// Componentes ausentes simplesmente nao existem no objeto.
// ---------------------------------------------------------------------------------------------------------------
namespace ser {

inline tool::Json vec(const float* v, int n) { tool::Json a = tool::Json::array(); for (int i = 0; i < n; ++i) a.push(static_cast<double>(v[i])); return a; }
inline void readVec(const tool::Json& j, float* v, int n) { const auto& it = j.items(); for (int i = 0; i < n && static_cast<size_t>(i) < it.size(); ++i) v[i] = static_cast<float>(it[static_cast<size_t>(i)].asNumber(v[i])); }
inline float num(const tool::Json& j, const char* k, float d) { return j.has(k) ? static_cast<float>(j[k].asNumber(d)) : d; }
inline bool flag(const tool::Json& j, const char* k, bool d) { return j.has(k) ? j[k].asBool(d) : d; }
inline void copyStr(char* dst, size_t cap, const std::string& s) { std::snprintf(dst, cap, "%s", s.c_str()); }
inline float clampNum(const tool::Json& j, const char* k, float d, float lo, float hi) { return std::clamp(num(j, k, d), lo, hi); } // valores vindos de arquivo nunca saem da faixa valida

// Pos-processamento <-> JSON (usado na camera e na configuracao global da cena).
inline tool::Json postFx(const tool::PostFx& p) {
  tool::Json o = tool::Json::object();
  o.set("enabled", p.enabled); o.set("exposure", static_cast<double>(p.exposure)); o.set("contrast", static_cast<double>(p.contrast)); o.set("saturation", static_cast<double>(p.saturation));
  o.set("tint", vec(p.tint, 3)); o.set("tintStrength", static_cast<double>(p.tintStrength)); o.set("vignette", static_cast<double>(p.vignette));
  o.set("vignetteSize", static_cast<double>(p.vignetteSize)); o.set("sharpen", static_cast<double>(p.sharpen)); o.set("additive", p.additive);
  return o;
}
inline tool::PostFx readPostFx(const tool::Json& o) {
  tool::PostFx p;
  p.enabled = flag(o, "enabled", false); p.exposure = clampNum(o, "exposure", 0.f, -5.f, 5.f); p.contrast = clampNum(o, "contrast", 1.f, 0.2f, 3.f);
  p.saturation = clampNum(o, "saturation", 1.f, 0.f, 3.f); readVec(o["tint"], p.tint, 3); for (float& t : p.tint) t = std::clamp(t, 0.f, 1.f);
  p.tintStrength = clampNum(o, "tintStrength", 0.f, 0.f, 1.f); p.vignette = clampNum(o, "vignette", 0.f, 0.f, 1.f);
  p.vignetteSize = clampNum(o, "vignetteSize", 0.75f, 0.1f, 1.5f); p.sharpen = clampNum(o, "sharpen", 0.f, 0.f, 1.f); p.additive = flag(o, "additive", true);
  return p;
}

} // namespace ser

inline tool::Json saveEntity(tool::Scene& sc, tool::Entity e) {
  using tool::Json;
  tool::World& w = sc.world();
  Json j = Json::object();
  if (auto* n = w.get<Name>(e)) j.set("name", std::string(n->text));
  Json tags = Json::array();
  const tool::TagMask m = w.tagMask(e);
  for (tool::TagId i = 0; i < sc.tags().size(); ++i) if (m.test(i)) tags.push(std::string(sc.tags().name(i)));
  j.set("tags", tags);
  if (auto* t = w.get<Transform>(e)) { Json o = Json::object(); o.set("pos", ser::vec(t->pos, 3)); o.set("rot", ser::vec(t->rot, 3)); o.set("scale", ser::vec(t->scale, 3)); j.set("transform", o); }
  if (auto* mr = w.get<MeshRenderer>(e)) {
    Json o = Json::object(); o.set("shape", mr->shape); o.set("color", ser::vec(mr->color, 3)); o.set("roughness", static_cast<double>(mr->roughness));
    o.set("metallic", static_cast<double>(mr->metallic)); o.set("model", std::string(mr->model)); o.set("texture", std::string(mr->texture));
    o.set("castShadow", mr->castShadow); o.set("receiveShadow", mr->receiveShadow); j.set("mesh", o);
  }
  if (auto* lc = w.get<LightComp>(e)) {
    Json o = Json::object(); const tool::Light& l = lc->light;
    o.set("type", static_cast<int>(l.type)); o.set("color", ser::vec(&l.color.x, 3)); o.set("intensity", static_cast<double>(l.intensity)); o.set("range", static_cast<double>(l.range));
    o.set("direction", ser::vec(&l.direction.x, 3)); j.set("light", o);
  }
  if (auto* g = w.get<GlobalLight>(e)) {
    Json o = Json::object(); o.set("color", ser::vec(g->color, 3)); o.set("intensity", static_cast<double>(g->intensity)); o.set("azimuth", static_cast<double>(g->azimuth));
    o.set("elevation", static_cast<double>(g->elevation)); o.set("ambient", ser::vec(g->ambient, 3)); o.set("dynamic", g->dynamic); o.set("speed", static_cast<double>(g->speed)); o.set("castShadow", g->castShadow); j.set("globalLight", o);
  }
  if (auto* c = w.get<CameraComp>(e)) {
    Json o = Json::object(); o.set("fov", static_cast<double>(c->fovDeg)); o.set("near", static_cast<double>(c->nearZ)); o.set("far", static_cast<double>(c->farZ));
    o.set("ortho2D", c->ortho2D); o.set("orthoSize", static_cast<double>(c->orthoSize));
    o.set("soundEar", c->soundEar); o.set("earVolume", static_cast<double>(c->earVolume)); o.set("post", ser::postFx(c->post)); j.set("camera", o);
  }
  if (auto* p = w.get<PhysicsComp>(e)) {
    Json o = Json::object(); o.set("body", p->body); o.set("collider", p->collider); o.set("mass", static_cast<double>(p->mass)); o.set("friction", static_cast<double>(p->friction));
    o.set("bounciness", static_cast<double>(p->bounciness)); o.set("linearDamping", static_cast<double>(p->linearDamping)); o.set("angularDamping", static_cast<double>(p->angularDamping));
    o.set("gravityScale", static_cast<double>(p->gravityScale)); o.set("useGravity", p->useGravity); o.set("freezeRotation", p->freezeRotation); o.set("ccd", p->ccd); o.set("trigger", p->trigger);
    o.set("overrideGravity", p->overrideGravity); o.set("gravity", ser::vec(p->gravity, 3)); j.set("physics", o);
  }
  if (auto* tl = w.get<ToolsComp>(e)) {
    Json arr = Json::array();
    for (int i = 0; i < tl->count && i < kMaxTools; ++i) {
      const ToolItem& t = tl->items[i]; Json o = Json::object();
      o.set("kind", t.kind); o.set("enabled", t.enabled); o.set("action", std::string(t.action)); o.set("target", std::string(t.target));
      o.set("a", static_cast<double>(t.a)); o.set("b", static_cast<double>(t.b)); o.set("c", static_cast<double>(t.c)); o.set("d", static_cast<double>(t.d)); o.set("player", t.player); o.set("mode", t.mode); arr.push(o);
    }
    j.set("tools", arr);
  }
  if (auto* sc2 = w.get<ScriptComp>(e)) { Json o = Json::object(); o.set("path", std::string(sc2->path)); o.set("enabled", sc2->enabled); j.set("script", o); }
  if (auto* a = w.get<AudioSource>(e)) {
    Json o = Json::object(); o.set("clip", std::string(a->clip)); o.set("volume", static_cast<double>(a->volume)); o.set("loop", a->loop); o.set("playOnStart", a->playOnStart);
    o.set("spatial", a->spatial); o.set("minDistance", static_cast<double>(a->minDistance)); o.set("maxDistance", static_cast<double>(a->maxDistance)); j.set("audio", o);
  }
  if (w.has<Inactive>(e)) j.set("inactive", true);
  if (auto* u = w.get<UiCanvas>(e)) {
    Json o = Json::object(); o.set("camera", std::string(u->camera)); o.set("visible", u->visible); Json arr = Json::array();
    for (int i = 0; i < u->count && i < kMaxUi; ++i) { const UiElem& it = u->items[i]; Json q = Json::object();
      q.set("kind", it.kind); q.set("text", std::string(it.text)); q.set("action", std::string(it.action)); q.set("rect", ser::vec(&it.x, 4)); q.set("color", ser::vec(it.color, 4));
      q.set("max", static_cast<double>(it.maxValue)); q.set("font", static_cast<double>(it.fontScale)); arr.push(q); }
    o.set("items", arr); j.set("ui", o);
  }
  if (auto* p = w.get<ParticleComp>(e)) {
    Json o = Json::object(); o.set("emitting", p->emitting); o.set("loop", p->loop); o.set("max", p->maxParticles); o.set("rate", static_cast<double>(p->rate)); o.set("lifetime", static_cast<double>(p->lifetime));
    o.set("speed", static_cast<double>(p->speed)); o.set("spread", static_cast<double>(p->spread)); o.set("size", static_cast<double>(p->size)); o.set("sizeEnd", static_cast<double>(p->sizeEnd));
    o.set("gravityScale", static_cast<double>(p->gravityScale)); o.set("bounce", static_cast<double>(p->bounce)); o.set("floorY", static_cast<double>(p->floorY));
    o.set("color", ser::vec(p->color, 3)); o.set("colorEnd", ser::vec(p->colorEnd, 3)); o.set("alpha", static_cast<double>(p->alpha)); o.set("worldSpace", p->worldSpace); j.set("particles", o);
  }
  return j;
}

inline tool::Entity loadEntity(tool::Scene& sc, const tool::Json& j) {
  tool::World& w = sc.world();
  tool::Entity e = w.create();
  Name n; ser::copyStr(n.text, sizeof n.text, j["name"].asString().empty() ? "Object" : j["name"].asString()); w.add<Name>(e, n);
  for (auto& t : j["tags"].items()) if (t.isString() && !t.asString().empty()) w.addTag(e, sc.tag(t.asString()));
  if (j.has("transform")) { Transform t; const auto& o = j["transform"]; ser::readVec(o["pos"], t.pos, 3); ser::readVec(o["rot"], t.rot, 3); ser::readVec(o["scale"], t.scale, 3); w.add<Transform>(e, t); }
  if (j.has("mesh")) {
    MeshRenderer m; const auto& o = j["mesh"]; m.shape = static_cast<int>(o["shape"].asNumber(0)); ser::readVec(o["color"], m.color, 3);
    m.roughness = ser::num(o, "roughness", 0.5f); m.metallic = ser::num(o, "metallic", 0.f); ser::copyStr(m.model, sizeof m.model, o["model"].asString()); ser::copyStr(m.texture, sizeof m.texture, o["texture"].asString());
    m.castShadow = ser::flag(o, "castShadow", true); m.receiveShadow = ser::flag(o, "receiveShadow", true);
    if (m.shape < 0 || m.shape > 3) m.shape = 0;
    w.add<MeshRenderer>(e, m);
  }
  if (j.has("light")) {
    tool::Light l; const auto& o = j["light"]; const int ty = static_cast<int>(o["type"].asNumber(1)); l.type = static_cast<tool::LightType>(ty < 0 || ty > 2 ? 1 : ty);
    ser::readVec(o["color"], &l.color.x, 3); l.intensity = ser::num(o, "intensity", 1.f); l.range = ser::num(o, "range", 10.f); ser::readVec(o["direction"], &l.direction.x, 3);
    w.add<LightComp>(e, LightComp{l});
  }
  if (j.has("globalLight")) {
    GlobalLight g; const auto& o = j["globalLight"]; ser::readVec(o["color"], g.color, 3); g.intensity = ser::num(o, "intensity", g.intensity);
    g.azimuth = ser::num(o, "azimuth", g.azimuth); g.elevation = ser::num(o, "elevation", g.elevation); ser::readVec(o["ambient"], g.ambient, 3);
    g.dynamic = ser::flag(o, "dynamic", false); g.speed = ser::num(o, "speed", g.speed); g.castShadow = ser::flag(o, "castShadow", true); w.add<GlobalLight>(e, g);
  }
  if (j.has("camera")) {
    CameraComp c; const auto& o = j["camera"]; c.fovDeg = ser::num(o, "fov", c.fovDeg); c.nearZ = ser::num(o, "near", c.nearZ); c.farZ = ser::num(o, "far", c.farZ);
    c.ortho2D = ser::flag(o, "ortho2D", false); c.orthoSize = ser::num(o, "orthoSize", c.orthoSize);
    c.soundEar = ser::flag(o, "soundEar", false); c.earVolume = ser::num(o, "earVolume", 1.f);
    if (o.has("post")) c.post = ser::readPostFx(o["post"]);
    w.add<CameraComp>(e, c);
  }
  if (j.has("physics")) {
    PhysicsComp p; const auto& o = j["physics"];
    p.body = std::clamp(static_cast<int>(o["body"].asNumber(1)), 0, 2); p.collider = std::clamp(static_cast<int>(o["collider"].asNumber(0)), 0, 2);
    p.mass = ser::clampNum(o, "mass", 1.f, 0.001f, 1e6f); p.friction = ser::clampNum(o, "friction", 0.5f, 0.f, 5.f); p.bounciness = ser::clampNum(o, "bounciness", 0.f, 0.f, 1.f);
    p.linearDamping = ser::clampNum(o, "linearDamping", 0.01f, 0.f, 100.f); p.angularDamping = ser::clampNum(o, "angularDamping", 0.05f, 0.f, 100.f);
    p.gravityScale = ser::clampNum(o, "gravityScale", 1.f, -10.f, 10.f); p.useGravity = ser::flag(o, "useGravity", true); p.freezeRotation = ser::flag(o, "freezeRotation", false);
    p.ccd = ser::flag(o, "ccd", false); p.trigger = ser::flag(o, "trigger", false); p.overrideGravity = ser::flag(o, "overrideGravity", false);
    ser::readVec(o["gravity"], p.gravity, 3); for (float& g : p.gravity) g = std::clamp(g, -1000.f, 1000.f);
    w.add<PhysicsComp>(e, p);
  }
  if (j.has("tools")) {
    ToolsComp tl;
    for (auto& o : j["tools"].items()) {
      if (tl.count >= kMaxTools || !o.isObject()) break;
      ToolItem& t = tl.items[tl.count++];
      t.kind = std::clamp(static_cast<int>(o["kind"].asNumber(0)), 0, static_cast<int>(ToolKind::Count) - 1); t.enabled = ser::flag(o, "enabled", true);
      ser::copyStr(t.action, sizeof t.action, o["action"].asString()); ser::copyStr(t.target, sizeof t.target, o["target"].asString());
      t.a = ser::clampNum(o, "a", 0.f, -1e6f, 1e6f); t.b = ser::clampNum(o, "b", 0.f, -1e6f, 1e6f); t.c = ser::clampNum(o, "c", 0.f, -1e6f, 1e6f); t.d = ser::clampNum(o, "d", 0.f, -1e6f, 1e6f);
      t.player = std::clamp(static_cast<int>(o["player"].asNumber(1)), 1, 8); t.mode = std::clamp(static_cast<int>(o["mode"].asNumber(0)), 0, 3);
    }
    w.add<ToolsComp>(e, tl);
  }
  if (j.has("script")) { ScriptComp sc2; const auto& o = j["script"]; ser::copyStr(sc2.path, sizeof sc2.path, o["path"].asString()); sc2.enabled = ser::flag(o, "enabled", true); w.add<ScriptComp>(e, sc2); }
  if (j.has("audio")) {
    AudioSource a; const auto& o = j["audio"]; ser::copyStr(a.clip, sizeof a.clip, o["clip"].asString()); a.volume = ser::num(o, "volume", 1.f);
    a.loop = ser::flag(o, "loop", true); a.playOnStart = ser::flag(o, "playOnStart", true); a.spatial = ser::flag(o, "spatial", true);
    a.minDistance = ser::num(o, "minDistance", 1.f); a.maxDistance = ser::num(o, "maxDistance", 30.f); w.add<AudioSource>(e, a);
  }
  if (j["inactive"].isBool() && j["inactive"].asBool()) w.add<Inactive>(e, Inactive{});
  if (j.has("ui")) {
    UiCanvas u; const auto& o = j["ui"]; ser::copyStr(u.camera, sizeof u.camera, o["camera"].asString()); u.visible = ser::flag(o, "visible", true);
    for (auto& q : o["items"].items()) {
      if (u.count >= kMaxUi || !q.isObject()) break;
      UiElem& it = u.items[u.count++]; it.kind = std::clamp(static_cast<int>(q["kind"].asNumber(0)), 0, static_cast<int>(UiKind::Count) - 1);
      ser::copyStr(it.text, sizeof it.text, q["text"].asString()); ser::copyStr(it.action, sizeof it.action, q["action"].asString());
      float r[4] = {it.x, it.y, it.w, it.h}; ser::readVec(q["rect"], r, 4); it.x = std::clamp(r[0], -2.f, 3.f); it.y = std::clamp(r[1], -2.f, 3.f); it.w = std::clamp(r[2], 0.f, 4.f); it.h = std::clamp(r[3], 0.f, 4.f);
      ser::readVec(q["color"], it.color, 4); it.maxValue = ser::clampNum(q, "max", 100.f, 0.0001f, 1e9f); it.fontScale = ser::clampNum(q, "font", 1.f, 0.3f, 8.f);
    }
    w.add<UiCanvas>(e, u);
  }
  if (j.has("particles")) {
    ParticleComp p; const auto& o = j["particles"]; p.emitting = ser::flag(o, "emitting", true); p.loop = ser::flag(o, "loop", true); p.maxParticles = std::clamp(static_cast<int>(o["max"].asNumber(200)), 1, 5000);
    p.rate = ser::clampNum(o, "rate", 40.f, 0.f, 5000.f); p.lifetime = ser::clampNum(o, "lifetime", 1.5f, 0.05f, 60.f); p.speed = ser::clampNum(o, "speed", 3.f, 0.f, 500.f); p.spread = ser::clampNum(o, "spread", 25.f, 0.f, 180.f);
    p.size = ser::clampNum(o, "size", 0.12f, 0.001f, 50.f); p.sizeEnd = ser::clampNum(o, "sizeEnd", 0.02f, 0.f, 50.f); p.gravityScale = ser::clampNum(o, "gravityScale", 1.f, -10.f, 10.f);
    p.bounce = ser::clampNum(o, "bounce", 0.f, 0.f, 1.f); p.floorY = ser::clampNum(o, "floorY", -1000.f, -1e5f, 1e5f);
    ser::readVec(o["color"], p.color, 3); ser::readVec(o["colorEnd"], p.colorEnd, 3); p.alpha = ser::clampNum(o, "alpha", 0.9f, 0.f, 1.f); p.worldSpace = ser::flag(o, "worldSpace", true);
    w.add<ParticleComp>(e, p);
  }
  return e;
}

// Salva/le a cena inteira. O pai e gravado como o INDICE do pai na lista (ordem de eachEntity), pois handles nao sobrevivem ao arquivo.
inline tool::Json saveAllEntities(tool::Scene& sc) {
  tool::World& w = sc.world(); std::unordered_map<uint32_t, int> idx; int i = 0;
  w.eachEntity([&](tool::Entity e) { idx[e.index] = i++; });
  tool::Json arr = tool::Json::array();
  w.eachEntity([&](tool::Entity e) {
    tool::Json j = saveEntity(sc, e);
    if (auto* p = w.get<Parent>(e)) { auto it = idx.find(p->e.index); if (w.alive(p->e) && it != idx.end()) j.set("parent", it->second); }
    arr.push(j);
  });
  return arr;
}
// Cria as entidades de `arr` (no maximo 100000) e resolve os pais. Retorna as entidades na ordem do arquivo (invalidas = entrada ruim).
inline std::vector<tool::Entity> loadAllEntities(tool::Scene& sc, const tool::Json& arr) {
  std::vector<tool::Entity> out; size_t n = 0;
  for (auto& e : arr.items()) { if (++n > 100000) break; out.push_back(e.isObject() ? loadEntity(sc, e) : tool::Entity{}); }
  size_t k = 0;
  for (auto& e : arr.items()) {
    if (k >= out.size()) break;
    if (e.isObject() && e.has("parent") && sc.world().alive(out[k])) {
      const int pi = static_cast<int>(e["parent"].asNumber(-1));
      if (pi >= 0 && static_cast<size_t>(pi) < out.size() && static_cast<size_t>(pi) != k && sc.world().alive(out[static_cast<size_t>(pi)])) sc.world().add<Parent>(out[k], Parent{out[static_cast<size_t>(pi)]});
    }
    ++k;
  }
  return out;
}

} // namespace editor
