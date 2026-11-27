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
// ToolEngine Editor - componentes dos objetos da cena e sua (de)serializacao em JSON.
#pragma once
#include "canvas_division.hpp"
#include "canvas_regions.hpp"
#include "free_values.hpp"
#include "ui_ids.hpp"
#include "tool/audio/audio.hpp"
#include "tool/canvas/canvas_layout.hpp"
#include "tool/core/json.hpp"
#include "tool/core/parallel.hpp"
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

// Graus -> radianos.
constexpr float kDeg2Rad = 3.14159265f / 180.f;

// Nome do objeto na hierarquia (buffer fixo de 40 bytes).
struct Name { char text[40] = "Object"; };
// Posicao, rotacao (graus, X/Y/Z) e escala locais do objeto.
struct Transform { float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1}; };

// Aparencia 3D do objeto: forma ou modelo, cor/PBR, textura, emissao, sombras e neblina.
struct MeshRenderer {
  int shape = 0;
  float color[3] = {0.8f, 0.3f, 0.2f};
  float roughness = 0.5f, metallic = 0.f;
  char model[160] = "";
  char texture[160] = "";
  bool castShadow = true, receiveShadow = true;
  int node = -1;
  float emissive[3] = {1.f, 1.f, 1.f};
  float emissiveIntensity = 0.f;
  float opacity = 1.f;
  bool emissionOn = false;
  bool fogOn = false;
  float fogColor[3] = {0.6f, 0.65f, 0.7f};
  float fogStart = 5.f, fogEnd = 50.f;
  float fogAmount = 1.f;
};

// Material trocado so em uma parte (indice) de um modelo importado.
struct PartOverride {
  int part = 0;
  float color[3] = {0.8f, 0.8f, 0.8f};
  float roughness = 0.5f, metallic = 0.f;
  char texture[160] = "";
};
// Lista de PartOverride de um objeto.
struct PartOverrides { std::vector<PartOverride> items; };
// Pai na hierarquia (so existe se o objeto tem pai).
struct Parent { tool::Entity e; };
// Luz da cena; syncRot acompanha a rotacao do objeto e target e o nome do objeto mirado.
struct LightComp { tool::Light light; bool syncRot = false; char target[40] = ""; };

// Sol da cena (luz direcional + ambiente); com dynamic o sol gira com o tempo de jogo.
struct GlobalLight {
  float color[4] = {1.f, 0.95f, 0.85f, 1.f};   // RGBA: o quarto numero e o alfa (1 = opaco; dados antigos sem alfa carregam com 1)
  float intensity = 2.2f;
  float azimuth = 35.f, elevation = 55.f;
  float ambient[3] = {0.03f, 0.03f, 0.04f};
  bool dynamic = false;
  float speed = 10.f;
  bool castShadow = true;
};

// Camera do jogo: campo de visao, planos, modo 2D ortografico, audio (soundEar) e pos-processamento proprio.
struct CameraComp {
  float fovDeg = 55.f, nearZ = 0.1f, farZ = 200.f;
  bool ortho2D = false;
  float orthoSize = 10.f;
  bool soundEar = false;
  float earVolume = 1.f;
  tool::PostFx post;
};

// Script (.js) anexado ao objeto.
struct ScriptComp {
  char path[160] = "";
  bool enabled = true;
};

// Corpo fisico: tipo de corpo e colisor, massa, atrito, amortecimento e gravidade propria opcional.
struct PhysicsComp {
  int body = 1, collider = 0;
  float mass = 1.f, friction = 0.5f, bounciness = 0.f;
  float linearDamping = 0.01f, angularDamping = 0.05f, gravityScale = 1.f;
  bool useGravity = true, freezeRotation = false, ccd = false, trigger = false;
  bool overrideGravity = false;
  float gravity[3] = {0.f, -9.81f, 0.f};
};

// Tipos de "tool" (comportamento pronto) que um objeto pode ter.
enum class ToolKind : int { Jump = 0, Move = 1, FollowTarget = 2, Spin = 3, OnCollision = 4, ScriptLib = 5, Count };
// Uma tool configurada; a/b/c/d dependem do kind (valores numericos da tool).
struct ToolItem {
  int kind = 0; bool enabled = true;
  char action[40] = "";
  char target[40] = "";
  float a = 0.f, b = 0.f, c = 0.f, d = 0.f;
  int player = 1;
  int mode = 0;
};
// Maximo de tools por objeto.
constexpr int kMaxTools = 8;
// Conjunto de ate kMaxTools tools do objeto.
struct ToolsComp { int count = 0; ToolItem items[kMaxTools]; };

