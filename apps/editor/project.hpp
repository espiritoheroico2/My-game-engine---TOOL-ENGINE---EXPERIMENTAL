// ToolEngine Editor - pastas do projeto, biblioteca de assets carregados e arquivos de cena.
//
// Estrutura em disco (criada automaticamente ao abrir o editor):
//   Project/Assets/   modelos (.glb .gltf .fbx), sons (.wav .mp3), texturas, textos... (pastas livres)
//   Project/Assets/Scripts/   scripts ToolObject (.tool.js), um por objeto (ver script.hpp)
//   Project/Scenes/   uma cena por arquivo: <nome>.scene.json
//   Project/project.json   configuracoes do projeto (ligacoes de tags, preset de exportacao; ver project_settings.hpp)
// Todo caminho guardado numa cena e RELATIVO a Project/ (ex.: "Assets/nave.glb"), entao o projeto pode ser movido.
// Entrada nao confiavel: nomes vindos do usuario passam por safeName()/resolve() (sem "..", sem caminho absoluto).
#pragma once
#include "components.hpp"
#include "image_io.hpp"
#include "project_paths.hpp"
#include "skybox.hpp"
#include "tool/assets/import.hpp"
#include "tool/audio/decode.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/lighting.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace editor {

namespace fs = std::filesystem;

// Extensao do arquivo em minusculas (".GLB" vira ".glb") para comparar tipos.
inline std::string lowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

// Tipo de um item da aba Assets, decidido so pela extensao/nome.
enum class AssetKind { Folder, Model, Audio, Scene, Image, Text, Other };
// Classifica um caminho em AssetKind; ".scene.json" e testado antes de ".json" para nao virar Text.
inline AssetKind kindOf(const fs::path& p, bool isDir) {
  if (isDir) return AssetKind::Folder;
  const std::string e = lowerExt(p);
  if (p.filename().string().size() > 11 && p.filename().string().rfind(".scene.json") == p.filename().string().size() - 11) return AssetKind::Scene;
  if (e == ".glb" || e == ".gltf" || e == ".fbx") return AssetKind::Model;
  if (e == ".wav" || e == ".mp3") return AssetKind::Audio;
  if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga") return AssetKind::Image;
  if (e == ".txt" || e == ".json" || e == ".md" || e == ".hlsl" || e == ".glsl" || e == ".csv" || e == ".ini" || e == ".lua" || e == ".js") return AssetKind::Text;
  return AssetKind::Other;
}

// Nome de arquivo/pasta valido: sem separadores, sem "..", sem caracteres proibidos no Windows, nao vazio.
inline bool safeName(const std::string& n) {
  if (n.empty() || n.size() > 100 || n == "." || n == "..") return false;
  for (char c : n) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 32) return false;
  return n.back() != ' ' && n.back() != '.';
}

// Pastas do projeto aberto (Assets/ e Scenes/) e resolucao segura de caminhos relativos.
class Project {
public:
  fs::path root = "Project";
  fs::path assetsDir() const { return root / "Assets"; }
  fs::path scenesDir() const { return root / "Scenes"; }

  // Cria Assets/Scripts e Scenes se ainda nao existirem (erros ignorados: criar de novo e inofensivo).
  void ensure() const {
    std::error_code ec;
    fs::create_directories(assetsDir() / "Scripts", ec);
    fs::create_directories(root / "Scripts", ec);   // pasta do jogo: EntityManagerScript.js (M8)
    fs::create_directories(scenesDir(), ec);
  }
  // Caminho relativo ao projeto ("Assets/x.glb") -> caminho real. Retorna vazio se tentar escapar da pasta,
  // por ".." ou por link simbolico apontando para fora (F-12). Ver project_paths.hpp (inclui a limitacao TOCTOU).
  fs::path resolve(const std::string& rel) const { return projpath::resolveInside(root, rel); }
  // Caminho absoluto -> relativo ao projeto, com "/" (formato guardado nas cenas).
  std::string relative(const fs::path& p) const {
    std::error_code ec;
    return fs::relative(p, root, ec).generic_string();
  }
  // Nomes das cenas (arquivos *.scene.json em Scenes/, sem a extensao), em ordem alfabetica.
  std::vector<std::string> listScenes() const {
    std::vector<std::string> out; std::error_code ec;
    if (!fs::exists(scenesDir(), ec)) return out;
    for (auto& e : fs::directory_iterator(scenesDir(), ec)) {
      const std::string f = e.path().filename().string();
      if (f.size() > 11 && f.compare(f.size() - 11, 11, ".scene.json") == 0) out.push_back(f.substr(0, f.size() - 11));
    }
    std::sort(out.begin(), out.end());
    return out;
  }
  fs::path scenePath(const std::string& name) const { return scenesDir() / (name + ".scene.json"); }

