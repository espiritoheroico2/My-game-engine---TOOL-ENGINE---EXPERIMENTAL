#pragma once

// Janela Assets: regras sem janela. Navegacao por pastas (migalhas, subir), ordem e filtro da lista, o que cada tipo
// de item oferece (duplo clique e menu de contexto), validacao de nome, e o que acontece com a selecao e a pasta
// atual depois de renomear ou apagar. A janela SpriteUI (assets_spriteui.hpp) so desenha e reporta cliques; o host
// faz o trabalho no disco. Testado como Editor.AssetsModel.

#include "project.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace editor::assets_model {

inline constexpr std::string_view kRoot = "Assets";
inline constexpr std::string_view kDefaultFolderName = "New Folder";
inline constexpr std::string_view kDefaultFileName = "notes.txt";
inline constexpr std::string_view kSceneSuffix = ".scene.json";
inline constexpr std::uintmax_t kMaxTextBytes = 60000;   // arquivo maior nao abre no editor de texto
inline constexpr float kMinIconSize = 48.f;
inline constexpr float kMaxIconSize = 140.f;

// Um item da pasta atual, ja com o tipo resolvido pelo host.
struct Item {
  std::string name;
  std::string rel;        // caminho relativo ao projeto
  bool dir = false;
  AssetKind kind = AssetKind::Other;
  std::uintmax_t size = 0;
};

inline std::string Lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// Pastas primeiro, depois por nome (sem diferenciar maiusculas), como na lista ImGui.
inline void Sort(std::vector<Item>& items) {
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.dir != b.dir) return a.dir;
    return Lower(a.name) < Lower(b.name);
  });
}

// Filtro de busca: o nome contem o texto, sem diferenciar maiusculas. Vazio mostra tudo.
inline bool Matches(std::string_view name, std::string_view filter) {
  if (filter.empty()) return true;
  return Lower(name).find(Lower(filter)) != std::string::npos;
}

// Migalhas do caminho: "Assets/a/b" vira Assets, Assets/a, Assets/a/b.
struct Crumb {
  std::string label;
  std::string folder;
};
inline std::vector<Crumb> Crumbs(std::string_view folder) {
  std::vector<Crumb> out;
  std::string acc;
  std::size_t start = 0;
  while (start <= folder.size()) {
    std::size_t slash = folder.find('/', start);
    if (slash == std::string_view::npos) slash = folder.size();
    const std::string_view part = folder.substr(start, slash - start);
    if (!part.empty()) {
      if (!acc.empty()) acc += '/';
      acc += std::string(part);
      out.push_back(Crumb{std::string(part), acc});
    }
    start = slash + 1;
  }
  if (out.empty()) out.push_back(Crumb{std::string(kRoot), std::string(kRoot)});
  return out;
}

// "<" sobe um nivel; nunca sai de Assets.
inline bool CanGoUp(std::string_view folder) { return folder != kRoot && !folder.empty(); }
inline std::string Parent(std::string_view folder) {
  const std::size_t slash = folder.rfind('/');
  if (slash == std::string_view::npos) return std::string(kRoot);
  return std::string(folder.substr(0, slash));
}

// Duplo clique: o que acontece com o item (pasta entra, modelo e som vao para a cena, cena abre, texto edita).
enum class Open : std::uint8_t { None, EnterFolder, Instantiate, OpenScene, EditText };
inline Open DoubleClickAction(AssetKind k) {
  switch (k) {
    case AssetKind::Folder: return Open::EnterFolder;
    case AssetKind::Model:
    case AssetKind::Audio: return Open::Instantiate;
    case AssetKind::Scene: return Open::OpenScene;
    case AssetKind::Text: return Open::EditText;
    default: return Open::None;
  }
}

// Menu de contexto: o que cada tipo oferece. Renomear e apagar valem para qualquer item.
struct Menu {
  bool addToScene = false;      // modelo
  bool playPreview = false;     // som
  bool addAudioSource = false;  // som
  bool assignToScene = false;   // modelo e som
  bool edit = false;            // texto
  bool openScene = false;       // cena
  bool openFolder = false;      // pasta
  bool rename = true;
  bool remove = true;
};
inline Menu MenuFor(AssetKind k) {
  Menu m;
  switch (k) {
    case AssetKind::Model: m.addToScene = true; m.assignToScene = true; break;
    case AssetKind::Audio: m.playPreview = true; m.addAudioSource = true; m.assignToScene = true; break;
    case AssetKind::Text: m.edit = true; break;
    case AssetKind::Scene: m.openScene = true; break;
    case AssetKind::Folder: m.openFolder = true; break;
    default: break;
  }
  return m;
}

// Nome da cena a partir do arquivo "Main.scene.json" (vazio se nao for cena).
inline std::string SceneNameFrom(std::string_view fileName) {
  if (fileName.size() <= kSceneSuffix.size()) return {};
  if (fileName.substr(fileName.size() - kSceneSuffix.size()) != kSceneSuffix) return {};
  return std::string(fileName.substr(0, fileName.size() - kSceneSuffix.size()));
}

// Renomear ou criar: o nome tem que passar em safeName (o host confere se ja existe na pasta).
[[nodiscard]] inline bool NameValid(const std::string& name) { return safeName(name); }

// Apagar: a pasta Assets em si nao se apaga.
inline bool CanDelete(std::string_view rel) { return !rel.empty() && rel != kRoot; }

// Depois de renomear ou mover: caminho que estava em `from` (ou dentro dele) passa a estar em `to`.
inline std::string FollowRename(const std::string& path, std::string_view from, std::string_view to) {
  if (path == from) return std::string(to);
  if (path.size() > from.size() && path.compare(0, from.size(), from) == 0 && path[from.size()] == '/') {
    return std::string(to) + path.substr(from.size());
  }
  return path;
}

// Depois de apagar: a selecao sai se estava dentro do item apagado.
inline std::string AfterDelete(const std::string& selected, std::string_view deleted) {
  if (selected == deleted) return {};
  if (selected.size() > deleted.size() && selected.compare(0, deleted.size(), deleted) == 0 && selected[deleted.size()] == '/') {
    return {};
  }
  return selected;
}

// Texto que abre no editor: so ate o limite de bytes.
inline bool TextEditable(std::uintmax_t bytes) { return bytes <= kMaxTextBytes; }

inline float ClampIconSize(float s) { return std::clamp(s, kMinIconSize, kMaxIconSize); }

// Colunas do grid de icones para a largura disponivel (sempre pelo menos 1).
inline int Columns(float availWidth, float tile, float pad) {
  if (tile + pad <= 0.f) return 1;
  return std::max(1, static_cast<int>(availWidth / (tile + pad)));
}

}  // namespace editor::assets_model
