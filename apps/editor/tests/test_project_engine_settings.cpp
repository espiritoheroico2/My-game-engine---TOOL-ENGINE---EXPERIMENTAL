// Bloco "engine" do project.json: padroes, ida e volta, faixas e projeto antigo. Roda no ctest como
// Editor.ProjectEngineSettings. Nao usa SDL nem ImGui.
#include "project_engine_settings.hpp"

#include <cstdio>

namespace {

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] %s\n", expr);
  }
}

}  // namespace

int main() {
  using editor::EngineSettings;

  // Padroes: 125 por cento de escala, ingles, render automatico.
  EngineSettings def;
  Check(def.uiScale == 1.25f, "padrao uiScale = 1.25");
  Check(def.language == "en", "padrao idioma = en");
  Check(def.uiRenderer == "auto", "padrao uiRenderer = auto");

  // Ida e volta pelo texto JSON (o que vai para o arquivo).
  EngineSettings custom;
  custom.language = "pt";
  custom.uiScale = 1.5f;
  custom.uiRenderer = "opengl";
  custom.graphicsApi = 2;
  custom.renderScale = 0.75f;
  custom.fpsMode = 1;
  const auto parsed = tool::Json::parse(custom.toJson().dump());
  Check(parsed.has_value(), "JSON gerado e valido");
  if (parsed) Check(EngineSettings::fromJson(*parsed) == custom, "ida e volta preserva todos os campos");

  // Projeto antigo (sem bloco): padroes.
  Check(EngineSettings::fromJson(tool::Json::object()) == def, "sem bloco = padroes");

  // Valores fora da faixa sao limitados; renderer desconhecido vira auto.
  tool::Json bad = tool::Json::object();
  bad.set("uiScale", 9.0);
  bad.set("renderScale", -3.0);
  bad.set("graphicsApi", 42);
  bad.set("fpsMode", -1);
  bad.set("uiRenderer", "vulkan-escondido");
  const EngineSettings clamped = EngineSettings::fromJson(bad);
  Check(clamped.uiScale == 2.0f, "uiScale limitado a 2.0");
  Check(clamped.renderScale == 0.25f, "renderScale limitado a 0.25");
  Check(clamped.graphicsApi == 4, "graphicsApi limitado a 4");
  Check(clamped.fpsMode == 0, "fpsMode limitado a 0");
  Check(clamped.uiRenderer == "auto", "renderer desconhecido vira auto");

  std::printf("%d checks, %d falhas\n", run, failed);
  return failed == 0 ? 0 : 1;
}