// Tipos de elemento de UI do jogo.
// Os quatro primeiros valores sao os antigos (arquivos salvos dependem deles): nao reordenar.
// Image = imagem do projeto; Window = painel com faixa de titulo; Menu = botoes em coluna; List = linhas de texto;
// Inventory = grade de slots (rows x cols); ListContainer = regiao com rolagem (linhas cortadas pela caixa).
enum class UiKind : int { Text = 0, Panel, Button, Bar, Image, Window, Menu, List, Inventory, ListContainer, Count };
// Maximo de textos (itens de Menu/List/ListContainer, rotulos de slots do Inventory) por elemento.
constexpr int kMaxUiEntries = 12;
// Tamanho de cada texto de entrada, com o zero final.
constexpr int kUiEntryLen = 32;
// Elemento de UI dentro de um canvas: posicao e tamanho em fracao do canvas, cor, texto e acao do botao.
struct UiElem {
  char id[32] = "";   // id unico do elemento no jogo inteiro, gerado na criacao (ver ui_ids.hpp): "e" + base36
  int region = 0;     // regiao do canvas onde o elemento esta (calculada pelo editor, ver canvas_regions.hpp)
  int z = 0;          // ordem Z: o de valor maior fica por cima dos outros
  int kind = 0; char text[64] = "Text"; float x = 0.05f, y = 0.05f, w = 0.2f, h = 0.08f; float color[4] = {1.f, 1.f, 1.f, 1.f};
  char action[40] = "";
  float maxValue = 100.f;
  float fontScale = 1.f;
  float radius = 0.f; // raio do canto em pixels de projeto (0 = quadrado); limitado a metade do menor lado ao desenhar
  char name[64] = "";         // nome do elemento (so para a Hierarchy e o Inspector)
  char image[96] = "";        // imagem PNG/JPEG do projeto (caminho relativo); vazio = sem imagem. Image e Button usam
  bool shadow = false;        // sombra projetada (texto e elementos); desligada por padrao
  float shadowX = 2.f, shadowY = 2.f;   // deslocamento da sombra, em pixels de projeto
  float shadowBlur = 0.f;     // suavizacao da sombra em pixels de projeto (0 = borda dura); vira camadas com alfa decrescente
  float shadowColor[4] = {0.f, 0.f, 0.f, 0.6f};   // cor RGBA 0..1 da sombra
  float wrapWidth = 0.f;      // Text: largura de quebra de linha em pixels de projeto (0 = sem quebra)
  int rows = 3, cols = 4;     // Inventory: linhas e colunas de slots
  int entryCount = 0;         // quantidade de textos validos em entries (Menu, List, ListContainer, rotulos do Inventory)
  char entries[kMaxUiEntries][kUiEntryLen] = {};   // textos das entradas (um por botao, linha ou slot)
  bool preserveAspect = false;   // Image e Inventory: imagem encaixada mantendo a proporcao (sem esticar); padrao: estica
  char slotBackImage[96] = "";   // Inventory: imagem de fundo de cada slot (caminho do projeto); vazio = quadrado de sempre
  char slotFrontImage[96] = "";  // Inventory: imagem da frente de cada slot, sobre o fundo e abaixo do rotulo
};
// Maximo de elementos de UI por canvas.
constexpr int kMaxUi = 16;
// Poe o tamanho padrao (fracao do canvas), o raio e as entradas de exemplo de um tipo de elemento novo. Nao mexe em
// posicao, cor, id nem z. Os tipos antigos ficam como estao (so o kind muda).
inline void initUiKind(UiElem& e, int kind) {
  e.kind = kind;
  auto entry = [&e](const char* t) {
    if (e.entryCount < kMaxUiEntries) std::snprintf(e.entries[e.entryCount++], kUiEntryLen, "%s", t);
  };
  e.entryCount = 0;
  switch (static_cast<UiKind>(kind)) {
    case UiKind::Image: e.w = 0.12f; e.h = 0.2f; e.text[0] = 0; break;
    case UiKind::Window: e.w = 0.3f; e.h = 0.4f; e.radius = 4.f; std::snprintf(e.text, sizeof e.text, "%s", "Window"); break;
    case UiKind::Menu: e.w = 0.18f; e.h = 0.3f; e.radius = 6.f; e.text[0] = 0; entry("Start"); entry("Options"); entry("Quit"); break;
    case UiKind::List: e.w = 0.2f; e.h = 0.3f; e.text[0] = 0; entry("Item 1"); entry("Item 2"); entry("Item 3"); break;
    case UiKind::Inventory: e.w = 0.25f; e.h = 0.25f; e.radius = 3.f; e.rows = 3; e.cols = 4; e.text[0] = 0; break;
    case UiKind::ListContainer: e.w = 0.25f; e.h = 0.35f; e.radius = 4.f; e.text[0] = 0;
      for (int i = 1; i <= 8; ++i) { char b[kUiEntryLen]; std::snprintf(b, sizeof b, "Row %d", i); entry(b); } break;
    default: break;
  }
}
// Marca: objeto desativado (nao simula nem desenha).
struct Inactive {};
// Marca: objeto travado (nao pode ser selecionado/movido no editor).
struct Locked {};

constexpr int kChunkSize = 16;
constexpr int kChunkBlocks = kChunkSize * kChunkSize * kChunkSize;
constexpr int kBlockTypes = 8;
// Pedaco 16x16x16 de blocos (voxels) com paleta de cores; revision sobe a cada edicao para refazer a malha.
struct VoxelChunk {
  unsigned char blocks[kChunkBlocks] = {};
  float cell = 1.f;
  float palette[kBlockTypes][3] = {{0, 0, 0}, {0.45f, 0.72f, 0.30f}, {0.52f, 0.37f, 0.22f}, {0.52f, 0.53f, 0.55f},
                                   {0.86f, 0.80f, 0.55f}, {0.30f, 0.55f, 0.85f}, {0.80f, 0.25f, 0.22f}, {0.95f, 0.95f, 0.95f}};
  unsigned revision = 1;  // Contador de edicao: o renderizador compara para saber se refaz a malha.
  // Indice linear de (x, y, z) em blocks[].
  static int index(int x, int y, int z) { return (y * kChunkSize + z) * kChunkSize + x; }
  // Tipo do bloco em (x, y, z); fora do pedaco devolve 0 (ar).
  unsigned char at(int x, int y, int z) const { return (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) ? 0 : blocks[index(x, y, z)]; }
};
// Canvas: a UI de jogo. Fica sobre a camera escolhida (camera vazia = todas as Principais), com ancora e tamanho
// (engine/canvas), e os elementos (texto, painel, botao, barra) em fracoes do proprio canvas. O cache guarda o
// retangulo do quadro atual e so e refeito quando o layout ou a area da camera mudam.
inline constexpr tool::canvas::Screen kDesignRef{1920.f, 1080.f};   // referencia dos layouts (pixels de projeto)
// Canvas de UI do jogo: layout/estilo/cache, camera alvo, cores, pontos e linhas de divisao, regioes e ate kMaxUi elementos.
struct CanvasComp {
  tool::canvas::Layout layout;
  tool::canvas::Style style;
  tool::canvas::Cache cache;
  char camera[40] = "";
  bool visible = true;        // "Visível no jogo": desenha na Game, para a camera escolhida
  bool showInScene = true;    // "Mostrar na Scene": previa (caixa e elementos) sobre a cena do editor
  float bg[4] = {0.f, 0.f, 0.f, 0.f};       // fundo RGBA 0..1 (alfa 0 = transparente)
  float edge[4] = {1.f, 1.f, 1.f, 1.f};    // cor da borda RGBA 0..1
  std::vector<canvas_division::Point> divisions;   // pontos de divisao (cruzes), em fracao do canvas
  std::vector<canvas_division::Line> divisionLines;   // linhas entre pontos de divisao
  canvas_regions::Regions regions;   // regioes cortadas pelas linhas, com numero (ver canvas_regions.hpp)
  int count = 0;
  UiElem items[kMaxUi];
};