  // Copia um arquivo externo para dentro de `dstDir` (sem sobrescrever: acrescenta " (2)", " (3)"...). Retorna o destino.
  fs::path importFile(const fs::path& src, const fs::path& dstDir, std::string* err = nullptr) const {
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { if (err) *err = "not a file: " + src.string(); return {}; }
    fs::create_directories(dstDir, ec);
    fs::path dst = dstDir / src.filename();
    for (int i = 2; fs::exists(dst, ec) && i < 1000; ++i) dst = dstDir / (src.stem().string() + " (" + std::to_string(i) + ")" + src.extension().string());
    fs::copy_file(src, dst, fs::copy_options::none, ec);
    if (ec) { if (err) *err = "copy failed: " + ec.message(); return {}; }
    return dst;
  }
};

// Modelos e sons ja carregados (carrega sob demanda; falhas ficam em cache para nao repetir todo quadro).
class AssetLib {
public:
  explicit AssetLib(const Project* p) : proj_(p) {}
  std::function<void(const tool::Mesh*)> onMeshForget; // o renderizador de GPU precisa saber quando uma malha some
  std::function<void(tool::u64)> onTextureForget;      // idem para texturas: recebe Texture::id (senao a textura antiga fica orfa na GPU)

  // Modelo importado com hierarquia. As partes ficam no espaco local do no delas; `whole` junta tudo no mundo
  // (enquadramento e modelos antigos sem hierarquia); `nodeMeshes` junta as partes de um no no espaco do no.
  struct LoadedModel {
    tool::assets::ModelData data;
    std::vector<tool::Mat4> worlds;
    std::unique_ptr<tool::Mesh> whole;
    std::map<int, std::unique_ptr<tool::Mesh>> nodeMeshes;
  };

  // Carrega (ou pega do cache) o modelo; falha fica em cache.
  LoadedModel* model(const std::string& rel) {
    if (auto it = models_.find(rel); it != models_.end()) return it->second.get();
    if (failed_.count(rel)) return nullptr;
    const fs::path path = proj_->resolve(rel);
    if (path.empty()) { fail(rel, "invalid model path"); return nullptr; }
    tool::assets::ImportResult r = tool::assets::importModelFile(path.string());
    if (!r.ok) { fail(rel, r.error); return nullptr; }
    auto lm = std::make_unique<LoadedModel>();
    lm->data = std::move(r.model);
    if (lm->data.parts.empty()) { fail(rel, "model has no triangles"); return nullptr; }
    lm->worlds = lm->data.worldMatrices();
    auto whole = std::make_unique<tool::Mesh>();
    bool hasUv = true;
    for (auto& p : lm->data.parts) hasUv = hasUv && (p.mesh.uv.size() == p.mesh.pos.size() * 2);
    for (auto& p : lm->data.parts) {
      tool::Mesh c = p.mesh; // copia: a malha do no fica intacta, a copia vai para o mundo
      if (!lm->worlds.empty()) tool::assets::bakeTransform(c, lm->worlds[static_cast<size_t>(p.node)]);
      const tool::u32 base = static_cast<tool::u32>(whole->pos.size());
      whole->pos.insert(whole->pos.end(), c.pos.begin(), c.pos.end());
      whole->normal.insert(whole->normal.end(), c.normal.begin(), c.normal.end());
      if (hasUv) whole->uv.insert(whole->uv.end(), c.uv.begin(), c.uv.end());
      for (auto i : c.idx) whole->idx.push_back(base + i);
    }
    if (whole->pos.empty() || whole->idx.empty()) { fail(rel, "model has no triangles"); return nullptr; }
    lm->whole = std::move(whole);
    tint_[rel] = lm->data.parts[0].material.albedo;
    for (auto& w : lm->data.warnings) tool::logWarn(rel + ": " + w);
    LoadedModel* raw = lm.get();
    models_[rel] = std::move(lm);
    return raw;
  }

