// ToolEngine Editor - configuracoes DO PROJETO (Project/project.json): ligacoes entre tags e preset de exportacao.
//
// TAGS PRE-DEFINIDAS: algumas tags tem significado para a engine ("Principal" = camera ativa do jogo, "player",
// "enemy"). Elas nao podem ser renomeadas. Para usar outro nome no seu projeto, crie sua tag (ex.: "jogador") e
// LIGUE-A a uma pre-definida em Settings > Tags: a engine passa a tratar "jogador" como "player".
//
// EXPORTACAO: o preset de plataforma ja e guardado, mas a funcao esta DESLIGADA (kExportEnabled = false): a UI
// mostra os campos bloqueados. Sera ligada quando existirem os exportadores (docs/NEXT_STEPS.md).
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
inline constexpr bool kExportEnabled = false; // desligado por enquanto (pedido do projeto): so guarda a preferencia

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
    return j;
  }
  void fromJson(const tool::Json& j) {
    *this = ProjectSettings{};
    size_t n = 0;
    for (auto& o : j["tagLinks"].items()) if (++n <= 64 && o.isObject()) link(o["alias"].asString(), o["target"].asString());
    ser::copyStr(externalEditor, sizeof externalEditor, j["externalEditor"].asString());
    projectName = j["name"].asString().substr(0, 100);
    author = j["author"].asString().substr(0, 100);
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