// Canvas que cobre a camera inteira e acompanha a tela: o padrao de um canvas novo e de uma UI migrada.
inline CanvasComp FullCanvas() {
  CanvasComp c;
  c.layout.anchorX = 0.f;
  c.layout.anchorY = 0.f;
  c.layout.width = kDesignRef.w;
  c.layout.height = kDesignRef.h;
  c.layout.resize = tool::canvas::Resize::Scale;
  return c;
}

// Gerador de objetos em massa (teste de carga): total, quantos por quadro, altura/area e semente do sorteio.
struct SpawnerComp {
  long long total = 0, spawned = 0; int perFrame = 1;
  float height = 15.f, area = 20.f;
  bool armed = false, logged = false;
  bool physics = true;   // true: "test ecsf" (esferas com fisica, caindo); false: "test ecs" (sem fisica, movendo)
  double startedAt = -1.0;
  uint32_t seed = 2463534242u;
};

// Movimento de uma instancia do teste "test ecs" (sem fisica): a posicao local oscila em torno de `base`, sobre
// um caminho de Lissajous. Atualizado por Editor::tickMovers a cada quadro de Play; so mexe no Transform.
struct MoverComp {
  float base[3] = {0.f, 0.f, 0.f};   // centro do movimento (local ao ECScall)
  float amp = 4.f;                   // amplitude em metros
  float freq = 0.5f;                 // ciclos por segundo (aproximado)
  float phase = 0.f;                 // fase inicial (rad)
  float t = 0.f;                     // tempo acumulado em segundos
};

// Emissor de particulas: taxa, vida, velocidade, cores ao longo da vida, area de emissao, rota e redemoinho.
struct ParticleComp {
  bool emitting = true, loop = true; int maxParticles = 200; float rate = 40.f, lifetime = 1.5f, speed = 3.f, spread = 25.f;
  float size = 0.12f, sizeEnd = 0.02f, gravityScale = 1.f, bounce = 0.f, floorY = -1000.f;
  float color[3] = {1.f, 0.6f, 0.2f}, color2[3] = {1.f, 0.42f, 0.12f}, color3[3] = {0.9f, 0.22f, 0.07f}, colorEnd[3] = {0.8f, 0.1f, 0.05f};
  float alpha = 0.9f; bool worldSpace = true;
  int form = 0;
  int area = 0;
  float areaSize[3] = {1.f, 0.2f, 1.f};
  bool fadeOpacity = true;
  float fadeDelay = 0.f;
  bool route = false; int routeCount = 0;
  float routePts[8][3] = {{0, 0, 0}, {0, 1, 0}, {1, 2, 0}, {1, 3, 1}, {0, 4, 1}, {0, 5, 0}, {0, 0, 0}, {0, 0, 0}};
  bool tornado = false;
  float tornadoSpeed = 1.5f, tornadoRadius = 0.6f, tornadoHeight = 2.f;
};

// Fonte de som: arquivo, volume, loop, 3D (spatial) com distancias minima/maxima; voice e o som tocando agora.
struct AudioSource {
  char clip[160] = "";
  float volume = 1.f;
  bool loop = true, playOnStart = true, spatial = true;
  float minDistance = 1.f, maxDistance = 30.f;
  tool::VoiceId voice{};
};

// Matriz local->mundo: T * Ry * Rx * Rz * S (rotacao em graus, ordem Y, X, Z).
inline tool::Mat4 worldMatrix(const Transform& t) {
  return tool::Mat4::translation({t.pos[0], t.pos[1], t.pos[2]}) * tool::Mat4::rotationY(t.rot[1] * kDeg2Rad) * tool::Mat4::rotationX(t.rot[0] * kDeg2Rad) *
         tool::Mat4::rotationZ(t.rot[2] * kDeg2Rad) * tool::Mat4::scale({t.scale[0], t.scale[1], t.scale[2]});
}

// Direcao "para frente" (-Z) do objeto, sem escala nem posicao.
inline tool::Vec3 forwardOf(const Transform& t) {
  Transform r = t; r.scale[0] = r.scale[1] = r.scale[2] = 1.f; r.pos[0] = r.pos[1] = r.pos[2] = 0.f;
  const tool::Vec4 f = worldMatrix(r).mul({0, 0, -1, 0});
  return tool::normalize({f.x, f.y, f.z});
}
// Direcao "para cima" (+Y) do objeto, sem escala nem posicao.
inline tool::Vec3 upOf(const Transform& t) {
  Transform r = t; r.scale[0] = r.scale[1] = r.scale[2] = 1.f; r.pos[0] = r.pos[1] = r.pos[2] = 0.f;
  const tool::Vec4 f = worldMatrix(r).mul({0, 1, 0, 0});
  return tool::normalize({f.x, f.y, f.z});
}