  // Malha unica (todas as partes no mundo) + cor media do primeiro material.
  const tool::Mesh* mesh(const std::string& rel, tool::Vec3* tint = nullptr) {
    LoadedModel* lm = model(rel);
    if (!lm) return nullptr;
    if (tint) *tint = tint_[rel];
    return lm->whole.get();
  }
  // Malha de um no: as partes dele juntas, no espaco do no (caixa envolvente e selecao). Null se o no nao tem partes.
  const tool::Mesh* nodeMesh(const std::string& rel, int node) {
    LoadedModel* lm = model(rel);
    if (!lm || node < 0 || static_cast<size_t>(node) >= lm->data.nodes.size()) return nullptr;
    if (auto it = lm->nodeMeshes.find(node); it != lm->nodeMeshes.end()) return it->second.get(); // cache: criada uma vez
    auto m = std::make_unique<tool::Mesh>();
    for (int pi : lm->data.nodes[static_cast<size_t>(node)].parts) {
      const tool::Mesh& c = lm->data.parts[static_cast<size_t>(pi)].mesh;
      const tool::u32 base = static_cast<tool::u32>(m->pos.size());
      m->pos.insert(m->pos.end(), c.pos.begin(), c.pos.end());
      m->normal.insert(m->normal.end(), c.normal.begin(), c.normal.end());
      for (auto i : c.idx) m->idx.push_back(base + i);
    }
    if (m->pos.empty() || m->idx.empty()) return nullptr;
    const tool::Mesh* raw = m.get();
    lm->nodeMeshes[node] = std::move(m);
    return raw;
  }
  // Som decodificado (WAV/MP3), com cache; falha tambem fica em cache e e registrada no log.
  tool::SoundPtr sound(const std::string& rel) {
    if (auto it = sounds_.find(rel); it != sounds_.end()) return it->second;
    if (failed_.count(rel)) return nullptr;
    const fs::path path = proj_->resolve(rel);
    std::string err;
    tool::SoundPtr s = path.empty() ? nullptr : tool::decodeAudioFile(path.string(), &err);
    if (!s) { fail(rel, err.empty() ? "invalid audio path" : err); return nullptr; }
    sounds_[rel] = s;
    return s;
  }
  // Descarta o cache (ex.: o arquivo foi renomeado/editado) para recarregar na proxima vez.
  void invalidate(const std::string& rel) {
    if (auto it = models_.find(rel); it != models_.end()) { forget(*it->second); models_.erase(it); }
    sounds_.erase(rel); failed_.erase(rel); tint_.erase(rel);
    if (auto it = textures_.find(rel); it != textures_.end()) { if (onTextureForget && it->second) onTextureForget(it->second->id); textures_.erase(it); }   // libera a textura da GPU
    failedTex_.erase(rel);
  }
  // Esvazia todos os caches, avisando o renderizador de GPU de cada malha e textura que some.
  void clear() {
    for (auto& [k, m] : models_) forget(*m);
    models_.clear(); sounds_.clear(); failed_.clear(); tint_.clear();
    if (onTextureForget) for (auto& [k, t] : textures_) if (t) onTextureForget(t->id);
    textures_.clear(); failedTex_.clear();
  }

  // Textura de albedo (PNG, JPG, BMP, TGA) do projeto. Falha fica em cache para nao repetir a cada quadro.
  const tool::Texture* texture(const std::string& rel) {
    if (auto it = textures_.find(rel); it != textures_.end()) return it->second.get();
    if (failedTex_.count(rel) || rel.empty()) return nullptr;
    const fs::path path = proj_->resolve(rel);
    auto t = std::make_unique<tool::Texture>();
    std::string err;
    if (path.empty() || !loadImageFile(path.string(), *t, &err)) { failedTex_.insert(rel); tool::logError("could not load texture '" + rel + "': " + (path.empty() ? "invalid path" : err)); return nullptr; }
    const tool::Texture* out = t.get();
    textures_[rel] = std::move(t);
    return out;
  }
  size_t meshCount() const { return models_.size(); }
  size_t soundCount() const { return sounds_.size(); }

private:
  // Avisa o renderizador de GPU de cada malha do modelo que vai sumir.
  void forget(LoadedModel& m) {
    if (!onMeshForget) return;
    onMeshForget(m.whole.get());
    for (auto& [k, v] : m.nodeMeshes) onMeshForget(v.get());
    for (auto& p : m.data.parts) onMeshForget(&p.mesh);
  }
  // Guarda a falha no cache (nao tenta de novo a cada quadro) e escreve no log.
  void fail(const std::string& rel, const std::string& why) { failed_.insert(rel); tool::logError("could not load '" + rel + "': " + why); }
  const Project* proj_;
  std::map<std::string, std::unique_ptr<LoadedModel>> models_;
  std::map<std::string, tool::Vec3> tint_;
  std::map<std::string, tool::SoundPtr> sounds_;
  std::set<std::string> failed_;
  std::map<std::string, std::unique_ptr<tool::Texture>> textures_;
  std::set<std::string> failedTex_;
};

