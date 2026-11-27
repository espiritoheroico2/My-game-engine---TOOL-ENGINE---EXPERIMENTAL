// ToolEngine Editor - material como asset: arquivo <projeto>/Materials/<nome>.json reutilizado por malhas e particulas.
// Formato: objeto JSON com chaves em camelCase; chave ausente = valor padrao e todo valor e preso em faixa segura
// (arquivo editado a mao nao quebra nada). Sem janela e sem estado: so leitura/gravacao de arquivos, entao roda em teste.
#pragma once
#include "project.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace editor::material_asset {

// Como o material se mistura com o que ja esta na tela.
enum class Blend : uint8_t { Opaque, Alpha, Additive };

// Dados de um material.
struct MaterialAsset {
  std::string name;
  float albedo[4] = {1.f, 1.f, 1.f, 1.f};   // RGBA; o alfa e usado na mistura
  float roughness = 0.5f;                   // 0..1
  float metallic = 0.f;                     // 0..1
  float emissive[3] = {0.f, 0.f, 0.f};
  float emissiveStrength = 0.f;             // >= 0
  std::string texture;                      // relativo ao projeto; vazio = sem textura
  Blend blend = Blend::Opaque;
  std::string shader = "pbr";               // pipeline existente no renderer: "pbr" ou "unlit"
  bool doubleSided = false;                 // true: desenha as duas faces (pipeline sem cull)
  std::string normalMap;                    // textura de normais (espaco tangente), relativa ao projeto; vazio = sem
  float normalStrength = 1.f;               // 0..2 (0 = ignora o mapa, 1 = normal do mapa, 2 = exagerado)
  float uvTiling[2] = {1.f, 1.f};           // repeticoes da textura em U/V; > 0
  float uvOffset[2] = {0.f, 0.f};           // deslocamento da UV
  float alphaCutoff = 0.f;                  // 0..1; > 0 descarta pixels com alfa menor (so vale em Blend::Alpha)
};

// True se o nome do shader esta na lista fixa (pbr, unlit).
inline bool isKnownShader(const std::string& s) { return s == "pbr" || s == "unlit"; }

// Nome valido para arquivo: / \ : * ? " < > | e controles viram "_"; espacos/pontos das pontas somem; max 100;
// vazio vira "material".
inline std::string sanitizeName(const std::string& raw) {
  std::string s;
  for (char c : raw) {
    const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 32;
    s.push_back(bad ? '_' : c);
  }
  size_t b = 0; while (b < s.size() && (s[b] == ' ' || s[b] == '.')) ++b;
  s = s.substr(b);
  if (s.size() > 100) s.resize(100);
  while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
  return s.empty() ? std::string("material") : s;
}

// Pasta dos materiais: "<projeto>/Materials".
inline std::string folder(const std::string& projectDir) { return (fs::path(projectDir) / "Materials").string(); }

// Arquivo do material: "<projeto>/Materials/<nome sanitizado>.json".
inline std::string pathFor(const std::string& projectDir, const std::string& name) {
  return (fs::path(folder(projectDir)) / (sanitizeName(name) + ".json")).string();
}

// Devolve uma copia com todos os valores presos nas faixas validas; shader desconhecido vira "pbr".
inline MaterialAsset clamped(MaterialAsset m) {
  auto c01 = [](float v) { return v != v ? 0.f : std::clamp(v, 0.f, 1.f); };   // NaN vira 0
  for (float& a : m.albedo) a = c01(a);
  m.roughness = c01(m.roughness);
  m.metallic = c01(m.metallic);
  m.emissiveStrength = (m.emissiveStrength != m.emissiveStrength) ? 0.f : std::max(0.f, m.emissiveStrength);
  if (!isKnownShader(m.shader)) m.shader = "pbr";
  // Campos novos: forca 0..2, tiling > 0 (invalido/NaN/<=0 vira 1), offset finito, cutoff 0..1.
  m.normalStrength = m.normalStrength != m.normalStrength ? 1.f : std::clamp(m.normalStrength, 0.f, 2.f);
  for (float& t : m.uvTiling) t = (t != t || t <= 0.f) ? 1.f : std::min(t, 1000.f);
  for (float& o : m.uvOffset) o = (o != o) ? 0.f : std::clamp(o, -1000.f, 1000.f);
  m.alphaCutoff = c01(m.alphaCutoff);
  return m;
}