// Monta a camera do motor a partir do Transform e do CameraComp.
inline tool::Camera cameraOf(const Transform& t, const CameraComp& c) {
  tool::Camera cam;
  cam.eye = {t.pos[0], t.pos[1], t.pos[2]};
  cam.target = cam.eye + forwardOf(t);
  cam.up = upOf(t);
  cam.fovY = c.fovDeg * kDeg2Rad; cam.nearZ = c.nearZ; cam.farZ = c.farZ;
  cam.ortho = c.ortho2D; cam.orthoHeight = c.orthoSize;
  return cam;
}

// Direcao da luz do sol (apontando para o chao) a partir de azimute e elevacao em graus.
inline tool::Vec3 sunDirection(float azimuthDeg, float elevationDeg) {
  const float az = azimuthDeg * kDeg2Rad, el = elevationDeg * kDeg2Rad;
  return tool::normalize({-std::cos(el) * std::sin(az), -std::sin(el), -std::cos(el) * std::cos(az)});
}

// Luz direcional do sol; no modo dinamico o azimute gira e a elevacao oscila com runtimeDeg, e a intensidade cai perto do horizonte.
inline tool::Light sunLight(const GlobalLight& g, float runtimeDeg) {
  tool::Light l; l.type = tool::LightType::Directional;
  const float az = g.azimuth + (g.dynamic ? runtimeDeg : 0.f);
  float el = g.elevation, k = 1.f;
  if (g.dynamic) {
    el = 15.f + 60.f * std::sin(runtimeDeg * kDeg2Rad * 0.5f + g.elevation * kDeg2Rad);
    k = std::clamp((el + 5.f) / 20.f, 0.f, 1.f);
  }
  l.direction = sunDirection(az, el);
  l.color = {g.color[0], g.color[1], g.color[2]};
  l.intensity = g.intensity * k;
  l.castShadow = g.castShadow;
  return l;
}

// Ajudantes de serializacao JSON (ler com padrao/limite, gravar vetores).
namespace ser {

// float[n] -> array JSON.
inline tool::Json vec(const float* v, int n) { tool::Json a = tool::Json::array(); for (int i = 0; i < n; ++i) a.push(static_cast<double>(v[i])); return a; }
// Array JSON -> float[n]; so preenche os numeros que existem (o resto fica como estava).
inline void readVec(const tool::Json& j, float* v, int n) { const auto& it = j.items(); for (int i = 0; i < n && static_cast<size_t>(i) < it.size(); ++i) v[i] = static_cast<float>(it[static_cast<size_t>(i)].asNumber(v[i])); }
// Le uma cor RGBA (4 numeros): dados antigos so com RGB carregam com alfa 1.
inline void readRgba(const tool::Json& j, float* rgba) { std::vector<double> d; for (const auto& it : j.items()) d.push_back(it.asNumber(0.0)); editor::free_values::ReadRgba(d.data(), d.size(), rgba); }
// Numero da chave k, ou o padrao d se a chave nao existir.
inline float num(const tool::Json& j, const char* k, float d) { return j.has(k) ? static_cast<float>(j[k].asNumber(d)) : d; }
inline bool flag(const tool::Json& j, const char* k, bool d) { return j.has(k) ? j[k].asBool(d) : d; }
inline void copyStr(char* dst, size_t cap, const std::string& s) { std::snprintf(dst, cap, "%s", s.c_str()); }
// Numero da chave k preso em [lo, hi] (arquivos editados a mao).
inline float clampNum(const tool::Json& j, const char* k, float d, float lo, float hi) { return std::clamp(num(j, k, d), lo, hi); }

// Pos-processamento -> JSON.
inline tool::Json postFx(const tool::PostFx& p) {
  tool::Json o = tool::Json::object();
  o.set("enabled", p.enabled); o.set("exposure", static_cast<double>(p.exposure)); o.set("contrast", static_cast<double>(p.contrast)); o.set("saturation", static_cast<double>(p.saturation));
  o.set("tint", vec(p.tint, 3)); o.set("tintStrength", static_cast<double>(p.tintStrength)); o.set("vignette", static_cast<double>(p.vignette));
  o.set("vignetteSize", static_cast<double>(p.vignetteSize)); o.set("sharpen", static_cast<double>(p.sharpen)); o.set("additive", p.additive);
  return o;
}
// JSON -> pos-processamento, com cada valor preso numa faixa segura.
inline tool::PostFx readPostFx(const tool::Json& o) {
  tool::PostFx p;
  p.enabled = flag(o, "enabled", false); p.exposure = clampNum(o, "exposure", 0.f, -5.f, 5.f); p.contrast = clampNum(o, "contrast", 1.f, 0.2f, 3.f);
  p.saturation = clampNum(o, "saturation", 1.f, 0.f, 3.f); readVec(o["tint"], p.tint, 3); for (float& t : p.tint) t = std::clamp(t, 0.f, 1.f);
  p.tintStrength = clampNum(o, "tintStrength", 0.f, 0.f, 1.f); p.vignette = clampNum(o, "vignette", 0.f, 0.f, 1.f);
  p.vignetteSize = clampNum(o, "vignetteSize", 0.75f, 0.1f, 1.5f); p.sharpen = clampNum(o, "sharpen", 0.f, 0.f, 1.f); p.additive = flag(o, "additive", true);
  return p;
}

} // namespace ser