// --- Arquivo de cena -------------------------------------------------------------------------------------------
// { "version":1, "name":"Main", "splitVertical":true, "assets":["Assets/a.glb",...], "render":{"lighting":{...},"post":{...}}, "entities":[ ... ] }
// Configuracao de iluminacao/render DA CENA (Settings > Lighting): Light Prism (luzes, clusters, CSM) e pos-processamento GLOBAL.
struct RenderSettings {
  tool::LightingSettings lighting; // Light Prism: qualidade, orcamento de luzes, sombras em cascata, ambiente
  bool frustumCull = true;         // pula objetos fora da visao da camera (sombras continuam corretas)
  bool occlusionCull = true;       // pula objetos inteiramente escondidos atras de objetos grandes (perspectiva; sombras continuam corretas)
  // Como a oclusao roda: "gpu" (HiZ na GPU; padrao, cai em "cpu" se o backend nao tem compute), "cpu" (software, para teste e
  // medicao) ou "off". Vale so com occlusionCull ligado. Chave ausente = "gpu". Ver docs/planos/34_OCLUSAO_GPU.md.
  std::string occlusionMode = "gpu";
  float drawDistance = 0.f;        // distancia de desenho (0 = usa o Far de cada camera); vale para Scene e Game
  tool::PostFx post;               // vale para todas as cameras; cada camera pode somar/substituir com o seu (postfx.hpp)
  // FSR 1 (AMD, upscale na GPU: EASU + RCAS). So age com "Render scale" abaixo de 1 e API GPU (Vulkan/D3D). Chaves
  // ausentes na cena = estes padroes, entao cenas antigas abrem iguais (FSR desligado).
  bool fsr1 = false;               // liga o upscale FSR 1 no lugar do upscale de software
  // Novos controles de FSR (Settings > Graphics and API). Chave ausente = padrao: cenas antigas abrem iguais.
  bool fsr4 = false;               // escolha do FSR 4 (so guarda a escolha enquanto o renderer nao tem motion vectors)
  bool frameGen = false;           // escolha do Frame generation (FSR 3.1); ainda sem efeito (nao integrado)
  int fsrQuality = 1;              // modo de qualidade do FSR: 0 Ultra Quality .. 4 Ultra Performance, 5 Personalizado (padrao 1 = Quality)
  float fsrSharpness = 0.2f;       // nitidez do RCAS (FSR 1), 0 (sem) a 1 (maxima). Le tambem a chave antiga "fsr1Sharpness"
  float fsrCustomScale = 0.67f;    // fator de render do modo Personalizado (render / saida), 0.5 a 1.0
};

// ---- Modos de qualidade do FSR e escala de render efetiva --------------------------------------------------------
// Fator de ampliacao de cada modo (saida / render): Ultra Quality 1.3x, Quality 1.5x, Balanced 1.7x, Performance 2.0x,
// Ultra Performance 3.0x. O indice 5 (Personalizado) usa fsrCustomScale no lugar.
inline constexpr int kFsrQualityCount = 6;
inline constexpr int kFsrQualityCustom = 5;
inline constexpr float kFsrQualityFactor[5] = {1.3f, 1.5f, 1.7f, 2.0f, 3.0f};
// Modo de qualidade preso em 0..5 (valor lido de arquivo pode estar fora da faixa).
[[nodiscard]] inline int clampFsrQuality(int q) { return std::clamp(q, 0, kFsrQualityCount - 1); }
// Escala de render (render / saida) do modo de qualidade: 1 / fator. Personalizado: o slider proprio (0.5 a 1.0).
[[nodiscard]] inline float fsrRenderScale(const RenderSettings& r) {
  const int q = clampFsrQuality(r.fsrQuality);
  if (q == kFsrQualityCustom) return std::clamp(r.fsrCustomScale, 0.5f, 1.f);
  return 1.f / kFsrQualityFactor[q];
}
// Escala de render EFETIVA, calculada num unico lugar (o renderer usa so esta): com o FSR 1 ou o FSR 4 ligado vale a
// escala derivada do modo de qualidade; sem FSR vale o slider de Render scale.
[[nodiscard]] inline float effectiveRenderScale(const RenderSettings& r, float sliderScale) {
  return (r.fsr1 || r.fsr4) ? fsrRenderScale(r) : sliderScale;
}

