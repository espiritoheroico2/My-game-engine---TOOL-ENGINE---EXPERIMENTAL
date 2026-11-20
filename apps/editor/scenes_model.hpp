#pragma once

// Janela Scenes: regras sem janela. Lista das cenas (em ordem de nome, a atual marcada), quais acoes valem para a
// cena selecionada, validacao do nome de uma cena nova ou renomeada, e a lista de assets da cena (so modelo ou
// som; sem repetir). A janela SpriteUI (scenes_panel.hpp) so chama estas funcoes. Testado como Editor.ScenesModel.

#include "project.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace editor::scenes_model {

struct Row {
  std::string name;
  bool current = false;   // cena aberta agora (aparece com *)
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

}  // namespace editor::scenes_model
