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

// Pastas primeiro, depois por nome (sem diferenciar maiusculas).
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

// ---------- Estado da janela SpriteUI ----------
// O host (assets_sprite_panel.hpp) preenche items e le selected/folder; a view (assets_spriteui.hpp) so muda o que
// o usuario pede (pasta, filtro, selecao, modo de nome, texto) e reporta acoes.
struct State {
  enum class Mode : std::uint8_t { None, NewFolder, NewFile, Rename, Delete };
  std::string folder = std::string(kRoot);   // pasta atual, relativa ao projeto
  std::vector<Item> items;                    // conteudo da pasta atual (o host ordena com Sort)
  std::string filter;
  bool iconView = true;
  int scroll = 0;                             // primeira linha visivel (lista ou grade de icones; no texto, a linha)
  std::string selected;                       // rel do item selecionado
  Mode mode = Mode::None;                     // confirmacao em curso (criar, renomear, apagar)
  std::string name;                           // campo de nome (criar e renomear)
  bool nameFocus = false;
  bool filterFocus = false;
  std::string status;
  bool textOpen = false;                      // editor de texto no lugar da lista
  std::string textPath;                       // rel do arquivo em edicao
  std::string text;                           // conteudo em edicao (digitacao so no fim)
};

inline constexpr int kListRows = 14;   // linhas visiveis na lista (1 item por linha)
inline constexpr int kIconRows = 4;    // linhas visiveis na grade de icones
inline constexpr int kIconCols = 6;    // icones por linha

inline int Cols(bool icon) { return icon ? kIconCols : 1; }
inline int VisibleRows(bool icon) { return icon ? kIconRows : kListRows; }

// Indices (em State::items) dos itens que passam no filtro, na ordem da lista.
inline std::vector<std::size_t> Visible(const State& s) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < s.items.size(); ++i) {
    if (Matches(s.items[i].name, s.filter)) out.push_back(i);
  }
  return out;
}

// Quantas linhas podem sair da rolagem: total de linhas menos as visiveis.
inline int MaxScroll(std::size_t count, bool icon) {
  const std::size_t cols = static_cast<std::size_t>(Cols(icon));
  const int rows = static_cast<int>((count + cols - 1) / cols);
  return std::max(0, rows - VisibleRows(icon));
}

inline bool NeedsName(State::Mode m) {
  return m == State::Mode::NewFolder || m == State::Mode::NewFile || m == State::Mode::Rename;
}

// Comeca uma confirmacao. Criar e renomear abrem o campo de nome; apagar so pede confirmacao.
inline void Begin(State& s, State::Mode m, std::string name = {}) {
  s.mode = m;
  s.name = std::move(name);
  s.nameFocus = NeedsName(m);
  s.filterFocus = false;
  s.status.clear();
}

inline void Finish(State& s) {
  s.mode = State::Mode::None;
  s.name.clear();
  s.nameFocus = false;
  s.status.clear();
}

// Nome de arquivo depois de "/" (o que o campo de renomear mostra).
inline std::string BaseName(std::string_view rel) {
  const std::size_t slash = rel.rfind('/');
  return std::string(slash == std::string_view::npos ? rel : rel.substr(slash + 1));
}

// Motivo para recusar o nome (vazio = aceito). Renomear ignora o proprio item.
inline std::string NameProblemFor(const State& s, const std::string& name) {
  if (!NameValid(name)) return "Invalid name (no / \\ : * ? \" < > |).";
  for (const Item& it : s.items) {
    if (s.mode == State::Mode::Rename && it.rel == s.selected) continue;
    if (it.name == name) return "A file with that name already exists.";
  }
  return "";
}

// Digitacao: campo de filtro, campo de nome ou o texto aberto (no fim). O filtro volta a lista ao topo.
inline void Type(State& s, const std::string& utf8) {
  if (s.textOpen) {
    s.text += utf8;
  } else if (s.filterFocus) {
    s.filter += utf8;
    s.scroll = 0;
  } else if (s.nameFocus && NeedsName(s.mode)) {
    s.name += utf8;
  }
}

inline void PopUtf8Back(std::string& s) {
  if (s.empty()) return;
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
  if (!s.empty()) s.pop_back();
}

inline void Backspace(State& s) {
  if (s.textOpen) {
    PopUtf8Back(s.text);
  } else if (s.filterFocus) {
    PopUtf8Back(s.filter);
    s.scroll = 0;
  } else if (s.nameFocus && NeedsName(s.mode)) {
    PopUtf8Back(s.name);
  }
}

// Linhas do texto aberto (separadas por \n; uma linha vazia no fim conta).
inline int TextLines(const std::string& text) {
  return 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

}  // namespace editor::assets_model