// Fisica do MUNDO desta cena (Settings > Physics). `gravity` vale para todos os corpos, em QUALQUER eixo (x, y ou z);
// um objeto com "vector physics" (PhysicsComp::overrideGravity) ignora este valor.
struct PhysicsSettings {
  bool enabled = true;
  float gravity[3] = {0.f, -9.81f, 0.f};
  int solverIterations = 10;   // mais = empilha melhor, custa mais
  int stepHz = 60;             // passo fixo da simulacao
  bool drawColliders = true;   // desenha as caixas/esferas de colisao na janela Scene
  float simRadius = 0.f;       // raio em volta da camera: corpos dinamicos fora dele nao sao simulados (0 = todos)
  // Recursos do Newtoon Physics (chave ausente na cena = este padrao, entao cenas antigas abrem iguais).
  bool softStep = true;        // contatos "soft" (Soft Step): estavel com razoes de massa altas
  bool warmStarting = true;    // reaproveita os impulsos do passo anterior (pilhas mais estaveis)
  bool islands = true;         // ilhas de simulacao: grupos de corpos dormem juntos
  bool interpolation = true;   // interpola a pose entre passos fixos ao desenhar (movimento suave com 144 Hz+)
  float contactHertz = 30.f;   // rigidez do contato soft (5..120)
  float contactDampingRatio = 1.f; // amortecimento do contato soft (0..5)
};

// Prende os campos da fisica em faixas seguras (arquivo editado a mao ou slider fora do limite nao quebra a simulacao).
// NaN/infinito caem no padrao. Funcao pura: testavel sem janela.
inline void clampPhysicsSettings(PhysicsSettings& p) {
  auto fin = [](float v, float d) { return std::isfinite(v) ? v : d; };
  p.contactHertz = std::clamp(fin(p.contactHertz, 30.f), 5.f, 120.f);
  p.contactDampingRatio = std::clamp(fin(p.contactDampingRatio, 1.f), 0.f, 5.f);
}

// Dados da cena fora das entidades: nome, divisao de tela, skybox, assets, render, fisica, narrativa e script.
struct SceneFile {
  std::string name = "Main";
  bool splitVertical = true;
  bool splitFill = false;          // split screen: a camera preenche cada metade (corta a vista) em vez de encaixar com barras
  SkyboxSettings sky;              // skybox de 6 imagens (skybox.hpp)
  std::vector<std::string> assets; // assets "alocados" nesta cena (carregados ao abrir)
  RenderSettings render;
  PhysicsSettings physics;
  std::string narrative; // grafo do Narrative Controller desta cena (relativo ao projeto), ex.: Assets/Narrative/Main.narr.json
  std::string script;    // codigo da cena (.js) que chama o Narrative Controller
  std::string presetPath; // arquivo de configuracao (JSON, render + fisica) desta cena; vazio = padrao do jogo
};

// Fisica da cena -> JSON (par de loadPhysicsSettings).
inline tool::Json savePhysicsSettings(const PhysicsSettings& p) {
  tool::Json o = tool::Json::object();
  o.set("enabled", p.enabled); o.set("gravity", ser::vec(p.gravity, 3)); o.set("solverIterations", p.solverIterations); o.set("stepHz", p.stepHz); o.set("drawColliders", p.drawColliders); o.set("simRadius", p.simRadius);
  o.set("softStep", p.softStep); o.set("warmStarting", p.warmStarting); o.set("islands", p.islands); o.set("interpolation", p.interpolation);
  o.set("contactHertz", p.contactHertz); o.set("contactDampingRatio", p.contactDampingRatio);
  return o;
}
// JSON -> fisica da cena; valores presos em faixas seguras (arquivo editado a mao nao quebra a simulacao).
inline PhysicsSettings loadPhysicsSettings(const tool::Json& o) {
  PhysicsSettings p; p.enabled = ser::flag(o, "enabled", true); ser::readVec(o["gravity"], p.gravity, 3);
  for (float& g : p.gravity) g = std::clamp(g, -1000.f, 1000.f);
  p.solverIterations = std::clamp(static_cast<int>(ser::num(o, "solverIterations", 10.f)), 1, 50); p.stepHz = std::clamp(static_cast<int>(ser::num(o, "stepHz", 60.f)), 15, 240);
  p.drawColliders = ser::flag(o, "drawColliders", true);
  p.simRadius = std::max(0.f, ser::num(o, "simRadius", 0.f));
  p.softStep = ser::flag(o, "softStep", true); p.warmStarting = ser::flag(o, "warmStarting", true);
  p.islands = ser::flag(o, "islands", true); p.interpolation = ser::flag(o, "interpolation", true);
  p.contactHertz = ser::num(o, "contactHertz", 30.f); p.contactDampingRatio = ser::num(o, "contactDampingRatio", 1.f);
  clampPhysicsSettings(p);
  return p;
}

