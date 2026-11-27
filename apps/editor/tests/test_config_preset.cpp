// Presets de configuracao (config_preset.hpp): salvar, listar, carregar, apagar e resolver o preset da cena. Sem janela.
// Roda no ctest como Editor.ConfigPreset. Usa uma pasta temporaria do sistema, nunca o projeto real.
#include "config_preset.hpp"

#include <cstdio>

namespace {
int run = 0, failed = 0;
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) { ++failed; std::fprintf(stderr, "[FAIL] linha %d: %s\n", line, expr); }
}
#define CHECK(e) Check((e), #e, __LINE__)
}

int main() {
  namespace cp = editor::config_preset;
  namespace fs = std::filesystem;
  const fs::path tmp = fs::temp_directory_path() / "toolengine_config_preset_test";
  std::error_code ec; fs::remove_all(tmp, ec);
  const std::string dir = tmp.string();

  // Pasta e padrao: <projeto>/Configs e <projeto>/Configs/default.json.
  CHECK(fs::path(cp::folder(dir)).filename() == "Configs");
  CHECK(fs::path(cp::defaultPath(dir)).filename() == "default.json");
  CHECK(cp::list(dir).empty());   // pasta ainda nao existe

  // Nome sanitizado; vazio ou so de caracteres invalidos que somem = erro.
  CHECK(cp::sanitizeName("a/b:c*") == "a_b_c_");
  CHECK(cp::sanitizeName("  ..  ").empty());
  editor::SceneFile info;
  std::string err;
  CHECK(!cp::save(dir, "", info, err) && !err.empty());

  // Ida e volta: valores diferentes do padrao sobrevivem.
  info.render.frustumCull = false;
  info.render.drawDistance = 123.f;
  info.physics.enabled = false;
  info.physics.gravity[1] = -3.f;
  info.physics.solverIterations = 20;
  info.physics.stepHz = 120;
  err.clear();
  CHECK(cp::save(dir, "preset 1", info, err));
  CHECK(cp::save(dir, "alpha", editor::SceneFile{}, err));
  const auto items = cp::list(dir);
  CHECK(items.size() == 2 && items[0].name == "alpha" && items[1].name == "preset 1");

  editor::SceneFile back;
  CHECK(cp::load(items[1].path, back, err));
  CHECK(!back.render.frustumCull && back.render.drawDistance == 123.f);
  CHECK(!back.physics.enabled && back.physics.gravity[1] == -3.f && back.physics.solverIterations == 20 && back.physics.stepHz == 120);

  // Arquivo com so uma parte (sem "physics"): o que falta volta ao padrao; arquivo invalido nao mexe em info.
  CHECK(editor::writeTextFile(fs::path(dir) / "Configs" / "parcial.json", R"({"render":{"frustumCull":false}})"));
  editor::SceneFile part; part.physics.stepHz = 30;
  CHECK(cp::load((fs::path(dir) / "Configs" / "parcial.json").string(), part, err));
  CHECK(!part.render.frustumCull && part.physics.stepHz == 60 && part.physics.enabled);
  CHECK(editor::writeTextFile(fs::path(dir) / "Configs" / "ruim.json", "nao e json"));
  editor::SceneFile keep; keep.physics.stepHz = 90;
  CHECK(!cp::load((fs::path(dir) / "Configs" / "ruim.json").string(), keep, err) && keep.physics.stepHz == 90);
  CHECK(!cp::load((fs::path(dir) / "Configs" / "naoexiste.json").string(), keep, err));

  // resolveForScene: existe -> o caminho da cena; senao (vazio ou ausente) -> default.json.
  CHECK(cp::resolveForScene(dir, items[1].path) == items[1].path);
  CHECK(cp::resolveForScene(dir, "") == cp::defaultPath(dir));
  CHECK(cp::resolveForScene(dir, (fs::path(dir) / "Configs" / "sumiu.json").string()) == cp::defaultPath(dir));

  // Apagar: some da lista; apagar de novo ou um arquivo que nao e .json falha.
  CHECK(cp::remove(items[1].path, err));
  CHECK(!fs::exists(items[1].path));
  CHECK(!cp::remove(items[1].path, err));
  CHECK(!cp::remove((fs::path(dir) / "x.txt").string(), err));
  CHECK(cp::resolveForScene(dir, items[1].path) == cp::defaultPath(dir));   // preset apagado volta ao padrao

  fs::remove_all(tmp, ec);
  if (failed) { std::fprintf(stderr, "%d de %d verificacoes falharam\n", failed, run); return 1; }
  std::printf("config_preset: %d verificacoes ok\n", run);
  return 0;
}
