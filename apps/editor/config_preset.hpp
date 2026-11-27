// ToolEngine Editor - presets de configuracao (render + fisica) em arquivos JSON.
//
// Um preset e um arquivo <projeto>/Configs/<nome>.json com as configuracoes de Settings (iluminacao, pos-processamento,
// culling, fisica). Uma cena pode apontar para um preset; sem preset proprio ela usa Configs/default.json (padrao do jogo).
// Formato: { "version":1, "render":{...}, "physics":{...} } (mesmas funcoes de project.hpp: saveRenderSettings/savePhysicsSettings).
// Chave ausente ao carregar = valor padrao, e todo valor e preso em faixa segura (arquivo editado a mao nao quebra nada).
// Sem janela e sem estado: so leitura/gravacao de arquivos, entao roda em teste.
#pragma once
#include "project.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace editor::config_preset {

// Um preset encontrado na pasta de configuracoes.
struct Entry {
  std::string name;  // nome sem a extensao (o que aparece na lista)
  std::string path;  // caminho completo do arquivo .json
};

// Pasta dos presets: "<projeto>/Configs".
inline std::string folder(const std::string& projectDir) { return (fs::path(projectDir) / "Configs").string(); }

// Padrao do jogo: "<projeto>/Configs/default.json" (pode nao existir ainda).
inline std::string defaultPath(const std::string& projectDir) { return (fs::path(folder(projectDir)) / "default.json").string(); }

// Nome valido para arquivo: caracteres proibidos (/ \ : * ? " < > | e controles) viram "_"; espacos/pontos das pontas somem;
// limitado a 100 caracteres. Resultado vazio = nome invalido.
inline std::string sanitizeName(const std::string& raw) {
  std::string s;
  for (char c : raw) {
    const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 32;
    s.push_back(bad ? '_' : c);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
  size_t b = 0; while (b < s.size() && (s[b] == ' ' || s[b] == '.')) ++b;
  s = s.substr(b);
  if (s.size() > 100) s.resize(100);
  while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
  return s;
}

// Lista os *.json da pasta de presets, em ordem alfabetica pelo nome. Pasta ausente = lista vazia.
inline std::vector<Entry> list(const std::string& projectDir) {
  std::vector<Entry> out;
  std::error_code ec;
  for (fs::directory_iterator it(folder(projectDir), ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || lowerExt(it->path()) != ".json") continue;
    out.push_back({it->path().stem().string(), it->path().string()});
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
  return out;
}

// Grava as configuracoes (render + fisica) de `info` em <projeto>/Configs/<nome>.json. Nome vazio/invalido = erro.
// `err` recebe o motivo quando devolve false.
inline bool save(const std::string& projectDir, const std::string& name, const SceneFile& info, std::string& err) {
  const std::string clean = sanitizeName(name);
  if (clean.empty()) { err = "config name is empty"; return false; }
  tool::Json j = tool::Json::object();
  j.set("version", 1);
  j.set("render", saveRenderSettings(info.render));
  j.set("physics", savePhysicsSettings(info.physics));
  const fs::path p = fs::path(folder(projectDir)) / (clean + ".json");
  if (!writeTextFile(p, j.dump())) { err = "cannot write '" + p.string() + "'"; return false; }
  return true;
}

// Le um preset e aplica render e fisica em `info` (so esses dois campos mudam). Chave ausente = padrao.
// Se o arquivo nao abre ou nao e JSON de objeto, devolve false e `info` fica intacto.
inline bool load(const std::string& path, SceneFile& info, std::string& err) {
  std::string text;
  if (!readTextFile(path, text)) { err = "cannot read '" + path + "'"; return false; }
  auto j = tool::Json::parse(text);
  if (!j || !j->isObject()) { err = "config file is not valid JSON"; return false; }
  info.render = j->has("render") ? loadRenderSettings((*j)["render"]) : RenderSettings{};
  info.physics = j->has("physics") ? loadPhysicsSettings((*j)["physics"]) : PhysicsSettings{};
  return true;
}

// Apaga um arquivo de preset (so .json). false e `err` se nao existir ou nao for .json.
inline bool remove(const std::string& path, std::string& err) {
  if (lowerExt(path) != ".json") { err = "not a config file"; return false; }
  std::error_code ec;
  if (!fs::remove(path, ec) || ec) { err = "cannot delete '" + path + "'"; return false; }
  return true;
}

// Preset a usar numa cena: o caminho da cena se o arquivo existir; senao o padrao do jogo (defaultPath).
inline std::string resolveForScene(const std::string& projectDir, const std::string& scenePresetPath) {
  std::error_code ec;
  if (!scenePresetPath.empty() && fs::is_regular_file(scenePresetPath, ec)) return scenePresetPath;
  return defaultPath(projectDir);
}

} // namespace editor::config_preset
