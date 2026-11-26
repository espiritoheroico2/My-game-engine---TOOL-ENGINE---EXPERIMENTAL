#include "test.hpp"
#include "tool/canvas/canvas_layout.hpp"

using namespace tool::canvas;

namespace {
bool Near(float a, float b) { return a > b - 1e-3f && a < b + 1e-3f; }
}  // namespace

TEST(canvas_follow_keeps_size_and_moves_by_anchor) {
  Layout l;
  l.anchorX = 0.5f;
  l.anchorY = 0.5f;
  l.offsetX = 10.f;
  l.width = 200.f;
  l.height = 100.f;
  l.resize = Resize::Follow;
  Rect r = Compute(l, {1000.f, 800.f}, {1000.f, 800.f});
  CHECK(Near(r.x, 510.f) && Near(r.y, 400.f));
  CHECK(Near(r.w, 200.f) && Near(r.h, 100.f));
  r = Compute(l, {2000.f, 1600.f}, {1000.f, 800.f});   // Follow: tamanho fixo, so a posicao anda
  CHECK(Near(r.x, 1010.f) && Near(r.y, 800.f));
  CHECK(Near(r.w, 200.f) && Near(r.h, 100.f));
}

TEST(canvas_scale_grows_and_shrinks_with_screen) {
  Layout l;
  l.width = 200.f;
  l.height = 100.f;
  l.offsetX = 20.f;
  l.resize = Resize::Scale;
  Rect r = Compute(l, {2000.f, 1600.f}, {1000.f, 800.f});   // cresce 2x
  CHECK(Near(r.w, 400.f) && Near(r.h, 200.f));
  CHECK(Near(r.x, 1040.f));       // ancora 0.5 * 2000 = 1000, mais o deslocamento 20 * 2 = 40
  r = Compute(l, {500.f, 400.f}, {1000.f, 800.f});          // encolhe 2x
  CHECK(Near(r.w, 100.f) && Near(r.h, 50.f));
}

TEST(canvas_zero_reference_does_not_divide) {
  Layout l;
  l.resize = Resize::Scale;
  l.width = 300.f;
  Rect r = Compute(l, {800.f, 600.f}, {0.f, 0.f});   // referencia invalida: fator 1
  CHECK(Near(r.w, 300.f));
}

TEST(canvas_refresh_recomputes_only_on_change) {
  Cache c;
  Layout l;
  Screen s{1000.f, 800.f};
  CHECK(Refresh(c, l, s, s));                     // primeira vez: calcula
  CHECK(!Refresh(c, l, s, s));                    // parado: nao calcula
  s.w = 1200.f;
  CHECK(Refresh(c, l, s, {1000.f, 800.f}));       // tela mudou
  CHECK(!Refresh(c, l, s, {1000.f, 800.f}));
  l.anchorX = 0.25f;
  CHECK(Refresh(c, l, s, {1000.f, 800.f}));       // layout mudou
  CHECK(!Refresh(c, l, s, {1000.f, 800.f}));
}

TEST(canvas_default_background_is_transparent) {
  Style st;
  CHECK((st.background >> 24) == 0u);   // alfa zero: so os filhos aparecem
  CHECK(st.borderWidth == 0.f);
}
