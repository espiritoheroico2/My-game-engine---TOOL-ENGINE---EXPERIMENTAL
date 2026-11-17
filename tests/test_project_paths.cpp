// Testes de F-12: resolucao de caminhos do projeto do editor, incluindo escape por symlink.
#include "test.hpp"
#include "../apps/editor/project_paths.hpp"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace editor::projpath;

namespace {

// Diretorio temporario proprio do teste, removido ao final (so o que o proprio teste criou).
struct TempTree {
  fs::path base;
  TempTree() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    base = fs::temp_directory_path() / ("tool_projpaths_" + std::to_string(stamp));
    fs::create_directories(base / "outside");
    fs::create_directories(base / "proj" / "Assets");
  }
  ~TempTree() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  fs::path proj() const { return base / "proj"; }
  fs::path outside() const { return base / "outside"; }
};

// Cria um link simbolico; devolve falso se o SO nao permitir (ex.: Windows sem privilegio).
bool makeLink(const fs::path& target, const fs::path& link) {
  std::error_code ec;
  fs::create_directory_symlink(target, link, ec);
  return !ec;
}

} // namespace

TEST(projpath_lexical_rejects_absolute_and_traversal) {
  CHECK(lexicalRelative("").empty());
  CHECK(lexicalRelative("/etc/passwd").empty());
  CHECK(lexicalRelative("../x").empty());
  CHECK(lexicalRelative("Assets/../../x").empty());
  CHECK(!lexicalRelative("Assets/nave.glb").empty());
  CHECK(lexicalRelative("./Assets/./x") == fs::path("Assets/x"));
}

TEST(projpath_resolves_normal_paths_inside_root) {
  TempTree t;
  const fs::path r = t.proj();
  CHECK(!resolveInside(r, "Assets/nave.glb").empty());   // arquivo ainda inexistente
  CHECK(!resolveInside(r, "Scenes/nova.scene.json").empty()); // pasta ainda inexistente
  CHECK(resolveInside(r, "../outside/x").empty());       // ".." lexical
  CHECK(resolveInside(r, "").empty());
}

TEST(projpath_symlink_dir_pointing_outside_is_rejected) {
  TempTree t;
  if (!makeLink(t.outside(), t.proj() / "Assets" / "ext")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(resolveInside(t.proj(), "Assets/ext/file.txt").empty());
  CHECK(resolveInside(t.proj(), "Assets/ext").empty());
}

TEST(projpath_dangling_symlink_pointing_outside_is_rejected) {
  TempTree t;
  // Alvo fora da raiz que NAO existe: weakly_canonical sozinho nao seguiria o link pendente.
  const fs::path missing = t.base / "outside" / "nao_existe";
  if (!makeLink(missing, t.proj() / "Assets" / "dangling")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(resolveInside(t.proj(), "Assets/dangling/novo.bin").empty());
  CHECK(resolveInside(t.proj(), "Assets/dangling").empty());
}

TEST(projpath_symlink_file_pointing_outside_is_rejected) {
  TempTree t;
  { std::ofstream(t.outside() / "segredo.txt") << "fora"; }
  if (!makeLink(t.outside() / "segredo.txt", t.proj() / "Assets" / "link.txt")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(resolveInside(t.proj(), "Assets/link.txt").empty());
}

TEST(projpath_relative_symlink_escape_is_rejected) {
  TempTree t;
  // Alvo relativo que sobe para fora: "../../outside" a partir de proj/Assets.
  if (!makeLink(fs::path("../../outside"), t.proj() / "Assets" / "up")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(resolveInside(t.proj(), "Assets/up/x").empty());
}

TEST(projpath_symlink_inside_root_is_accepted) {
  TempTree t;
  fs::create_directories(t.proj() / "Assets" / "real");
  if (!makeLink(t.proj() / "Assets" / "real", t.proj() / "Assets" / "alias")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(!resolveInside(t.proj(), "Assets/alias/x.glb").empty());
}

TEST(projpath_root_that_is_itself_a_symlink_is_accepted) {
  TempTree t;
  // A raiz do projeto pode ser um link para outra pasta (ex.: projeto em disco externo).
  if (!makeLink(t.proj(), t.base / "proj_link")) { std::printf("    (sem permissao para symlink: teste pulado)\n"); return; }
  CHECK(!resolveInside(t.base / "proj_link", "Assets/nave.glb").empty());
}