// Elementos da UI <-> JSON. Um so formato para o Canvas e para a migracao de cenas antigas (bloco "ui").
inline tool::Json itemsJson(const CanvasComp& c) {
  tool::Json arr = tool::Json::array();
  for (int i = 0; i < c.count && i < kMaxUi; ++i) {
    const UiElem& it = c.items[i];
    tool::Json q = tool::Json::object();
    q.set("kind", it.kind); q.set("text", std::string(it.text)); q.set("action", std::string(it.action));
    q.set("id", std::string(it.id)); q.set("region", static_cast<double>(it.region)); q.set("z", static_cast<double>(it.z));
    q.set("rect", ser::vec(&it.x, 4)); q.set("color", ser::vec(it.color, 4));
    q.set("max", static_cast<double>(it.maxValue)); q.set("font", static_cast<double>(it.fontScale));
    q.set("radius", static_cast<double>(it.radius));
    q.set("name", std::string(it.name)); q.set("image", std::string(it.image));
    q.set("preserveAspect", it.preserveAspect);
    q.set("slotBackImage", std::string(it.slotBackImage)); q.set("slotFrontImage", std::string(it.slotFrontImage));
    tool::Json sh = tool::Json::object();
    sh.set("on", it.shadow); sh.set("x", static_cast<double>(it.shadowX)); sh.set("y", static_cast<double>(it.shadowY));
    sh.set("blur", static_cast<double>(it.shadowBlur)); sh.set("color", ser::vec(it.shadowColor, 4));
    q.set("shadow", sh);
    q.set("wrap", static_cast<double>(it.wrapWidth));
    q.set("rows", static_cast<double>(it.rows)); q.set("cols", static_cast<double>(it.cols));
    tool::Json ents = tool::Json::array();
    for (int e = 0; e < it.entryCount && e < kMaxUiEntries; ++e) ents.push(std::string(it.entries[e]));
    q.set("entries", ents);
    arr.push(q);
  }
  return arr;
}
// Pontos de divisao do canvas -> JSON.
inline tool::Json divisionsJson(const CanvasComp& c) {
  tool::Json arr = tool::Json::array();
  for (const canvas_division::Point& p : c.divisions) {
    tool::Json q = tool::Json::object();
    q.set("x", static_cast<double>(p.x)); q.set("y", static_cast<double>(p.y)); q.set("name", p.name);
    arr.push(q);
  }
  return arr;
}
// Linhas entre pontos de divisao -> JSON.
inline tool::Json divisionLinesJson(const CanvasComp& c) {
  tool::Json arr = tool::Json::array();
  for (const canvas_division::Line& l : c.divisionLines) {
    tool::Json q = tool::Json::object();
    q.set("a", static_cast<double>(l.a)); q.set("b", static_cast<double>(l.b)); q.set("dotted", l.dotted);
    arr.push(q);
  }
  return arr;
}
// JSON -> linhas (maximo 128); ignora indices invalidos e linhas de um ponto nele mesmo.
inline void readDivisionLines(const tool::Json& arr, CanvasComp& c) {
  const auto& it = arr.items();
  for (size_t i = 0; i < it.size() && c.divisionLines.size() < 128; ++i) {
    canvas_division::Line l;
    l.a = static_cast<size_t>(std::max(0.0, it[i]["a"].asNumber(0.0)));
    l.b = static_cast<size_t>(std::max(0.0, it[i]["b"].asNumber(0.0)));
    l.dotted = it[i]["dotted"].asBool(false);
    if (l.a < c.divisions.size() && l.b < c.divisions.size() && l.a != l.b) c.divisionLines.push_back(l);
  }
}
// JSON -> pontos de divisao (maximo 64), posicoes presas em [0, 1].
inline void readDivisions(const tool::Json& arr, CanvasComp& c) {
  const auto& it = arr.items();
  for (size_t i = 0; i < it.size() && c.divisions.size() < 64; ++i) {
    canvas_division::Point p;
    p.x = canvas_division::clamp01(static_cast<float>(it[i]["x"].asNumber(0.5)));
    p.y = canvas_division::clamp01(static_cast<float>(it[i]["y"].asNumber(0.5)));
    p.name = it[i]["name"].asString();
    c.divisions.push_back(p);
  }
}
// JSON -> elementos de UI (ate kMaxUi); gera id novo se faltar e limita posicao, tamanho e fonte.
inline void readItems(const tool::Json& arr, CanvasComp& c) {
  for (const auto& q : arr.items()) {
    if (c.count >= kMaxUi || !q.isObject()) break;
    UiElem& it = c.items[c.count++];
    it.kind = std::clamp(static_cast<int>(q["kind"].asNumber(0)), 0, static_cast<int>(UiKind::Count) - 1);
    ser::copyStr(it.text, sizeof it.text, q["text"].asString()); ser::copyStr(it.action, sizeof it.action, q["action"].asString());
    ser::copyStr(it.id, sizeof it.id, q["id"].asString());
    if (ui_ids::parse(it.id) == 0) ser::copyStr(it.id, sizeof it.id, ui_ids::next());   // sem id valido: gera um novo
    else ui_ids::observe(it.id);                                                       // novos ids nao repetem este
    it.z = static_cast<int>(q["z"].asNumber(static_cast<double>(c.count - 1)));
    float r[4] = {it.x, it.y, it.w, it.h}; ser::readVec(q["rect"], r, 4);
    it.x = std::clamp(r[0], -2.f, 3.f); it.y = std::clamp(r[1], -2.f, 3.f); it.w = std::clamp(r[2], 0.f, 4.f); it.h = std::clamp(r[3], 0.f, 4.f);
    ser::readVec(q["color"], it.color, 4); it.maxValue = ser::clampNum(q, "max", 100.f, 0.0001f, 1e9f); it.fontScale = ser::clampNum(q, "font", 1.f, 0.3f, 8.f);
    // Arquivo antigo sem o campo: mantem o visual de antes (Panel 4 px, Button 6 px, demais 0).
    const float legacy = it.kind == static_cast<int>(UiKind::Panel) ? 4.f : it.kind == static_cast<int>(UiKind::Button) ? 6.f : 0.f;
    it.radius = ser::clampNum(q, "radius", legacy, 0.f, 1e4f);
    // Campos novos: arquivo antigo sem eles fica sem imagem, sem sombra, sem quebra (mesmo visual de antes).
    ser::copyStr(it.image, sizeof it.image, q["image"].asString());
    it.preserveAspect = ser::flag(q, "preserveAspect", false);   // arquivo antigo: estica como antes
    ser::copyStr(it.slotBackImage, sizeof it.slotBackImage, q["slotBackImage"].asString());
    ser::copyStr(it.slotFrontImage, sizeof it.slotFrontImage, q["slotFrontImage"].asString());
    ser::copyStr(it.name, sizeof it.name, q["name"].asString());   // arquivo antigo sem nome: fica ""
    const tool::Json& sh = q["shadow"];
    it.shadow = ser::flag(sh, "on", false);
    it.shadowX = ser::clampNum(sh, "x", 2.f, -200.f, 200.f); it.shadowY = ser::clampNum(sh, "y", 2.f, -200.f, 200.f);
    it.shadowBlur = ser::clampNum(sh, "blur", 0.f, 0.f, 100.f);
    ser::readVec(sh["color"], it.shadowColor, 4); for (float& c : it.shadowColor) c = std::clamp(c, 0.f, 1.f);
    it.wrapWidth = ser::clampNum(q, "wrap", 0.f, 0.f, 1e5f);
    it.rows = std::clamp(static_cast<int>(ser::num(q, "rows", 3.f)), 1, 16); it.cols = std::clamp(static_cast<int>(ser::num(q, "cols", 4.f)), 1, 16);
    it.entryCount = 0;
    for (const auto& en : q["entries"].items()) {
      if (it.entryCount >= kMaxUiEntries) break;
      ser::copyStr(it.entries[it.entryCount++], kUiEntryLen, en.asString());
    }
  }
}

