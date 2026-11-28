// Teste do atributo //[visible] (script_vars.hpp): leitura das declaracoes, tipos, sincronizacao com o Inspector,
// troca do literal no texto do script e ida e volta no JSON da cena. Sem janela. Roda como Editor.ScriptVars.
#include "script_vars.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0;
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FALHOU: %s\n", what); ++g_fail; }
}

int main() {
  using namespace editor;
  const std::string src =
    "// cabecalho\n"
    "//[visible]\n"
    "let speed = 3;\n"
    "// [visible]\n"
    "const title = \"Hello\";   // comentario\n"
    "//[visible]\n"
    "var active = true;\n"
    "//[visible]\n"
    "let tint = \"#FF8000\";\n"
    "let hidden = 7;\n"
    "//[visible]\n"
    "let calc = 3 + 4;\n"
    "//[visible]\n"
    "\n"
    "let afterBlank = -1.5e1;\n";
  const auto d = parseVisibleVars(src);
  check(d.size() == 5, "five declarations are read (the 3 + 4 expression and the unmarked one are ignored)");
  check(d.size() >= 5 && d[0].name == "speed" && d[0].type == ScriptVarType::Number && d[0].literal == "3", "number variable");
  check(d.size() >= 5 && d[1].name == "title" && d[1].type == ScriptVarType::Text, "text variable (with '// [visible]' spacing)");
  check(d.size() >= 5 && d[2].name == "active" && d[2].type == ScriptVarType::Bool, "bool variable");
  check(d.size() >= 5 && d[3].name == "tint" && d[3].type == ScriptVarType::Color, "color variable (#RRGGBB text)");
  check(d.size() >= 5 && d[4].name == "afterBlank" && d[4].literal == "-1.5e1", "a blank line between the directive and the declaration is allowed");

  // Sincroniza com um objeto novo: valores vem do codigo.
  ScriptVars sv;
  check(syncScriptVars(sv, d) && sv.items.size() == 5, "first sync fills the list");
  check(sv.items[0].num == 3.f && std::string(sv.items[1].text) == "Hello" && sv.items[2].flag && std::fabs(sv.items[3].rgb[0] - 1.f) < 1e-3f, "values come from the script");
  check(!syncScriptVars(sv, d), "second sync with the same script changes nothing");

  // O usuario muda os valores no Inspector.
  sv.items[0].num = 12.5f; std::snprintf(sv.items[1].text, sizeof sv.items[1].text, "%s", "Say \"hi\""); sv.items[2].flag = false;
  sv.items[3].rgb[0] = 0.f; sv.items[3].rgb[1] = 0.f; sv.items[3].rgb[2] = 1.f;
  const std::string out = applyScriptVars(src, sv);
  check(out.find("let speed = 12.5;") != std::string::npos, "number from the Inspector reaches the script");
  check(out.find("const title = \"Say \\\"hi\\\"\";   // comentario") != std::string::npos, "text is escaped and the trailing comment stays");
  check(out.find("var active = false;") != std::string::npos, "bool from the Inspector reaches the script");
  check(out.find("let tint = \"#0000FF\";") != std::string::npos, "color from the Inspector reaches the script");
  check(out.find("let hidden = 7;") != std::string::npos && out.find("let calc = 3 + 4;") != std::string::npos, "other lines are untouched");
  const auto d2 = parseVisibleVars(out);
  check(d2.size() == 5 && d2[0].literal == "12.5", "the rewritten script is read back with the new value");

  // Mudou o codigo: o valor do codigo vence o do Inspector; variavel nova entra; variavel removida sai.
  std::string edited = src; edited.replace(edited.find("let speed = 3;"), 14, "let speed = 9;");
  check(syncScriptVars(sv, parseVisibleVars(edited)), "editing the script literal updates the list");
  check(sv.items[0].num == 9.f, "the literal edited in the code wins over the Inspector value");
  check(std::string(sv.items[1].text) == "Say \"hi\"", "an unchanged variable keeps the Inspector value");

  // JSON da cena: ida e volta (usa o mesmo World do editor).
  tool::Scene scene; tool::World& w = scene.world();
  tool::Entity e = w.create();
  w.add<Name>(e, Name{}); w.add<ScriptVars>(e, sv);
  tool::Entity c = loadEntity(scene, saveEntity(scene, e));
  const ScriptVars* back = w.get<ScriptVars>(c);
  check(back && back->items.size() == sv.items.size() && back->items[0].num == 9.f && std::string(back->items[1].text) == "Say \"hi\"", "[visible] values survive save and load of the scene");
  w.add<EntityManagerTag>(e, EntityManagerTag{});
  tool::Entity c2 = loadEntity(scene, saveEntity(scene, e));
  check(w.has<EntityManagerTag>(c2), "the Entity Manager mark survives save and load of the scene");

  if (g_fail == 0) std::printf("OK: script_vars\n");
  return g_fail == 0 ? 0 : 1;
}