// Texto JSON -> material (ja preso nas faixas). Chave ausente = padrao. JSON invalido: false e `err`.
// Em sucesso, `err` fica vazio, ou traz um aviso (ex.: shader desconhecido trocado por "pbr").
inline bool fromJson(const std::string& text, MaterialAsset& out, std::string& err) {
  err.clear();
  auto j = tool::Json::parse(text);
  if (!j || !j->isObject()) { err = "material file is not valid JSON"; return false; }
  MaterialAsset m;
  if (j->has("name")) m.name = (*j)["name"].asString();
  const auto& al = (*j)["albedo"].items();
  for (size_t i = 0; i < 4 && i < al.size(); ++i) m.albedo[i] = static_cast<float>(al[i].asNumber(m.albedo[i]));
  m.roughness = static_cast<float>((*j)["roughness"].asNumber(m.roughness));
  m.metallic = static_cast<float>((*j)["metallic"].asNumber(m.metallic));
  const auto& em = (*j)["emissive"].items();
  for (size_t i = 0; i < 3 && i < em.size(); ++i) m.emissive[i] = static_cast<float>(em[i].asNumber(m.emissive[i]));
  m.emissiveStrength = static_cast<float>((*j)["emissiveStrength"].asNumber(m.emissiveStrength));
  if (j->has("texture")) m.texture = (*j)["texture"].asString();
  if (j->has("blend")) {
    const std::string& b = (*j)["blend"].asString();
    m.blend = b == "alpha" ? Blend::Alpha : b == "additive" ? Blend::Additive : Blend::Opaque;
  }
  if (j->has("shader")) {
    m.shader = (*j)["shader"].asString();
    if (!isKnownShader(m.shader)) err = "unknown shader '" + m.shader + "', using 'pbr'";
  }
  m.doubleSided = (*j)["doubleSided"].asBool(m.doubleSided);
  // Campos novos: chave ausente (material antigo) = padrao.
  if (j->has("normalMap")) m.normalMap = (*j)["normalMap"].asString();
  m.normalStrength = static_cast<float>((*j)["normalStrength"].asNumber(m.normalStrength));
  const auto& tl = (*j)["uvTiling"].items();
  for (size_t i = 0; i < 2 && i < tl.size(); ++i) m.uvTiling[i] = static_cast<float>(tl[i].asNumber(m.uvTiling[i]));
  const auto& of = (*j)["uvOffset"].items();
  for (size_t i = 0; i < 2 && i < of.size(); ++i) m.uvOffset[i] = static_cast<float>(of[i].asNumber(m.uvOffset[i]));
  m.alphaCutoff = static_cast<float>((*j)["alphaCutoff"].asNumber(m.alphaCutoff));
  out = clamped(std::move(m));
  return true;
}

// Material -> texto JSON (blend como "opaque"/"alpha"/"additive").
inline std::string toJson(const MaterialAsset& m) {
  tool::Json j = tool::Json::object();
  j.set("name", m.name);
  tool::Json al = tool::Json::array();
  for (float v : m.albedo) al.push(static_cast<double>(v));
  j.set("albedo", al);
  j.set("roughness", static_cast<double>(m.roughness));
  j.set("metallic", static_cast<double>(m.metallic));
  tool::Json em = tool::Json::array();
  for (float v : m.emissive) em.push(static_cast<double>(v));
  j.set("emissive", em);
  j.set("emissiveStrength", static_cast<double>(m.emissiveStrength));
  j.set("texture", m.texture);
  j.set("blend", m.blend == Blend::Alpha ? "alpha" : m.blend == Blend::Additive ? "additive" : "opaque");
  j.set("shader", m.shader);
  j.set("doubleSided", m.doubleSided);
  j.set("normalMap", m.normalMap);
  j.set("normalStrength", static_cast<double>(m.normalStrength));
  tool::Json tl = tool::Json::array();
  for (float v : m.uvTiling) tl.push(static_cast<double>(v));
  j.set("uvTiling", tl);
  tool::Json of = tool::Json::array();
  for (float v : m.uvOffset) of.push(static_cast<double>(v));
  j.set("uvOffset", of);
  j.set("alphaCutoff", static_cast<double>(m.alphaCutoff));
  return j.dump();
}

// Grava o material (preso nas faixas) em <projeto>/Materials/<nome>.json, criando a pasta se preciso.
inline bool save(const std::string& projectDir, const MaterialAsset& m, std::string& err) {
  const fs::path p = pathFor(projectDir, m.name);
  if (!writeTextFile(p, toJson(clamped(m)))) { err = "cannot write '" + p.string() + "'"; return false; }
  return true;
}

// Le um material de um arquivo .json.
inline bool load(const std::string& path, MaterialAsset& out, std::string& err) {
  std::string text;
  if (!readTextFile(path, text)) { err = "cannot read '" + path + "'"; return false; }
  return fromJson(text, out, err);
}

// Nomes (sem extensao) dos *.json da pasta de materiais, em ordem alfabetica. Pasta ausente = vazia.
inline std::vector<std::string> listNames(const std::string& projectDir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(folder(projectDir), ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || lowerExt(it->path()) != ".json") continue;
    out.push_back(it->path().stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Apaga um material. So aceita .json dentro de uma pasta chamada "Materials" (protege contra apagar outros arquivos).
inline bool remove(const std::string& path, std::string& err) {
  const fs::path p(path);
  if (lowerExt(p) != ".json" || p.parent_path().filename() != "Materials") { err = "not a material file"; return false; }
  std::error_code ec;
  if (!fs::remove(p, ec) || ec) { err = "cannot delete '" + path + "'"; return false; }
  return true;
}

} // namespace editor::material_asset
