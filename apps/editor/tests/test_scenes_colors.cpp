// Cores da janela Scenes: tudo vem da paleta em uso (Dark blue e Light), nada de branco fixo. Roda como Editor.ScenesColors.
#include "scenes_spriteui.hpp"

#include <cstdio>

namespace {
int gFailures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}
bool Same(const ToolEngine::SpriteUi::Vec4& a, const ToolEngine::SpriteUi::Vec4& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
bool PureWhite(const ToolEngine::SpriteUi::Vec4& a) { return a.x >= 0.999f && a.y >= 0.999f && a.z >= 0.999f && a.w >= 0.999f; }
}  // namespace

int main() {
  namespace P = editor::palette;
  const std::size_t saved = P::activeIndex();
  const std::size_t themes[2] = {0, 2};   // Dark blue, Light
  for (const std::size_t idx : themes) {
    P::activeIndex() = idx;
    const P::Palette p = P::active();
    const auto c = editor::scenesui::PaletteColors();
    std::printf("tema %s\n", p.name);
    Check(Same(c.field, editor::sui::PaletteColor(p.surface)), "campo/linha = surface da paleta");
    Check(Same(c.text, editor::sui::PaletteColor(p.text)), "texto = text da paleta");
    Check(Same(c.textDim, editor::sui::PaletteColor(p.textDim)), "texto apagado = textDim da paleta");
    Check(Same(c.warning, editor::sui::PaletteColor(p.error)), "aviso/X = error da paleta");
    Check(Same(c.help, editor::sui::PaletteColor(p.accent)), "destaque = accent da paleta");
    Check(!PureWhite(c.button) && !PureWhite(c.buttonHover) && !PureWhite(c.buttonDanger) && !PureWhite(c.disabled),
          "botoes sem branco fixo");
    Check(!PureWhite(c.text) || idx == 99, "texto nao e branco puro");
    Check(editor::scenesui::ClearTint().w == 0.f, "grupos e rotulos transparentes (nao branco padrao)");
  }
  P::activeIndex() = saved;
  std::printf("%d falhas\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