// Elemento novo no canvas: id unico da sessao e ordem Z acima de todos os outros elementos do canvas.
inline void stampNew(UiElem& e, const CanvasComp& c) {
  std::snprintf(e.id, sizeof e.id, "%s", ui_ids::next().c_str());
  int top = -1;
  for (int i = 0; i < c.count && i < kMaxUi; ++i) {
    if (&c.items[i] != &e) top = std::max(top, c.items[i].z);
  }
  e.z = top + 1;
}

// Objeto -> JSON: nome, tags e cada componente que ele tem (um bloco por componente).
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
    o.set("castShadow", mr->castShadow); o.set("receiveShadow", mr->receiveShadow); o.set("node", mr->node);
    o.set("emissive", ser::vec(mr->emissive, 3)); o.set("emissiveIntensity", static_cast<double>(mr->emissiveIntensity)); o.set("emissionOn", mr->emissionOn); o.set("opacity", static_cast<double>(mr->opacity));
    o.set("fogOn", mr->fogOn); o.set("fogColor", ser::vec(mr->fogColor, 3)); o.set("fogStart", static_cast<double>(mr->fogStart)); o.set("fogEnd", static_cast<double>(mr->fogEnd)); o.set("fogAmount", static_cast<double>(mr->fogAmount));
    j.set("mesh", o);
  }
  if (auto* po = w.get<PartOverrides>(e)) {
    Json arr = Json::array();
    for (const PartOverride& it : po->items) {
      Json o = Json::object(); o.set("part", it.part); o.set("color", ser::vec(it.color, 3));
      o.set("roughness", static_cast<double>(it.roughness)); o.set("metallic", static_cast<double>(it.metallic));
      o.set("texture", std::string(it.texture)); arr.push(o);
    }
    j.set("partMaterials", arr);
  }
  if (auto* lc = w.get<LightComp>(e)) {
    Json o = Json::object(); const tool::Light& l = lc->light;
    o.set("type", static_cast<int>(l.type)); o.set("color", ser::vec(&l.color.x, 3)); o.set("intensity", static_cast<double>(l.intensity)); o.set("range", static_cast<double>(l.range));
    o.set("direction", ser::vec(&l.direction.x, 3)); o.set("castShadow", l.castShadow); o.set("syncRot", lc->syncRot); o.set("target", std::string(lc->target)); j.set("light", o);
  }
  if (auto* g = w.get<GlobalLight>(e)) {
    Json o = Json::object(); o.set("color", ser::vec(g->color, 4)); o.set("intensity", static_cast<double>(g->intensity)); o.set("azimuth", static_cast<double>(g->azimuth));
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
  if (w.has<Locked>(e)) j.set("locked", true);
  if (auto* vc = w.get<VoxelChunk>(e)) {
    Json o = Json::object(); o.set("cell", static_cast<double>(vc->cell)); o.set("palette", ser::vec(&vc->palette[0][0], kBlockTypes * 3));
    Json rle = Json::array();
    for (int i = 0; i < kChunkBlocks;) { const unsigned char v = vc->blocks[i]; int n = 1; while (i + n < kChunkBlocks && vc->blocks[i + n] == v) ++n; rle.push(static_cast<int>(v)); rle.push(n); i += n; }
    o.set("rle", rle); j.set("voxels", o);
  }
  if (auto* c = w.get<CanvasComp>(e)) {
    Json o = Json::object(); const auto& L = c->layout; const auto& S = c->style;
    o.set("ax", static_cast<double>(L.anchorX)); o.set("ay", static_cast<double>(L.anchorY));
    o.set("ox", static_cast<double>(L.offsetX)); o.set("oy", static_cast<double>(L.offsetY));
    o.set("w", static_cast<double>(L.width)); o.set("h", static_cast<double>(L.height));
    o.set("resize", std::string(L.resize == tool::canvas::Resize::Scale ? "scale" : "follow"));
    o.set("radius", static_cast<double>(S.radius)); o.set("border_width", static_cast<double>(S.borderWidth));
    o.set("bg", ser::vec(c->bg, 4)); o.set("bc", ser::vec(c->edge, 4));
    o.set("camera", std::string(c->camera)); o.set("visible", c->visible); o.set("scene", c->showInScene);
    o.set("items", itemsJson(*c));
    o.set("divisions", divisionsJson(*c));
    o.set("lines", divisionLinesJson(*c));
    {
      tool::Json rg = tool::Json::object();
      rg.set("next", static_cast<double>(c->regions.next));
      tool::Json cells = tool::Json::array();
      for (std::uint16_t v : c->regions.cell) cells.push(static_cast<double>(v));
      rg.set("cells", cells);
      o.set("regions", rg);
    }
    j.set("canvas", o);
  }
  if (auto* p = w.get<ParticleComp>(e)) {
    Json o = Json::object(); o.set("emitting", p->emitting); o.set("loop", p->loop); o.set("max", p->maxParticles); o.set("rate", static_cast<double>(p->rate)); o.set("lifetime", static_cast<double>(p->lifetime));
    o.set("speed", static_cast<double>(p->speed)); o.set("spread", static_cast<double>(p->spread)); o.set("size", static_cast<double>(p->size)); o.set("sizeEnd", static_cast<double>(p->sizeEnd));
    o.set("gravityScale", static_cast<double>(p->gravityScale)); o.set("bounce", static_cast<double>(p->bounce)); o.set("floorY", static_cast<double>(p->floorY));
    o.set("color", ser::vec(p->color, 3)); o.set("color2", ser::vec(p->color2, 3)); o.set("color3", ser::vec(p->color3, 3)); o.set("colorEnd", ser::vec(p->colorEnd, 3));
    o.set("alpha", static_cast<double>(p->alpha)); o.set("worldSpace", p->worldSpace);
    o.set("form", p->form); o.set("area", p->area); o.set("areaSize", ser::vec(p->areaSize, 3));
    o.set("fadeOpacity", p->fadeOpacity); o.set("fadeDelay", static_cast<double>(p->fadeDelay));
    o.set("route", p->route); { Json pts = Json::array(); for (int i = 0; i < p->routeCount && i < 8; ++i) pts.push(ser::vec(p->routePts[i], 3)); o.set("routePts", pts); }
    o.set("tornado", p->tornado); o.set("tornadoSpeed", static_cast<double>(p->tornadoSpeed)); o.set("tornadoRadius", static_cast<double>(p->tornadoRadius)); o.set("tornadoHeight", static_cast<double>(p->tornadoHeight));
    j.set("particles", o);
  }
  return j;
}

