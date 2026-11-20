#pragma once

// Janela Scenes: regras e estado, sem janela. Lista das cenas (em ordem de nome, a atual marcada), quais acoes valem
// para a cena selecionada, validacao do nome de uma cena nova ou renomeada, e a lista de assets da cena (so modelo ou
// som; sem repetir). A janela SpriteUI (scenes_spriteui.hpp) so desenha e reporta cliques; o host faz o trabalho.
// Testado como Editor.ScenesModel.

#include "project.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace editor::scenes_model {

struct Row {
  std::string name;
  bool current = false;   // cena aberta agora (aparece com "(open)")
};

// Lista das cenas: em ordem de nome; a cena atual vem marcada.
[[nodiscard]] inline std::vector<Row> Rows(std::vector<std::string> scenes, std::string_view current) {
  std::sort(scenes.begin(), scenes.end());
  std::vector<Row> out;
  out.reserve(scenes.size());
  for (std::string& n : scenes) {
    const bool isCurrent = n == current;
    out.push_back(Row{std::move(n), isCurrent});
  }
  return out;
}

// Acoes sobre a cena selecionada. Abrir e apagar nunca valem para a cena atual (ela e salva antes de trocar, e nao
// se apaga aberta). Renomear vale para qualquer selecionada: a atual e salva antes.
struct Actions {
  bool open = false;
  bool rename = false;
  bool remove = false;
};

[[nodiscard]] inline Actions ActionsFor(std::string_view selected, std::string_view current) {
  Actions a;
  if (selected.empty()) return a;
  const bool isCurrent = selected == current;
  a.open = !isCurrent;
  a.rename = true;
  a.remove = !isCurrent;
  return a;
}

// Nome de cena nova ou renomeada. Vazio quando vale; senao, a mensagem do problema (em ingles, como as outras).
[[nodiscard]] inline std::string NameProblem(std::string_view name, const std::vector<std::string>& scenes) {
  const std::string n(name);
  if (!safeName(n)) return "Invalid name (no / \\ : * ? \" < > |).";
  for (const std::string& s : scenes) {
    if (s == n) return "A scene with this name already exists";
  }
  return {};
}

// Atribuir um asset a cena: so modelo ou som, e nao repetir o que ja esta.
[[nodiscard]] inline bool Assign(std::vector<std::string>& assets, const std::string& rel, bool modelOrSound) {
  if (!modelOrSound) return false;
  if (std::find(assets.begin(), assets.end(), rel) != assets.end()) return false;
  assets.push_back(rel);
  return true;
}

// Tirar um asset da lista pelo indice. Indice fora da lista nao muda nada.
inline bool RemoveAt(std::vector<std::string>& assets, std::size_t index) {
  if (index >= assets.size()) return false;
  assets.erase(assets.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

// Estado da janela: listas, cena selecionada, e o campo de nome (criar ou renomear).
enum class NameMode : std::uint8_t { None, New, Rename };

struct Model {
  std::vector<std::string> scenes;
  std::string current;
  std::string selected;
  std::vector<std::string> assets;
  NameMode mode = NameMode::None;
  std::string name;
  bool nameFocus = false;
  std::string status;   // ultimo problema mostrado (vazio quando nao ha)
};

// Abre o campo de nome. Renomear preenche com o nome atual da selecao; criar comeca vazio.
inline void BeginName(Model& m, NameMode mode) {
  m.mode = mode;
  m.name = mode == NameMode::Rename ? m.selected : std::string{};
  m.nameFocus = true;
  m.status.clear();
}

// Fecha o campo de nome (depois de criar, renomear ou cancelar).
inline void FinishName(Model& m) {
  m.mode = NameMode::None;
  m.name.clear();
  m.nameFocus = false;
}

// Apaga o ultimo caractere UTF-8 (inteiro, nao so um byte).
inline void PopUtf8(std::string& s) {
  if (s.empty()) return;
  std::size_t i = s.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  s.erase(i);
}

}  // namespace editor::scenes_model