// Light Prism (Settings > Lighting). Cada campo e um parametro direto da biblioteca; os limites evitam valores absurdos
// num .scene.json editado a mao.
inline tool::Json saveLightingSettings(const tool::LightingSettings& s) {
  tool::Json o = tool::Json::object();
  o.set("tier", s.tier); o.set("custom", s.custom); o.set("shadows", s.shadows);
  o.set("maxPointLights", static_cast<double>(s.maxPointLights)); o.set("maxSpotLights", static_cast<double>(s.maxSpotLights));
  o.set("autoRadiusCutoff", static_cast<double>(s.autoRadiusCutoff));
  o.set("maxLightsPerCluster", static_cast<double>(s.maxLightsPerCluster)); o.set("indicesPerCluster", static_cast<double>(s.indicesPerCluster));
  o.set("csmCascades", static_cast<double>(s.csmCascades)); o.set("shadowMapResolution", static_cast<double>(s.shadowMapResolution));
  o.set("pcfTaps", static_cast<double>(s.pcfTaps)); o.set("splitLambda", static_cast<double>(s.splitLambda));
  o.set("shadowDistance", static_cast<double>(s.shadowDistance)); o.set("blendFraction", static_cast<double>(s.blendFraction));
  o.set("casterExtension", static_cast<double>(s.casterExtension)); o.set("depthBias", static_cast<double>(s.depthBias));
  o.set("normalBias", static_cast<double>(s.normalBias)); o.set("snapToTexels", s.snapToTexels);
  o.set("pointShadowLights", static_cast<double>(s.pointShadowLights)); o.set("pointShadowResolution", static_cast<double>(s.pointShadowResolution));
  o.set("ssao", s.ssao); o.set("ssaoKernel", static_cast<double>(s.ssaoKernel)); o.set("ssaoRadius", static_cast<double>(s.ssaoRadius));
  o.set("ssaoBias", static_cast<double>(s.ssaoBias)); o.set("ssaoPower", static_cast<double>(s.ssaoPower));
  o.set("rayTracing", s.rayTracing); o.set("rtAo", s.rtAo); o.set("rtAoRays", static_cast<double>(s.rtAoRays)); o.set("rtAoRadius", static_cast<double>(s.rtAoRadius));
  o.set("bloom", s.bloom); o.set("bloomThreshold", static_cast<double>(s.bloomThreshold)); o.set("bloomStrength", static_cast<double>(s.bloomStrength));
  o.set("ambientIntensity", static_cast<double>(s.ambientIntensity)); o.set("preExposure", static_cast<double>(s.preExposure));
  return o;
}
// JSON -> Light Prism; cada valor preso a uma faixa segura e com padrao quando a chave falta.
inline tool::LightingSettings loadLightingSettings(const tool::Json& o) {
  tool::LightingSettings s;
  s.tier = std::clamp(static_cast<int>(ser::num(o, "tier", 2.f)), 0, 3);
  s.custom = ser::flag(o, "custom", false); s.shadows = ser::flag(o, "shadows", true);
  s.maxPointLights = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "maxPointLights", 4096.f)), 1, 65536));
  s.maxSpotLights = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "maxSpotLights", 1024.f)), 1, 65536));
  s.autoRadiusCutoff = ser::clampNum(o, "autoRadiusCutoff", 0.02f, 0.f, 1.f);
  s.maxLightsPerCluster = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "maxLightsPerCluster", 64.f)), 1, 1024));
  s.indicesPerCluster = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "indicesPerCluster", 8.f)), 1, 64));
  s.csmCascades = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "csmCascades", 3.f)), 1, 4));
  s.shadowMapResolution = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "shadowMapResolution", 2048.f)), 256, 4096));
  const int pcf = static_cast<int>(ser::num(o, "pcfTaps", 4.f));
  s.pcfTaps = (pcf <= 1) ? 1u : (pcf <= 4 ? 4u : 9u);
  s.pointShadowLights = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "pointShadowLights", 2.f)), 0, 4));
  s.pointShadowResolution = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "pointShadowResolution", 512.f)), 64, 1024));
  s.ssao = ser::flag(o, "ssao", true);
  s.ssaoKernel = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "ssaoKernel", 16.f)), 1, 32));
  s.ssaoRadius = ser::clampNum(o, "ssaoRadius", 0.5f, 0.01f, 5.f);
  s.ssaoBias = ser::clampNum(o, "ssaoBias", 0.025f, 0.f, 0.5f);
  s.ssaoPower = ser::clampNum(o, "ssaoPower", 1.f, 0.1f, 4.f);
  s.rayTracing = ser::flag(o, "rayTracing", true);
  s.rtAo = ser::flag(o, "rtAo", false);
  s.rtAoRays = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(o, "rtAoRays", 16.f)), 1, 64));
  s.rtAoRadius = ser::clampNum(o, "rtAoRadius", 0.6f, 0.05f, 5.f);
  s.bloom = ser::flag(o, "bloom", false);
  s.bloomThreshold = ser::clampNum(o, "bloomThreshold", 0.75f, 0.f, 1.f);
  s.bloomStrength = ser::clampNum(o, "bloomStrength", 0.8f, 0.f, 4.f);
  s.splitLambda = ser::clampNum(o, "splitLambda", 0.7f, 0.f, 1.f);
  s.shadowDistance = ser::clampNum(o, "shadowDistance", 0.f, 0.f, 5000.f);
  s.blendFraction = ser::clampNum(o, "blendFraction", 0.1f, 0.f, 0.5f);
  s.casterExtension = ser::clampNum(o, "casterExtension", 60.f, 0.f, 1000.f);
  s.depthBias = ser::clampNum(o, "depthBias", 0.05f, 0.f, 5.f);
  s.normalBias = ser::clampNum(o, "normalBias", 1.5f, 0.f, 8.f);
  s.snapToTexels = ser::flag(o, "snapToTexels", true);
  s.ambientIntensity = ser::clampNum(o, "ambientIntensity", 1.f, 0.f, 10.f);
  s.preExposure = ser::clampNum(o, "preExposure", 1.f, 0.01f, 16.f);
  return s;
}

