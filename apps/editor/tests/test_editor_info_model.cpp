// Janela Editor info (SpriteUI): modelo sem janela. Regras da janela ImGui antiga, sem perder nenhuma.
#include "editor_info_model.hpp"

#include <cstdio>
#include <string>

namespace {

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

using namespace editor::info;
using editor::ProjectSettings;

ProjectSettings complete() {
  ProjectSettings s;
  s.projectName = "Meu Jogo";
  s.author = "Joao";
  s.info.studio = "Estudio";
  s.info.country = "Brasil";
  s.info.version = "1.0.0";
  s.info.description = "Um jogo de teste";
  return s;
}

}  // namespace

int main() {
  // 1) Carrega do projeto; nada muda sem applyTo.
  {
    ProjectSettings s = complete();
    s.info.city = "Sao Paulo";
    Model m{s};
    CHECK(m.text(Field::Product) == "Meu Jogo");
    CHECK(m.text(Field::Creator) == "Joao");
    CHECK(m.text(Field::City) == "Sao Paulo");
    CHECK(m.missing().empty());
    m.Focus(Field::Product);
    m.SelectAll();
    m.InsertText("Outro");
    CHECK(s.projectName == "Meu Jogo");          // rascunho nao toca o projeto
  }

  // 2) applyTo grava o rascunho inteiro (o que "Save info" faz).
  {
    ProjectSettings s = complete();
    Model m{s};
    m.Focus(Field::Product);
    m.SelectAll();
    m.InsertText("Novo Nome");
    m.Focus(Field::Website);
    m.InsertText("https://exemplo.test");
    ProjectSettings out;
    m.applyTo(out);
    CHECK(out.projectName == "Novo Nome");
    CHECK(out.info.website == "https://exemplo.test");
    CHECK(out.info.studio == "Estudio");
  }

  // 3) Obrigatorios: mesmas chaves do ProjectSettings::missingInfo.
  {
    ProjectSettings s;   // tudo vazio
    Model m{s};
    const auto miss = m.missing();
    CHECK(miss.size() == 5);                       // produto, criador, estudio, pais e descricao (versao tem padrao)
    bool hasProduct = false, hasDescription = false;
    for (const auto& k : miss) {
      hasProduct = hasProduct || k == "Product name";
      hasDescription = hasDescription || k == "Description";
    }
    CHECK(hasProduct);
    CHECK(hasDescription);
  }

  // 4) Versao invalida conta como faltando.
  {
    ProjectSettings s = complete();
    s.info.version = "1.x";
    Model m{s};
    bool hasVersion = false;
    for (const auto& k : m.missing()) hasVersion = hasVersion || k == "Version";
    CHECK(hasVersion);
  }

  // 5) Copyright vazio nao e obrigatorio; o automatico usa o estudio.
  {
    ProjectSettings s = complete();
    Model m{s};
    CHECK(m.missing().empty());
    CHECK(m.copyrightText(2026) == "Copyright (c) 2026 Estudio");
    m.Focus(Field::Copyright);
    m.InsertText("(c) Dono");
    CHECK(m.copyrightText(2026) == "(c) Dono");
  }

  // 6) Limite de tamanho: a versao aceita ate 32 bytes; texto alem disso e ignorado.
  {
    ProjectSettings s = complete();
    Model m{s};
    m.Focus(Field::Version);
    m.SelectAll();
    m.InsertText(std::string(40, '9'));
    CHECK(m.text(Field::Version) == "1.0.0");      // 40 bytes nao cabem em 32; o texto antigo continua
    m.SelectAll();
    m.Press(Key::Backspace, false);                // apaga a selecao antes de digitar o valor de 32 bytes
    m.InsertText(std::string(32, '9'));
    CHECK(m.text(Field::Version).size() == 32);    // exatamente o limite cabe
  }

  // 7) Tab percorre os nove campos na ordem do formulario e volta ao inicio.
  {
    ProjectSettings s = complete();
    Model m{s};
    m.Focus(Field::Product);
    m.Press(Key::Tab, false);
    CHECK(m.focusField() == Field::Version);
    for (int i = 0; i < 7; ++i) m.Press(Key::Tab, false);   // Version -> Copyright (8 passos desde Product)
    CHECK(m.focusField() == Field::Copyright);
    m.Press(Key::Tab, false);
    CHECK(m.focusField() == Field::Product);
    m.Press(Key::Tab, true);                       // Shift+Tab volta
    CHECK(m.focusField() == Field::Copyright);
  }

  // 8) Sem foco, digitar nao muda nada; Escape tira o foco.
  {
    ProjectSettings s = complete();
    Model m{s};
    m.InsertText("x");
    CHECK(m.text(Field::Product) == "Meu Jogo");
    m.Focus(Field::Product);
    CHECK(m.hasFocus(Field::Product));
    m.Press(Key::Escape, false);
    CHECK(!m.focused());
  }

  // 9) Resultado do salvar fica no modelo para a tela mostrar.
  {
    ProjectSettings s = complete();
    Model m{s};
    m.setSaveResult(true, "Saved.");
    CHECK(m.saved());
    CHECK(m.status() == "Saved.");
  }

  std::printf("editor info model: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