// JSON -> objeto novo com os componentes do arquivo; o que falta usa o padrao. O pai e ligado depois, em loadAllEntities.
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
    m.node = static_cast<int>(o["node"].asNumber(-1));
    ser::readVec(o["emissive"], m.emissive, 3); m.emissiveIntensity = ser::num(o, "emissiveIntensity", 0.f);
    m.opacity = ser::clampNum(o, "opacity", 1.f, 0.f, 1.f);
    m.emissionOn = o.has("emissionOn") ? ser::flag(o, "emissionOn", false) : m.emissiveIntensity > 0.f;
    m.fogOn = ser::flag(o, "fogOn", false); ser::readVec(o["fogColor"], m.fogColor, 3);
    m.fogStart = ser::num(o, "fogStart", 5.f); m.fogEnd = ser::num(o, "fogEnd", 50.f);
    m.fogAmount = ser::clampNum(o, "fogAmount", 1.f, 0.f, 1.f);
    if (m.shape < 0 || m.shape > 9) m.shape = 0;
    w.add<MeshRenderer>(e, m);
  }
  if (j.has("partMaterials")) {
    PartOverrides po;
    for (const auto& o : j["partMaterials"].items()) {
      PartOverride it; it.part = static_cast<int>(o["part"].asNumber(0));
      ser::readVec(o["color"], it.color, 3); it.roughness = ser::num(o, "roughness", 0.5f); it.metallic = ser::num(o, "metallic", 0.f);
      ser::copyStr(it.texture, sizeof it.texture, o["texture"].asString());
      po.items.push_back(it);
    }
    w.add<PartOverrides>(e, po);
  }
  if (j.has("light")) {
    tool::Light l; const auto& o = j["light"]; const int ty = static_cast<int>(o["type"].asNumber(1)); l.type = static_cast<tool::LightType>(ty < 0 || ty > 2 ? 1 : ty);
    ser::readVec(o["color"], &l.color.x, 3); l.intensity = ser::num(o, "intensity", 1.f); l.range = ser::num(o, "range", 10.f); ser::readVec(o["direction"], &l.direction.x, 3); l.castShadow = ser::flag(o, "castShadow", false);
    LightComp lcomp{l}; lcomp.syncRot = ser::flag(o, "syncRot", false); ser::copyStr(lcomp.target, sizeof lcomp.target, o["target"].asString());
    w.add<LightComp>(e, lcomp);
  }
  if (j.has("globalLight")) {
    GlobalLight g; const auto& o = j["globalLight"]; ser::readRgba(o["color"], g.color); g.intensity = ser::num(o, "intensity", g.intensity);
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
  if (j["locked"].isBool() && j["locked"].asBool()) w.add<Locked>(e, Locked{});
  if (j.has("voxels")) {
    VoxelChunk vc; const auto& o = j["voxels"]; vc.cell = ser::clampNum(o, "cell", 1.f, 0.01f, 100.f);
    ser::readVec(o["palette"], &vc.palette[0][0], kBlockTypes * 3);
    int pos = 0; const auto& runs = o["rle"].items();
    for (size_t k = 0; k + 1 < runs.size() && pos < kChunkBlocks; k += 2) {
      const int v = std::clamp(static_cast<int>(runs[k].asNumber(0)), 0, kBlockTypes - 1);
      const int n = std::clamp(static_cast<int>(runs[k + 1].asNumber(0)), 0, kChunkBlocks - pos);
      for (int q = 0; q < n; ++q) vc.blocks[pos++] = static_cast<unsigned char>(v);
    }
    w.add<VoxelChunk>(e, vc);
  }
  if (j.has("ui") && !j.has("canvas")) {   // cena antiga: a UI do jogo vira um canvas que cobre a camera inteira
    CanvasComp c = FullCanvas(); const auto& o = j["ui"];
    ser::copyStr(c.camera, sizeof c.camera, o["camera"].asString()); c.visible = ser::flag(o, "visible", true);
    readItems(o["items"], c);
    w.add<CanvasComp>(e, c);
  }
  if (j.has("canvas")) {
    CanvasComp c; const auto& o = j["canvas"];
    c.layout.anchorX = ser::clampNum(o, "ax", 0.5f, -2.f, 3.f); c.layout.anchorY = ser::clampNum(o, "ay", 0.5f, -2.f, 3.f);
    c.layout.offsetX = ser::clampNum(o, "ox", 0.f, -1e5f, 1e5f); c.layout.offsetY = ser::clampNum(o, "oy", 0.f, -1e5f, 1e5f);
    c.layout.width = ser::clampNum(o, "w", 320.f, 1.f, 1e5f); c.layout.height = ser::clampNum(o, "h", 240.f, 1.f, 1e5f);
    c.layout.resize = o["resize"].asString() == "scale" ? tool::canvas::Resize::Scale : tool::canvas::Resize::Follow;
    c.style.radius = ser::clampNum(o, "radius", 0.f, 0.f, 1e4f); c.style.borderWidth = ser::clampNum(o, "border_width", 0.f, 0.f, 1000.f);
    ser::readVec(o["bg"], c.bg, 4); ser::readVec(o["bc"], c.edge, 4);
    ser::copyStr(c.camera, sizeof c.camera, o["camera"].asString()); c.visible = ser::flag(o, "visible", true);
    c.showInScene = ser::flag(o, "scene", true);
    readItems(o["items"], c);
    readDivisions(o["divisions"], c);
    readDivisionLines(o["lines"], c);
    {
      const auto& cells = o["regions"]["cells"].items();
      if (cells.size() == c.regions.cell.size()) {
        for (std::size_t i = 0; i < cells.size(); ++i) c.regions.cell[i] = static_cast<std::uint16_t>(std::max(0.0, cells[i].asNumber(0.0)));
        c.regions.next = std::max(1, static_cast<int>(o["regions"]["next"].asNumber(1.0)));
      }
    }
    w.add<CanvasComp>(e, c);
  }
  if (j.has("particles")) {
    ParticleComp p; const auto& o = j["particles"]; p.emitting = ser::flag(o, "emitting", true); p.loop = ser::flag(o, "loop", true); p.maxParticles = std::clamp(static_cast<int>(o["max"].asNumber(200)), 1, 5000);
    p.rate = ser::clampNum(o, "rate", 40.f, 0.f, 5000.f); p.lifetime = ser::clampNum(o, "lifetime", 1.5f, 0.05f, 60.f); p.speed = ser::clampNum(o, "speed", 3.f, 0.f, 500.f); p.spread = ser::clampNum(o, "spread", 25.f, 0.f, 180.f);
    p.size = ser::clampNum(o, "size", 0.12f, 0.001f, 50.f); p.sizeEnd = ser::clampNum(o, "sizeEnd", 0.02f, 0.f, 50.f); p.gravityScale = ser::clampNum(o, "gravityScale", 1.f, -10.f, 10.f);
    p.bounce = ser::clampNum(o, "bounce", 0.f, 0.f, 1.f); p.floorY = ser::clampNum(o, "floorY", -1000.f, -1e5f, 1e5f);
    ser::readVec(o["color"], p.color, 3); ser::readVec(o["colorEnd"], p.colorEnd, 3); p.alpha = ser::clampNum(o, "alpha", 0.9f, 0.f, 1.f); p.worldSpace = ser::flag(o, "worldSpace", true);
    for (int c = 0; c < 3; ++c) { p.color2[c] = p.color[c] + (p.colorEnd[c] - p.color[c]) / 3.f; p.color3[c] = p.color[c] + (p.colorEnd[c] - p.color[c]) * 2.f / 3.f; }
    ser::readVec(o["color2"], p.color2, 3); ser::readVec(o["color3"], p.color3, 3);
    p.form = std::clamp(static_cast<int>(o["form"].asNumber(0)), 0, 6); p.area = std::clamp(static_cast<int>(o["area"].asNumber(0)), 0, 2);
    ser::readVec(o["areaSize"], p.areaSize, 3);
    p.fadeOpacity = ser::flag(o, "fadeOpacity", true); p.fadeDelay = ser::clampNum(o, "fadeDelay", 0.f, 0.f, 600.f);
    p.route = ser::flag(o, "route", false); { int k = 0; for (auto& v : o["routePts"].items()) if (k < 8) { ser::readVec(v, p.routePts[k], 3); ++k; } p.routeCount = k; }
    p.tornado = ser::flag(o, "tornado", false); p.tornadoSpeed = ser::clampNum(o, "tornadoSpeed", 1.5f, -20.f, 20.f);
    p.tornadoRadius = ser::clampNum(o, "tornadoRadius", 0.6f, 0.f, 50.f); p.tornadoHeight = ser::clampNum(o, "tornadoHeight", 2.f, -100.f, 100.f);
    w.add<ParticleComp>(e, p);
  }
  return e;
}

// Todos os objetos -> array JSON; "parent" guarda a posicao do pai nesse array.
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

// Array JSON -> objetos (maximo 100000), depois liga os pais pelo indice (ignora indice invalido ou o proprio objeto).
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