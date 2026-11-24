// Teste dos sistemas de UI sem janela: pacotes de idioma e temas (escolha, escala e arquivo). Sem ImGui.
// Teste dos sistemas de UI do editor: pacotes de idioma e temas (sem abrir janela).
#include "i18n.hpp"
#include "theme.hpp"
#include "ui_style.hpp"

#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
  auto& i = editor::I18n::get();
  i.scan(std::string(TOOL_SOURCE_DIR) + "/assets/lang");
  CHECK(std::string(editor::T("Hierarchy")) == "Hierarchy"); // padrao: ingles, sem arquivo nenhum carregado
  // Idioma padrao: ingles; codigo salvo vazio ou inexistente cai nele (sem configuracao salva, a UI sai em ingles).
  CHECK(std::string(editor::kDefaultLanguage) == "en");
  CHECK(i.select("pt-BR") && !i.selectOrDefault("") && i.code() == "en");
  CHECK(std::string(editor::T("Hierarchy")) == "Hierarchy");
  CHECK(i.select("pt-BR") && !i.selectOrDefault("xx-inexistente") && i.code() == "en");
  CHECK(i.selectOrDefault("pt-BR") && i.code() == "pt-BR");
  CHECK(i.select(editor::kDefaultLanguage) && i.code() == "en");
  CHECK(i.packs().size() >= 3);
  CHECK(i.select("pt-BR"));
  CHECK(std::string(editor::T("Hierarchy")) == "Hierarquia");
  CHECK(std::string(editor::T("phrase that does not exist")) == "phrase that does not exist"); // fallback: devolve o texto-chave (ingles)
  CHECK(editor::F("live entities: %zu", size_t(7)) == "entidades vivas: 7");
  CHECK(editor::W("Hierarchy", "hier") == "Hierarquia###hier");
  CHECK(i.select("es") && std::string(editor::T("Settings")) == "Configuración");
  CHECK(i.select("en") && std::string(editor::T("Position")) == "Position"); // ingles = chave
  CHECK(i.select("pt-BR") && std::string(editor::T("Position")) == "Posi\xc3\xa7\xc3\xa3o");
  CHECK(!i.select("xx-inexistente"));
  const std::string tpl = std::string(TOOL_SOURCE_DIR) + "/out/ui_test_modelo.json";
  CHECK(i.exportTemplate(tpl));
  { // um pacote exportado e valido e recarregavel
    std::ifstream f(tpl); std::stringstream ss; ss << f.rdbuf();
    auto j = tool::Json::parse(ss.str());
    CHECK(j && (*j)["strings"].members().size() > 3);
  }

  editor::Theme th;
  th.setPreset(1); // Claro
  CHECK(th.current == "Light" && th.base.bg.x > 0.8f);
  th.base.accent = {0.9f, 0.1f, 0.1f, 1};
  th.scale = 1.5f;
  const std::string tf = std::string(TOOL_SOURCE_DIR) + "/out/ui_test_tema.json";
  CHECK(th.save(tf, "teste"));
  editor::Theme t2;
  t2.setPreset(0); // escuro
  CHECK(t2.load(tf));
  CHECK(t2.current == "teste");
  CHECK(std::abs(t2.base.accent.x - 0.9f) < 1e-3f);   // cor de destaque sobreviveu ao ciclo
  CHECK(std::abs(t2.scale - 1.5f) < 1e-3f);           // escala sobreviveu ao ciclo
  CHECK(!t2.load(std::string(TOOL_SOURCE_DIR) + "/out/nao_existe.json"));
  { // escala da interface: multiplica o texto; no padrao (125%) nao muda nada
    namespace us = editor::ui_style;
    us::textSetting() = 1.f; us::interfaceFactor() = 1.f; us::refreshScale();
    CHECK(std::abs(us::textScale() - 1.f) < 1e-6f);
    us::interfaceFactor() = 1.5f / editor::Theme::kDefaultScale; us::textSetting() = 1.2f; us::refreshScale();
    CHECK(std::abs(us::textScale() - 1.44f) < 1e-4f);
    us::textSetting() = 1.f; us::interfaceFactor() = 1.f; us::refreshScale();
  }
  std::printf(fails ? "ui_test: %d falha(s)\n" : "ui_test: OK\n", fails);
  return fails ? 1 : 0;
}
