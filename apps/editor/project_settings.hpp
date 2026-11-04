// ToolEngine Editor - configuracoes DO PROJETO (Project/project.json): ligacoes entre tags e preset de exportacao.
//
// TAGS PRE-DEFINIDAS: algumas tags tem significado para a engine ("Principal" = camera ativa do jogo, "player",
// "enemy"). Elas nao podem ser renomeadas. Para usar outro nome no seu projeto, crie sua tag (ex.: "jogador") e
// LIGUE-A a uma pre-definida em Settings > Tags: a engine passa a tratar "jogador" como "player".
//
// EXPORTACAO: "Editor info" guarda os dados do estudio/criador (usados no pacote e no export.json). O jogo
// exportado e uma copia do proprio editor em modo --player mais a pasta Data/Project (ver export.hpp).
//
// SEGURANCA: o arquivo e texto editavel -> toda leitura limita tamanho/quantidade e so aceita alvos conhecidos.
#pragma once
#include "project.hpp"

#include <array>
#include <string>
#include <vector>

namespace editor {

struct BuiltinTag { const char* name; const char* meaning; };
inline constexpr std::array<BuiltinTag, 3> kBuiltinTags = {{
  {"Principal", "Active game camera (two = split screen)"},
  {"player", "The player object"},
  {"enemy", "Enemies / things that attack the player"},
}};
inline bool isBuiltinTag(const std::string& n) { for (auto& b : kBuiltinTags) if (n == b.name) return true; return false; }

struct TagLink { std::string alias, target; };  // alias (tag do usuario) -> target (tag pre-definida)

// Plataformas de exportacao previstas (ver doc de design, secao de exportacao).
inline constexpr std::array<const char*, 6> kExportPlatforms = {"Windows x64", "Windows ARM64", "Linux x64", "Android (ARM64)", "Web (WebAssembly)", "UWP"};
// Quais plataformas ESTE build consegue exportar: a copia do editor so roda no sistema em que foi compilada.
// Windows x64 no build Windows, Linux x64 no build Linux. Android, Web e UWP ainda precisam de toolchain proprio.
inline bool platformAvailable(int i) {
#if defined(_WIN32)
  return i == 0;
#elif defined(__linux__)
  return i == 2;
#else
  return false;
#endif
}

struct ExportPreset {
  int platform = 0;                // indice em kExportPlatforms
  char appName[48] = "MyGame";
  bool compressAssets = true, stripEditorData = true;
};

struct ProjectSettings {
  std::vector<TagLink> links;
  ExportPreset exportPreset;
  // Editor de codigo externo para o botao "Edit external" dos scripts. Vazio = padrao da plataforma (Windows: notepad.exe). Executado SEM shell.
  char externalEditor[160] = "";
  // Identificacao do projeto, escolhida na janela de inicio (nome e autor). Ficam em project.json.
  std::string projectName, author;
  // "Editor info": dados do estudio e do jogo que vao para o pacote exportado (export.json).
  struct Info { std::string studio, country, city, version = "1.0.0.0", description, copyright, website; } info;

  // Uma string por campo obrigatorio que ainda falta (ou tem valor invalido). Vazio = pronto para exportar.
  std::vector<std::string> missingInfo() const {
    std::vector<std::string> m;
    if (projectName.empty()) m.push_back("Product name");
    if (author.empty()) m.push_back("Creator");
    if (info.studio.empty()) m.push_back("Studio");
    if (info.country.empty()) m.push_back("Country");
    if (!validVersion(info.version)) m.push_back("Version");
    if (info.description.empty()) m.push_back("Description");
    return m;
  }
  // Versao no formato 1.2 ate 1.2.3.4; cada parte e um numero de 0 a 65535 (limite do Windows).
  static bool validVersion(const std::string& v) {
    int parts = 0; size_t i = 0;
    while (true) {
      size_t start = i; long n = 0;
      while (i < v.size() && v[i] >= '0' && v[i] <= '9') { n = n * 10 + (v[i] - '0'); if (n > 65535) return false; ++i; }
      if (i == start) return false;
      ++parts;
      if (i == v.size()) break;
      if (v[i] != '.') return false;
      ++i;
    }
    return parts >= 1 && parts <= 4;
  }
  // Copyright automatico quando o usuario deixa em branco: "Copyright (c) <ano> <estudio>".
  std::string copyrightText(int year) const {
    if (!info.copyright.empty()) return info.copyright;
    return "Copyright (c) " + std::to_string(year) + " " + info.studio;
  }