// Render da cena (iluminacao, pos-processamento, culling, distancia) -> JSON.
inline tool::Json saveRenderSettings(const RenderSettings& r) {
  tool::Json o = tool::Json::object();
  o.set("lighting", saveLightingSettings(r.lighting)); o.set("post", ser::postFx(r.post));
  o.set("frustumCull", r.frustumCull); o.set("occlusionCull", r.occlusionCull); o.set("occlusionMode", r.occlusionMode); o.set("drawDistance", static_cast<double>(r.drawDistance));
  o.set("fsr1", r.fsr1); o.set("fsrSharpness", static_cast<double>(r.fsrSharpness));
  o.set("fsr4", r.fsr4); o.set("frameGen", r.frameGen); o.set("fsrQuality", r.fsrQuality);
  o.set("fsrCustomScale", static_cast<double>(r.fsrCustomScale));
  return o;
}
// Todo valor lido do arquivo e limitado a uma faixa segura (um .scene.json editado a mao nao pode travar o render).
// Cenas antigas (versao 0.7.14 e anteriores) tinham "shadow": essas chaves viram as correspondentes da Light Prism.
inline RenderSettings loadRenderSettings(const tool::Json& o) {
  RenderSettings r;
  if (o.has("lighting")) r.lighting = loadLightingSettings(o["lighting"]);
  else if (o.has("shadow")) {
    const auto& s = o["shadow"];
    r.lighting.shadows = ser::flag(s, "enabled", true);
    r.lighting.shadowMapResolution = static_cast<tool::u32>(std::clamp(static_cast<int>(ser::num(s, "resolution", 2048.f)), 256, 4096));
    const int pcf = static_cast<int>(ser::num(s, "pcf", 1.f));
    r.lighting.pcfTaps = pcf <= 0 ? 1u : (pcf == 1 ? 4u : 9u);
  }
  if (o.has("post")) r.post = ser::readPostFx(o["post"]);
  r.frustumCull = ser::flag(o, "frustumCull", true); r.occlusionCull = ser::flag(o, "occlusionCull", true);
  if (o.has("occlusionMode")) { const std::string& m = o["occlusionMode"].asString(); r.occlusionMode = (m == "cpu" || m == "off") ? m : "gpu"; }   // valor desconhecido = padrao
  r.drawDistance = ser::clampNum(o, "drawDistance", 0.f, 0.f, 20000.f);
  r.fsr1 = ser::flag(o, "fsr1", false);
  // Nitidez: chave nova "fsrSharpness"; cenas antigas tinham "fsr1Sharpness" (migrada aqui, sem perder o valor).
  r.fsrSharpness = ser::clampNum(o, o.has("fsrSharpness") ? "fsrSharpness" : "fsr1Sharpness", 0.2f, 0.f, 1.f);
  r.fsr4 = ser::flag(o, "fsr4", false); r.frameGen = ser::flag(o, "frameGen", false);
  r.fsrQuality = clampFsrQuality(static_cast<int>(ser::num(o, "fsrQuality", 1.f)));
  r.fsrCustomScale = ser::clampNum(o, "fsrCustomScale", 0.67f, 0.5f, 1.f);
  return r;
}

