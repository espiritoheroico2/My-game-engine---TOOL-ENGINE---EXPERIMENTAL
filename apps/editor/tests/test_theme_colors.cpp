// Testes de theme_colors.hpp: as cores de janelas e barras vem da paleta em uso, entao trocar o tema as muda.
// Sem janela e sem SpriteUI (so sui_palette.hpp e theme_colors.hpp).
#include "theme_colors.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;  // Quantidade de verificacoes que falharam.

// Imprime ok/FALHOU para a verificacao e conta as falhas.
void Check(bool ok, const std::string& what) {
  if (!ok) ++gFailures;
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what.c_str());
}

using editor::palette::Rgb;
namespace tc = editor::theme_colors;

// As duas cores diferem em algum canal (tolerancia 1e-4)?
bool Differ(const Rgb& a, const Rgb& b) {
  return std::fabs(a.r - b.r) > 1e-4f || std::fabs(a.g - b.g) > 1e-4f || std::fabs(a.b - b.b) > 1e-4f || std::fabs(a.a - b.a) > 1e-4f;
}

// Token de cor com nome, para percorrer todos os campos de Colors em laco.
struct Named {
  const char* name;
  Rgb tc::Colors::*member;
};

// Todo token que janelas e barras usam. O erro fica de fora na comparacao entre presets (varios usam o mesmo vermelho).
const Named kTokens[] = {
    {"panel", &tc::Colors::panel},       {"surface", &tc::Colors::surface},   {"text", &tc::Colors::text},
    {"textDim", &tc::Colors::textDim},   {"accent", &tc::Colors::accent},     {"border", &tc::Colors::border},
    {"scroll", &tc::Colors::scroll},     {"btn", &tc::Colors::btn},           {"btnHover", &tc::Colors::btnHover},
    {"btnPressed", &tc::Colors::btnPressed}, {"btnOn", &tc::Colors::btnOn},   {"field", &tc::Colors::field},
    {"fieldFocus", &tc::Colors::fieldFocus}, {"selection", &tc::Colors::selection}, {"rowHover", &tc::Colors::rowHover},
    {"rowSel", &tc::Colors::rowSel},     {"heading", &tc::Colors::heading},   {"headerActive", &tc::Colors::headerActive},
    {"headerInactive", &tc::Colors::headerInactive}, {"headerDragging", &tc::Colors::headerDragging},
    {"borderDragging", &tc::Colors::borderDragging},
};

}  // namespace

// Roda os testes: troca de tema muda cada token, assinatura muda com a paleta, Custom reage a cor-base e tokens ficam em 0..1.
int main() {
  // Escuro azul x Claro: todo token usado por janelas e barras muda (incluindo o erro).
  const tc::Colors dark = tc::From(editor::palette::kPresets[0]);
  const tc::Colors light = tc::From(editor::palette::kPresets[2]);
  for (const Named& n : kTokens) Check(Differ(dark.*(n.member), light.*(n.member)), std::string("Dark blue x Light muda: ") + n.name);
  Check(Differ(dark.error, light.error), "Dark blue x Light muda: error");

  // Qualquer par de presets: o fundo, a superficie, o texto e o cabecalho da moldura mudam.
  for (std::size_t i = 0; i < editor::palette::kCount; ++i) {
    for (std::size_t j = i + 1; j < editor::palette::kCount; ++j) {
      const tc::Colors a = tc::From(editor::palette::kPresets[i]);
      const tc::Colors b = tc::From(editor::palette::kPresets[j]);
      const std::string pair = std::to_string(i) + " x " + std::to_string(j);
      Check(Differ(a.panel, b.panel), "painel difere (" + pair + ")");
      Check(Differ(a.headerActive, b.headerActive), "cabecalho difere (" + pair + ")");
      Check(Differ(a.field, b.field), "campo difere (" + pair + ")");
      Check(Differ(a.btn, b.btn), "botao difere (" + pair + ")");
      Check(tc::Signature(editor::palette::kPresets[i]) != tc::Signature(editor::palette::kPresets[j]), "assinatura difere (" + pair + ")");
    }
  }

  // Active() segue palette::active(): trocar o indice troca as cores, e voltar restaura.
  editor::palette::activeIndex() = 0;
  const tc::Colors a0 = tc::Active();
  const unsigned long long s0 = tc::Signature();
  editor::palette::activeIndex() = 2;
  const tc::Colors a2 = tc::Active();
  const unsigned long long s2 = tc::Signature();
  editor::palette::activeIndex() = 0;
  Check(Differ(a0.panel, a2.panel) && Differ(a0.headerActive, a2.headerActive) && Differ(a0.rowHover, a2.rowHover), "Active() depende do indice ativo");
  Check(s0 != s2, "Signature() muda com o tema");
  Check(tc::Signature() == s0 && !Differ(tc::Active().panel, a0.panel), "voltar ao tema restaura as cores");

  // Paleta Custom: mudar uma cor-base muda as cores derivadas e a assinatura.
  editor::palette::activeIndex() = editor::palette::kCustomIndex;
  const unsigned long long c0 = tc::Signature();
  const Rgb before = tc::Active().panel;
  editor::palette::custom().base[0] = Rgb{0.9f, 0.2f, 0.2f};
  editor::palette::deriveTokens(editor::palette::custom());
  Check(Differ(before, tc::Active().panel) && tc::Signature() != c0, "Custom: cor-base nova muda painel e assinatura");
  editor::palette::activeIndex() = 0;

  // Sem tokens fora de 0..1 (cor invalida pintaria errado).
  bool range = true;
  for (std::size_t i = 0; i < editor::palette::kCount; ++i) {
    const tc::Colors c = tc::From(editor::palette::kPresets[i]);
    for (const Named& n : kTokens) {
      const Rgb& v = c.*(n.member);
      if (v.r < 0.f || v.r > 1.f || v.g < 0.f || v.g > 1.f || v.b < 0.f || v.b > 1.f || v.a < 0.f || v.a > 1.f) range = false;
    }
  }
  Check(range, "todos os tokens ficam em 0..1");

  std::printf("%s (%d falhas)\n", gFailures == 0 ? "OK" : "FALHOU", gFailures);
  return gFailures == 0 ? 0 : 1;
}