  // Tags (nomes) que a engine deve tratar como `builtin`: ela mesma + todas as ligadas a ela.
  std::vector<std::string> namesFor(const std::string& builtin) const {
    std::vector<std::string> out{builtin};
    for (auto& l : links) if (l.target == builtin) out.push_back(l.alias);
    return out;
  }
  // Cria/atualiza a ligacao. Recusa alias vazio/longo, alias igual a uma pre-definida, alvo desconhecido.
  bool link(const std::string& alias, const std::string& target) {
    if (alias.empty() || alias.size() > 40 || isBuiltinTag(alias) || !isBuiltinTag(target)) return false;
    for (auto& l : links) if (l.alias == alias) { l.target = target; return true; }
    if (links.size() >= 64) return false;
    links.push_back({alias, target});
    return true;
  }
  void unlink(const std::string& alias) { for (size_t i = 0; i < links.size(); ++i) if (links[i].alias == alias) { links.erase(links.begin() + static_cast<long>(i)); return; } }
  void renameAlias(const std::string& from, const std::string& to) { for (auto& l : links) if (l.alias == from) l.alias = to; }

  tool::Json toJson() const {
    using tool::Json;
    Json j = Json::object(), ls = Json::array(), ex = Json::object();
    j.set("version", 1);
    for (auto& l : links) { Json o = Json::object(); o.set("alias", l.alias); o.set("target", l.target); ls.push(o); }
    j.set("tagLinks", ls);
    ex.set("platform", exportPreset.platform); ex.set("appName", std::string(exportPreset.appName));
    ex.set("compressAssets", exportPreset.compressAssets); ex.set("stripEditorData", exportPreset.stripEditorData);
    j.set("exportPreset", ex); j.set("externalEditor", std::string(externalEditor));
    j.set("name", projectName); j.set("author", author);
    Json in = Json::object();
    in.set("studio", info.studio); in.set("country", info.country); in.set("city", info.city); in.set("version", info.version);
    in.set("description", info.description); in.set("copyright", info.copyright); in.set("website", info.website);
    j.set("editorInfo", in);
    return j;
  }
  void fromJson(const tool::Json& j) {
    *this = ProjectSettings{};
    size_t n = 0;
    for (auto& o : j["tagLinks"].items()) if (++n <= 64 && o.isObject()) link(o["alias"].asString(), o["target"].asString());
    ser::copyStr(externalEditor, sizeof externalEditor, j["externalEditor"].asString());
    projectName = j["name"].asString().substr(0, 100);
    author = j["author"].asString().substr(0, 100);
    const auto& in = j["editorInfo"];
    info.studio = in["studio"].asString().substr(0, 100); info.country = in["country"].asString().substr(0, 100);
    info.city = in["city"].asString().substr(0, 100);
    if (!in["version"].asString().empty()) info.version = in["version"].asString().substr(0, 32);
    info.description = in["description"].asString().substr(0, 400); info.copyright = in["copyright"].asString().substr(0, 200);
    info.website = in["website"].asString().substr(0, 200);
    const auto& ex = j["exportPreset"];
    exportPreset.platform = std::clamp(static_cast<int>(ser::num(ex, "platform", 0.f)), 0, static_cast<int>(kExportPlatforms.size()) - 1);
    ser::copyStr(exportPreset.appName, sizeof exportPreset.appName, ex["appName"].asString().empty() ? "MyGame" : ex["appName"].asString());
    exportPreset.compressAssets = ser::flag(ex, "compressAssets", true); exportPreset.stripEditorData = ser::flag(ex, "stripEditorData", true);
  }
  bool save(const Project& p) const { return writeTextFile(p.root / "project.json", toJson().dump()); }
  bool load(const Project& p) {
    std::string text;
    if (!readTextFile(p.root / "project.json", text, 1u << 20)) return false;
    auto j = tool::Json::parse(text);
    if (!j || !j->isObject()) return false;
    fromJson(*j);
    return true;
  }
};

// Cria um projeto NOVO em <location>/<name>: pastas Assets/Scripts e Scenes e project.json (nome e autor).
// Recusa nome invalido (safeName) e pasta que ja tem um projeto ou nao esta vazia. Retorna a raiz, ou vazio com `err`
// preenchido com uma chave em ingles (a janela traduz com T()).
inline fs::path createProjectFolder(const std::string& name, const std::string& author, const fs::path& location, std::string& err) {
  std::error_code ec;
  if (!safeName(name)) { err = "Invalid project name."; return {}; }
  if (author.size() > 100 || author.find_first_of("\r\n\t") != std::string::npos) { err = "Invalid author name."; return {}; }
  if (location.empty()) { err = "Choose a folder for the project."; return {}; }
  const fs::path root = fs::absolute(location, ec) / name;
  if (fs::exists(root, ec) && (!fs::is_directory(root, ec) || !fs::is_empty(root, ec))) { err = "This folder already exists and is not empty."; return {}; }
  fs::create_directories(root / "Assets" / "Scripts", ec);
  fs::create_directories(root / "Scenes", ec);
  if (ec) { err = "Could not create the folder."; return {}; }
  Project p; p.root = root;
  ProjectSettings ps;
  ps.projectName = name; ps.author = author;
  if (!ps.save(p)) { err = "Could not write project.json."; return {}; }
  return root;
}

// Abre uma pasta existente como projeto. Se nao tiver project.json, cria um (nome = nome da pasta).
inline fs::path openProjectFolder(const fs::path& folder, std::string& err) {
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) { err = "Folder not found."; return {}; }
  const fs::path root = fs::absolute(folder, ec);
  Project p; p.root = root;
  if (!fs::exists(root / "project.json", ec)) {
    ProjectSettings ps; ps.projectName = root.filename().string().substr(0, 100);
    if (!ps.save(p)) { err = "Could not write project.json."; return {}; }
  }
  p.ensure();
  return root;
}

// Renomeia uma tag nos ARQUIVOS de todas as outras cenas (a cena aberta e salva normalmente pelo editor).
// Retorna quantos arquivos foram alterados.
inline int renameTagInSceneFiles(const Project& proj, const std::string& skipScene, const std::string& from, const std::string& to) {
  int changed = 0;
  for (auto& name : proj.listScenes()) {
    if (name == skipScene) continue;
    std::string text; if (!readTextFile(proj.scenePath(name), text)) continue;
    auto j = tool::Json::parse(text); if (!j || !j->isObject()) continue;
    bool dirty = false;
    tool::Json ents = tool::Json::array();
    for (auto& e : (*j)["entities"].items()) {
      tool::Json copy = e;
      if (e.isObject() && e.has("tags")) {
        tool::Json tags = tool::Json::array();
        for (auto& t : e["tags"].items()) { if (t.isString() && t.asString() == from) { tags.push(to); dirty = true; } else tags.push(t); }
        copy.set("tags", tags);
      }
      ents.push(copy);
    }
    if (dirty) { j->set("entities", ents); if (writeTextFile(proj.scenePath(name), j->dump())) ++changed; }
  }
  return changed;
}

} // namespace editor