// Grava texto (cria as pastas se faltar); true se escreveu sem erro.
inline bool writeTextFile(const fs::path& p, const std::string& text) {
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc); f << text; return static_cast<bool>(f);
}
// Le um arquivo de texto inteiro; recusa arquivos acima de maxBytes (padrao 8 MB). true se leu.
inline bool readTextFile(const fs::path& p, std::string& out, size_t maxBytes = 8u << 20) {
  std::error_code ec; const auto sz = fs::file_size(p, ec);
  if (ec || sz > maxBytes) return false;
  std::ifstream f(p, std::ios::binary); if (!f) return false;
  std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}

// Skybox -> JSON (image0..5 e solid0..3, onde solid3 e o alfa).
inline tool::Json saveSkybox(const SkyboxSettings& s) {
  tool::Json o = tool::Json::object();
  o.set("enabled", s.enabled);
  for (int i = 0; i < 6; ++i) o.set("image" + std::to_string(i), std::string(s.face[i]));
  for (int i = 0; i < 4; ++i) o.set("solid" + std::to_string(i), static_cast<double>(s.solid[i]));   // solid3 = alfa
  return o;
}
// JSON -> skybox; chaves ausentes (cenas antigas) mantem os valores padrao.
inline SkyboxSettings loadSkybox(const tool::Json& o) {
  SkyboxSettings s;
  s.enabled = ser::flag(o, "enabled", false);
  for (int i = 0; i < 6; ++i) { const std::string k = "image" + std::to_string(i); ser::copyStr(s.face[i], sizeof s.face[i], o[k.c_str()].asString()); } // cenas antigas (faces procedurais) ficam sem imagem
  for (int i = 0; i < 4; ++i) { const std::string k = "solid" + std::to_string(i); s.solid[i] = std::clamp(ser::num(o, k.c_str(), s.solid[i]), 0.f, 1.f); }
  return s;
}

// Grava a cena em Scenes/<nome>.scene.json (versao 1): configuracoes + todas as entidades.
inline bool saveSceneFile(const Project& proj, tool::Scene& sc, const SceneFile& info) {
  tool::Json j = tool::Json::object(), as = tool::Json::array();
  j.set("version", 1); j.set("name", info.name); j.set("splitVertical", info.splitVertical); j.set("splitFill", info.splitFill);
  j.set("skybox", saveSkybox(info.sky));
  for (auto& a : info.assets) as.push(a);
  j.set("assets", as); j.set("render", saveRenderSettings(info.render)); j.set("physics", savePhysicsSettings(info.physics));
  j.set("narrative", info.narrative); j.set("script", info.script);
  j.set("presetPath", info.presetPath);   // config personalizada da cena (vazio = padrao do jogo)
  j.set("entities", saveAllEntities(sc));
  return writeTextFile(proj.scenePath(info.name), j.dump());
}

// Apaga TODOS os objetos da cena atual (usado ao trocar de cena).
inline void clearScene(tool::Scene& sc) {
  std::vector<tool::Entity> all;
  sc.world().eachEntity([&](tool::Entity e) { all.push_back(e); });
  for (auto e : all) sc.world().destroy(e);
  sc.flushPool();
}

// Carrega uma cena: limpa a atual, le configuracoes e recria as entidades. false e *err se o arquivo for invalido.
inline bool loadSceneFile(const Project& proj, tool::Scene& sc, const std::string& name, SceneFile& info, std::string* err = nullptr) {
  std::string text;
  if (!readTextFile(proj.scenePath(name), text)) { if (err) *err = "cannot read scene '" + name + "'"; return false; }
  auto j = tool::Json::parse(text);
  if (!j || !j->isObject()) { if (err) *err = "scene file is not valid JSON"; return false; }
  clearScene(sc);
  info = SceneFile{};
  info.name = name;
  info.splitVertical = ser::flag(*j, "splitVertical", true);
  info.splitFill = ser::flag(*j, "splitFill", false);
  if (j->has("skybox")) info.sky = loadSkybox((*j)["skybox"]);   // cena antiga (sem a chave) fica sem skybox
  for (auto& a : (*j)["assets"].items()) if (a.isString()) info.assets.push_back(a.asString());
  if (j->has("render")) info.render = loadRenderSettings((*j)["render"]);
  if (j->has("physics")) info.physics = loadPhysicsSettings((*j)["physics"]);
  info.narrative = (*j)["narrative"].asString(); info.script = (*j)["script"].asString();
  if (info.narrative.size() > 160 || info.script.size() > 160) { info.narrative.clear(); info.script.clear(); } // entrada nao confiavel
  info.presetPath = (*j)["presetPath"].asString();   // chave ausente (cena antiga) = vazio = padrao do jogo
  loadAllEntities(sc, (*j)["entities"]);
  return true;
}

} // namespace editor
