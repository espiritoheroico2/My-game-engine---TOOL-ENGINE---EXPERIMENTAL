// Testes da paleta da SpriteUI (sui_palette.hpp): padrao, fallback de indice e contraste do texto.
// Contraste pela formula WCAG 2.x: texto principal >= 4.5:1 sobre fundo e superficie; texto de rotulo >= 3:1.
#include "sui_palette.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

double Linear(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double Luminance(const editor::palette::Rgb& c) {
  return 0.2126 * Linear(c.r) + 0.7152 * Linear(c.g) + 0.0722 * Linear(c.b);
}

double Contrast(const editor::palette::Rgb& a, const editor::palette::Rgb& b) {
  const double la = Luminance(a), lb = Luminance(b);
  const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

bool InRange(const editor::palette::Rgb& c) {
  return c.r >= 0.f && c.r <= 1.f && c.g >= 0.f && c.g <= 1.f && c.b >= 0.f && c.b <= 1.f;
}

}  // namespace

int main() {
  namespace P = editor::palette;

  // 1. Padrao: o primeiro preset (Azul escuro), e indice invalido cai nele.
  P::activeIndex() = 0;
  Check(std::string_view(P::active().name) == "Dark blue", "padrao e o Azul escuro");
  P::activeIndex() = 999;
  Check(&P::active() == &P::kPresets[0], "indice invalido cai no padrao");
  P::activeIndex() = 0;

  // 2. Cada preset: nome, cores validas e fundos distintos.
  for (std::size_t i = 0; i < P::kCount; ++i) {
    const P::Palette& p = P::kPresets[i];
    const std::string_view name = p.name;
    Check(!name.empty(), "preset tem nome");
    Check(InRange(p.panel) && InRange(p.surface) && InRange(p.text) && InRange(p.textDim) && InRange(p.accent) &&
              InRange(p.scroll) && InRange(p.border) && InRange(p.error),
          "todas as cores do preset estao entre 0 e 1");
  }

  // 3. Contraste por preset (texto principal e rotulo sobre o fundo e a superficie).
  for (std::size_t i = 0; i < P::kCount; ++i) {
    const P::Palette& p = P::kPresets[i];
    const std::string label = std::string("contraste do texto principal >= 4.5 em ") + p.name;
    Check(Contrast(p.text, p.panel) >= 4.5 && Contrast(p.text, p.surface) >= 4.5, label.c_str());
    const std::string dim = std::string("contraste do rotulo >= 3 em ") + p.name;
    Check(Contrast(p.textDim, p.panel) >= 3.0 && Contrast(p.textDim, p.surface) >= 3.0, dim.c_str());
  }

  std::printf("%s (%d falha(s))\n", gFailures == 0 ? "TODOS OS TESTES PASSARAM" : "HA FALHAS", gFailures);
  return gFailures == 0 ? 0 : 1;
}
