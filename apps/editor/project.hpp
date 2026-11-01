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
#include "skybox.hpp"
#include "tool/assets/import.hpp"
#include "tool/audio/decode.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/shadow.hpp"

#include <algorithm>
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

inline std::string lowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

enum class AssetKind { Folder, Model, Audio, Scene, Image, Text, Other };
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

class Project {
public:
  fs::path root = "Project";
  fs::path assetsDir() const { return root / "Assets"; }
  fs::path scenesDir() const { return root / "Scenes"; }

  void ensure() const {
    std::error_code ec;
    fs::create_directories(assetsDir() / "Scripts", ec);
    fs::create_directories(scenesDir(), ec);
  }
  // Caminho relativo ao projeto ("Assets/x.glb") -> caminho real. Retorna vazio se tentar escapar da pasta.
  fs::path resolve(const std::string& rel) const {
    if (rel.empty()) return {};
    const fs::path p = fs::path(rel).lexically_normal();
    if (p.is_absolute() || p.has_root_name()) return {};
    for (auto& part : p) if (part == "..") return {};
    return root / p;
  }
  std::string relative(const fs::path& p) const {
    std::error_code ec;
    return fs::relative(p, root, ec).generic_string();
  }
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

  // Malha unica (todas as partes do modelo juntas) + cor media do primeiro material.
  const tool::Mesh* mesh(const std::string& rel, tool::Vec3* tint = nullptr) {
    if (auto it = meshes_.find(rel); it != meshes_.end()) { if (tint) *tint = tint_[rel]; return it->second.get(); }
    if (failed_.count(rel)) return nullptr;
    const fs::path path = proj_->resolve(rel);
    if (path.empty()) { fail(rel, "invalid model path"); return nullptr; }
    tool::assets::ImportResult r = tool::assets::importModelFile(path.string());
    if (!r.ok) { fail(rel, r.error); return nullptr; }
    auto m = std::make_unique<tool::Mesh>();
    bool hasUv = true;
    for (auto& p : r.model.parts) hasUv = hasUv && (p.mesh.uv.size() == p.mesh.pos.size() * 2);
    for (auto& p : r.model.parts) {
      const tool::u32 base = static_cast<tool::u32>(m->pos.size());
      m->pos.insert(m->pos.end(), p.mesh.pos.begin(), p.mesh.pos.end());
      m->normal.insert(m->normal.end(), p.mesh.normal.begin(), p.mesh.normal.end());
      if (hasUv) m->uv.insert(m->uv.end(), p.mesh.uv.begin(), p.mesh.uv.end());
      for (auto i : p.mesh.idx) m->idx.push_back(base + i);
    }
    if (m->pos.empty() || m->idx.empty()) { fail(rel, "model has no triangles"); return nullptr; }
    tint_[rel] = r.model.parts.empty() ? tool::Vec3{0.8f, 0.8f, 0.8f} : r.model.parts[0].material.albedo;
    for (auto& w : r.model.warnings) tool::logWarn(rel + ": " + w);
    const tool::Mesh* raw = m.get();
    meshes_[rel] = std::move(m);
    if (tint) *tint = tint_[rel];
    return raw;
  }
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
    if (auto it = meshes_.find(rel); it != meshes_.end()) { if (onMeshForget) onMeshForget(it->second.get()); meshes_.erase(it); }
    sounds_.erase(rel); failed_.erase(rel); tint_.erase(rel);
    textures_.erase(rel); failedTex_.erase(rel);
  }
  void clear() { for (auto& [k, m] : meshes_) if (onMeshForget) onMeshForget(m.get()); meshes_.clear(); sounds_.clear(); failed_.clear(); tint_.clear(); textures_.clear(); failedTex_.clear(); }

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
  size_t meshCount() const { return meshes_.size(); }
  size_t soundCount() const { return sounds_.size(); }

private:
  void fail(const std::string& rel, const std::string& why) { failed_.insert(rel); tool::logError("could not load '" + rel + "': " + why); }
  const Project* proj_;
  std::map<std::string, std::unique_ptr<tool::Mesh>> meshes_;
  std::map<std::string, tool::Vec3> tint_;
  std::map<std::string, tool::SoundPtr> sounds_;
  std::set<std::string> failed_;
  std::map<std::string, std::unique_ptr<tool::Texture>> textures_;
  std::set<std::string> failedTex_;
};

// --- Arquivo de cena -------------------------------------------------------------------------------------------
// { "version":1, "name":"Main", "splitVertical":true, "assets":["Assets/a.glb",...], "render":{"shadow":{...},"post":{...}}, "entities":[ ... ] }
// Configuracao de iluminacao/render DA CENA (Settings > Lighting): sombras em tempo real e pos-processamento GLOBAL.
struct RenderSettings {
  tool::ShadowSettings shadow;
  bool frustumCull = true;         // pula objetos fora da visao da camera (sombras continuam corretas)
  tool::PostFx post;               // vale para todas as cameras; cada camera pode somar/substituir com o seu (postfx.hpp)
};

// Fisica do MUNDO desta cena (Settings > Physics). `gravity` vale para todos os corpos, em QUALQUER eixo (x, y ou z);
// um objeto com "vector physics" (PhysicsComp::overrideGravity) ignora este valor.
struct PhysicsSettings {
  bool enabled = true;
  float gravity[3] = {0.f, -9.81f, 0.f};
  int solverIterations = 10;   // mais = empilha melhor, custa mais
  int stepHz = 60;             // passo fixo da simulacao
  bool drawColliders = true;   // desenha as caixas/esferas de colisao na janela Scene
};

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
};

