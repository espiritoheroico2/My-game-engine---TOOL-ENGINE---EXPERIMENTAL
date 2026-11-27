#pragma once

// Janela Scenes: operacoes em arquivo (renomear e apagar a cena na pasta Scenes do projeto), sem Editor e sem janela.
// O host (main.cpp) chama estas funcoes; os testes (test_scenes_view.cpp) as usam em um projeto temporario.

#include "project.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace editor::scenes_ops {

// Renomeia o arquivo da cena. Falha (com `err`) se o nome e invalido, o destino ja existe ou a origem nao existe.
inline bool RenameSceneFile(const Project& proj, const std::string& from, const std::string& to, std::string& err) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!safeName(to)) { err = "Invalid name (no / \\ : * ? \" < > |)."; return false; }
  if (from == to) return true;
  if (!fs::exists(proj.scenePath(from), ec)) { err = "Scene file not found: " + from; return false; }
  if (fs::exists(proj.scenePath(to), ec)) { err = "A scene with this name already exists"; return false; }
  fs::rename(proj.scenePath(from), proj.scenePath(to), ec);
  if (ec) { err = ec.message(); return false; }
  return true;
}

// Apaga o arquivo da cena. Falha (com `err`) se ele nao existe ou nao pode ser removido.
inline bool DeleteSceneFile(const Project& proj, const std::string& name, std::string& err) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(proj.scenePath(name), ec)) { err = "Scene file not found: " + name; return false; }
  fs::remove(proj.scenePath(name), ec);
  if (ec) { err = ec.message(); return false; }
  return true;
}

}  // namespace editor::scenes_ops