inline tool::Json savePhysicsSettings(const PhysicsSettings& p) {
  tool::Json o = tool::Json::object();
  o.set("enabled", p.enabled); o.set("gravity", ser::vec(p.gravity, 3)); o.set("solverIterations", p.solverIterations); o.set("stepHz", p.stepHz); o.set("drawColliders", p.drawColliders);
  return o;
}
inline PhysicsSettings loadPhysicsSettings(const tool::Json& o) {
  PhysicsSettings p; p.enabled = ser::flag(o, "enabled", true); ser::readVec(o["gravity"], p.gravity, 3);
  for (float& g : p.gravity) g = std::clamp(g, -1000.f, 1000.f);
  p.solverIterations = std::clamp(static_cast<int>(ser::num(o, "solverIterations", 10.f)), 1, 50); p.stepHz = std::clamp(static_cast<int>(ser::num(o, "stepHz", 60.f)), 15, 240);
  p.drawColliders = ser::flag(o, "drawColliders", true);
  return p;
}

inline tool::Json saveRenderSettings(const RenderSettings& r) {
  tool::Json o = tool::Json::object(), s = tool::Json::object();
  s.set("enabled", r.shadow.enabled); s.set("resolution", r.shadow.resolution); s.set("distance", static_cast<double>(r.shadow.distance));
  s.set("bias", static_cast<double>(r.shadow.bias)); s.set("normalBias", static_cast<double>(r.shadow.normalBias)); s.set("pcf", r.shadow.pcf);
  s.set("strength", static_cast<double>(r.shadow.strength));
  o.set("shadow", s); o.set("post", ser::postFx(r.post)); o.set("frustumCull", r.frustumCull);
  return o;
}
// Todo valor lido do arquivo e limitado a uma faixa segura (um .scene.json editado a mao nao pode travar o render).
inline RenderSettings loadRenderSettings(const tool::Json& o) {
  RenderSettings r; const auto& s = o["shadow"];
  r.shadow.enabled = ser::flag(s, "enabled", true);
  r.shadow.resolution = std::clamp(static_cast<int>(ser::num(s, "resolution", 1024.f)), 256, 4096);
  r.shadow.distance = ser::clampNum(s, "distance", 40.f, 2.f, 500.f); r.shadow.bias = ser::clampNum(s, "bias", 0.0025f, 0.f, 0.05f);
  r.shadow.normalBias = ser::clampNum(s, "normalBias", 1.5f, 0.f, 8.f); r.shadow.pcf = std::clamp(static_cast<int>(ser::num(s, "pcf", 1.f)), 0, 2);
  r.shadow.strength = ser::clampNum(s, "strength", 0.85f, 0.f, 1.f);
  if (o.has("post")) r.post = ser::readPostFx(o["post"]);
  r.frustumCull = ser::flag(o, "frustumCull", true);
  return r;
}

inline bool writeTextFile(const fs::path& p, const std::string& text) {
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc); f << text; return static_cast<bool>(f);
}
inline bool readTextFile(const fs::path& p, std::string& out, size_t maxBytes = 8u << 20) {
  std::error_code ec; const auto sz = fs::file_size(p, ec);
  if (ec || sz > maxBytes) return false;
  std::ifstream f(p, std::ios::binary); if (!f) return false;
  std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}

inline tool::Json saveSkybox(const SkyboxSettings& s) {
  tool::Json o = tool::Json::object();
  o.set("enabled", s.enabled);
  for (int i = 0; i < 6; ++i) o.set("image" + std::to_string(i), std::string(s.face[i]));
  for (int i = 0; i < 3; ++i) o.set("solid" + std::to_string(i), static_cast<double>(s.solid[i]));
  return o;
}
inline SkyboxSettings loadSkybox(const tool::Json& o) {
  SkyboxSettings s;
  s.enabled = ser::flag(o, "enabled", false);
  for (int i = 0; i < 6; ++i) { const std::string k = "image" + std::to_string(i); ser::copyStr(s.face[i], sizeof s.face[i], o[k.c_str()].asString()); } // cenas antigas (faces procedurais) ficam sem imagem
  for (int i = 0; i < 3; ++i) { const std::string k = "solid" + std::to_string(i); s.solid[i] = std::clamp(ser::num(o, k.c_str(), s.solid[i]), 0.f, 1.f); }
  return s;
}

inline bool saveSceneFile(const Project& proj, tool::Scene& sc, const SceneFile& info) {
  tool::Json j = tool::Json::object(), as = tool::Json::array();
  j.set("version", 1); j.set("name", info.name); j.set("splitVertical", info.splitVertical); j.set("splitFill", info.splitFill);
  j.set("skybox", saveSkybox(info.sky));
  for (auto& a : info.assets) as.push(a);
  j.set("assets", as); j.set("render", saveRenderSettings(info.render)); j.set("physics", savePhysicsSettings(info.physics));
  j.set("narrative", info.narrative); j.set("script", info.script);
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
  loadAllEntities(sc, (*j)["entities"]);
  return true;
}

} // namespace editor
