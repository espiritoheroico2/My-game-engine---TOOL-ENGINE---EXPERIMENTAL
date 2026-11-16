#pragma once

// Janela Scene: regras que nao dependem de janela. Projeta pontos 3D na tela, monta a geometria do gizmo (mover,
// girar, escalar), descobre qual alca esta sob o mouse e calcula o novo Transform durante o arrasto. A janela
// SpriteUI (scene_view_panel.hpp) so desenha o que este modelo devolve e repassa o mouse. Sem ImGui e sem SpriteUI:
// roda no ctest como Editor.SceneViewModel.
//
// Exemplo: o gizmo de mover segue a Caixa azul. Se a Caixa esta em (2, 0, 0), a ponta do eixo X sai dela para a
// direita. Arrastar a ponta 40 px ao longo do eixo move a Caixa o equivalente em metros, e a Caixa continua embaixo
// do eixo (diferenca menor que 1 px).
//
// Como funciona:
//   Projection:  camera + retangulo da imagem -> pixel. Mesma conta que ViewXf (perspectiva).
//   Gizmo:       pontas dos eixos, anel de girar e cubinho de escala, com tamanho ~constante na tela.
//   hitTest:     alca sob o mouse (raio de captura de 9 px), ou nenhuma.
//   dragEdit:    novo Transform a partir do mouse inicial e atual. Nada aqui grava no objeto.

#include "tool/math/math.hpp"
#include "tool/render/soft_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace editor::scene_view_model {

struct Pixel {
  float x = 0.f, y = 0.f;
};

// Retangulo da imagem na janela, em pixels (origem no canto superior esquerdo).
struct Area {
  float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
};

// Projecao da camera para um retangulo da tela. Pontos atras da camera nao aparecem (point retorna false).
class Projection {
public:
  static Projection make(const tool::Camera& cam, Area area) {
    Projection p;
    p.area_ = area;
    p.vp_ = tool::Mat4::perspective(cam.fovY, area.w / area.h, cam.nearZ, cam.farZ) * tool::Mat4::lookAt(cam.eye, cam.target, cam.up);
    return p;
  }

  [[nodiscard]] bool point(tool::Vec3 p, Pixel& out) const {
    const tool::Vec4 c = vp_.mul({p.x, p.y, p.z, 1.f});
    if (c.w < kNear) return false;
    out = toPixel(c);
    return true;
  }

  // Segmento recortado no plano da camera: uma ponta atras nao estica a linha. false se o segmento inteiro esta atras.
  [[nodiscard]] bool segment(tool::Vec3 a, tool::Vec3 b, Pixel& pa, Pixel& pb) const {
    tool::Vec4 ca = vp_.mul({a.x, a.y, a.z, 1.f}), cb = vp_.mul({b.x, b.y, b.z, 1.f});
    if (ca.w < kNear && cb.w < kNear) return false;
    if (ca.w < kNear) ca = lerp(ca, cb, (kNear - ca.w) / (cb.w - ca.w));
    else if (cb.w < kNear) cb = lerp(cb, ca, (kNear - cb.w) / (ca.w - cb.w));
    pa = toPixel(ca);
    pb = toPixel(cb);
    return true;
  }

private:
  static constexpr float kNear = 0.01f;

  static tool::Vec4 lerp(const tool::Vec4& p, const tool::Vec4& q, float t) {
    return {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t};
  }

  [[nodiscard]] Pixel toPixel(const tool::Vec4& c) const {
    return {area_.x + (c.x / c.w * 0.5f + 0.5f) * area_.w, area_.y + (1.f - (c.y / c.w * 0.5f + 0.5f)) * area_.h};
  }

  tool::Mat4 vp_{};
  Area area_{};
};

// Distancia (px) de um ponto a um segmento.
inline float distanceToSegment(Pixel p, Pixel a, Pixel b) {
  const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
  const float t = l2 > 1e-6f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.f, 1.f) : 0.f;
  return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

// Modo do gizmo. Os valores batem com ed.gizmoMode (0 = mover, 1 = girar, 2 = escalar).
enum class Mode : std::uint8_t { Move = 0, Rotate = 1, Scale = 2 };

// Alca do gizmo: eixos X, Y e Z (0..2) e o centro (3), que escala uniforme. None = nenhuma.
enum class Handle : std::int8_t { None = -1, X = 0, Y = 1, Z = 2, Center = 3 };

inline constexpr int kRingSegments = 24;       // linhas do anel de girar (24 segmentos formam o anel)
inline constexpr float kCaptureRadius = 9.f;   // raio de captura do mouse, em pixels

inline tool::Vec3 axisOf(int i) { return i == 0 ? tool::Vec3{1, 0, 0} : i == 1 ? tool::Vec3{0, 1, 0} : tool::Vec3{0, 0, 1}; }

// Eixos do mundo (X, Y, Z). O gizmo usa estes eixos no espaco Mundo.
inline std::array<tool::Vec3, 3> worldAxes() { return {axisOf(0), axisOf(1), axisOf(2)}; }

// Eixos de um objeto (espaco Local): as colunas X, Y e Z da matriz de mundo, sem escala nem posicao, normalizadas.
// Uma coluna quase vazia (escala zero) volta ao eixo do mundo. Exemplo: a Caixa azul girada 90 graus em Y tem o
// eixo X local apontando para -Z do mundo.
inline std::array<tool::Vec3, 3> localAxes(const tool::Mat4& world) {
  const tool::Vec4 cols[3] = {world.mul({1, 0, 0, 0}), world.mul({0, 1, 0, 0}), world.mul({0, 0, 1, 0})};
  std::array<tool::Vec3, 3> out = worldAxes();
  for (std::size_t i = 0; i < 3; ++i) {
    const tool::Vec3 n = tool::normalize({cols[i].x, cols[i].y, cols[i].z});
    if (tool::length(n) > 0.5f) out[i] = n;
  }
  return out;
}

// Comprimento do eixo no mundo: cresce com a distancia da camera, para o tamanho ficar ~constante na tela.
inline float axisLength(tool::Vec3 origin, tool::Vec3 eye) { return std::max(0.2f, tool::length(origin - eye) * 0.16f); }

// Gizmo do objeto: origem (mundo e tela) e comprimento do eixo. Nao existe se o objeto nao esta na frente da camera.
// `axes` e a direcao de X, Y e Z (mundo ou local). Mover e escalar usam `axes`; o anel de girar fica no mundo.
struct Gizmo {
  tool::Vec3 origin{};
  float len = 1.f;
  Pixel center{};
  std::array<tool::Vec3, 3> axes = worldAxes();
};

// Ponto do anel de girar (no plano perpendicular ao eixo i) para o lado k de kRingSegments.
inline tool::Vec3 ringPoint(const Gizmo& g, int i, int k) {
  const tool::Vec3 u = axisOf((i + 1) % 3), r = axisOf((i + 2) % 3);
  const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(kRingSegments);
  return g.origin + (u * std::cos(a) + r * std::sin(a)) * g.len;
}

// Um traco do gizmo na tela: segmento de a a b, de qual alca ele e, e se esta destacado (arrasto ou sob o mouse).
struct Stroke {
  Pixel a, b;
  Handle handle = Handle::None;
  bool highlighted = false;
};

// Alca sob o mouse. Eixos: segmento centro -> ponta. Girar: poligono do anel. Escalar: o centro vence os eixos.
inline Handle hitTest(const Projection& proj, const Gizmo& g, Mode mode, Pixel mouse) {
  Handle hot = Handle::None;
  float best = kCaptureRadius;
  for (int i = 0; i < 3; ++i) {
    if (mode == Mode::Rotate) {
      Pixel prev{};
      bool havePrev = false;
      for (int k = 0; k <= kRingSegments; ++k) {
        Pixel s{};
        if (!proj.point(ringPoint(g, i, k), s)) { havePrev = false; continue; }
        if (havePrev) {
          const float d = distanceToSegment(mouse, prev, s);
          if (d < best) { best = d; hot = static_cast<Handle>(i); }
        }
        prev = s;
        havePrev = true;
      }
    } else {
      Pixel tip{};
      if (!proj.point(g.origin + g.axes[static_cast<std::size_t>(i)] * g.len, tip)) continue;
      const float d = distanceToSegment(mouse, g.center, tip);
      if (d < best) { best = d; hot = static_cast<Handle>(i); }
    }
  }
  if (mode == Mode::Scale && std::hypot(mouse.x - g.center.x, mouse.y - g.center.y) < kCaptureRadius) hot = Handle::Center;
  return hot;
}

// Tracos do gizmo para desenhar: uma lista so, para a janela montar um lote por textura (sem um quad por alca).
// Os eixos saem como segmentos; as pontas e o cubinho ficam com o chamador (poucos itens).
inline std::vector<Stroke> strokes(const Projection& proj, const Gizmo& g, Mode mode, Handle shown) {
  std::vector<Stroke> out;
  out.reserve(static_cast<std::size_t>(3 * kRingSegments));
  for (int i = 0; i < 3; ++i) {
    const Handle h = static_cast<Handle>(i);
    const bool hi = shown == h;
    if (mode == Mode::Rotate) {
      for (int k = 0; k < kRingSegments; ++k) {
        Pixel a{}, b{};
        if (proj.segment(ringPoint(g, i, k), ringPoint(g, i, k + 1), a, b)) out.push_back({a, b, h, hi});
      }
    } else {
      Pixel tip{};
      if (proj.point(g.origin + g.axes[static_cast<std::size_t>(i)] * g.len, tip)) out.push_back({g.center, tip, h, hi});
    }
  }
  return out;
}

// Entrada do arrasto: o que estava no objeto quando o arrasto comecou, e o mouse nesse momento.
struct DragStart {
  Mode mode = Mode::Move;
  Handle handle = Handle::X;
  std::array<float, 3> pos{}, rot{}, scale{1.f, 1.f, 1.f};
  Pixel mouse0{};
  tool::Vec3 startWorld{};   // posicao de mundo no inicio (objeto com pai: o Transform e local)
  float angle0 = 0.f;        // girar: angulo do mouse em torno do centro no inicio
  Gizmo gizmo{};             // gizmo no inicio do arrasto
  tool::Vec3 eye{};          // camera (girar: sentido do giro depende de a camera ver o eixo de frente)
  bool hasParent = false;
  bool worldAxes = true;     // eixos do mundo: mover sem pai grava a coordenada absoluta; senao, o deslocamento
  tool::Mat4 parentInv{};    // inversa da matriz de mundo do pai (so com hasParent)
};

// Resultado de um quadro de arrasto: os valores novos (o chamador grava no objeto) e o texto de leitura.
struct DragResult {
  bool changed = false;
  std::array<float, 3> pos{}, rot{}, scale{};
  std::string readout;
};

// Opcoes do quadro: mouse atual, ima ligado (encaixa na grade), passo do ima e a camera projetada.
struct DragFrame {
  Pixel mouse{};
  bool snap = false;
  float snapStep = 0.25f;
  Projection proj = Projection{};
};

inline float wrapDegrees(float deg) {
  while (deg > 180.f) deg -= 360.f;
  while (deg < -180.f) deg += 360.f;
  return deg;
}

// Novo Transform de um quadro de arrasto. Mover: o eixo segue o mouse projetado na direcao do eixo na tela.
// Girar: angulo do mouse em torno do centro, passos de 15 graus com o ima. Escalar: eixo ou uniforme (centro).
inline DragResult dragEdit(const DragStart& s, const DragFrame& f) {
  DragResult r{false, s.pos, s.rot, s.scale, {}};
  const float dx = f.mouse.x - s.mouse0.x, dy = f.mouse.y - s.mouse0.y;
  const int a = static_cast<int>(s.handle);
  const tool::Vec3 ax = a < 3 ? s.gizmo.axes[static_cast<std::size_t>(a)] : tool::Vec3{};
  char buf[64];

  if (s.mode == Mode::Rotate && a < 3) {
    const float cur = std::atan2(f.mouse.y - s.gizmo.center.y, f.mouse.x - s.gizmo.center.x);
    float delta = cur - s.angle0;
    while (delta > 3.14159265f) delta -= 6.2831853f;
    while (delta < -3.14159265f) delta += 6.2831853f;
    const float facing = tool::dot(axisOf(a), s.eye - s.startWorld) > 0.f ? -1.f : 1.f;   // eixo do anel para a camera: sentido horario e negativo
    float deg = delta * facing * 57.2957795f;
    if (f.snap) deg = std::round(deg / 15.f) * 15.f;
    r.rot[static_cast<std::size_t>(a)] = wrapDegrees(s.rot[static_cast<std::size_t>(a)] + deg);
    r.changed = true;
    std::snprintf(buf, sizeof buf, "%s rot %d deg", a == 0 ? "X" : a == 1 ? "Y" : "Z", static_cast<int>(std::round(r.rot[static_cast<std::size_t>(a)])));
    r.readout = buf;
    return r;
  }

  if (s.mode == Mode::Scale && s.handle == Handle::Center) {   // escala uniforme: arrastar para a direita/cima aumenta
    const float k = std::clamp(1.f + (dx - dy) * 0.006f, 0.01f, 100.f);
    for (std::size_t i = 0; i < 3; ++i) {
      float v = s.scale[i] * k;
      if (f.snap) v = std::round(v / 0.1f) * 0.1f;
      r.scale[i] = std::clamp(v, 0.01f, 10000.f);
    }
    r.changed = true;
    std::snprintf(buf, sizeof buf, "scale x%.2f", static_cast<double>(k));
    r.readout = buf;
    return r;
  }

  if (a >= 3) return r;
  // Mover/escalar num eixo: o mouse, projetado na direcao do eixo na tela, da os metros ao longo dele.
  Pixel s0{}, s1{};
  if (!f.proj.point(s.startWorld, s0) || !f.proj.point(s.startWorld + ax * s.gizmo.len, s1)) return r;
  const float sx = s1.x - s0.x, sy = s1.y - s0.y, sl2 = sx * sx + sy * sy;
  if (sl2 <= 4.f) return r;
  const float along = (dx * sx + dy * sy) / sl2 * s.gizmo.len;   // metros ao longo do eixo
  const auto ai = static_cast<std::size_t>(a);

  if (s.mode == Mode::Move) {
    const float step = std::max(0.01f, f.snapStep);
    float val = 0.f;   // metros mostrados na leitura
    if (s.hasParent || !s.worldAxes) {   // deslocamento: o eixo (do mundo ou local) vira mudanca no Transform
      val = f.snap ? std::round(along / step) * step : along;
      const tool::Vec4 world{ax.x * val, ax.y * val, ax.z * val, 0.f};
      const tool::Vec4 local = s.hasParent ? s.parentInv.mul(world) : world;   // com pai, o Transform e local ao pai
      r.pos = {s.pos[0] + local.x, s.pos[1] + local.y, s.pos[2] + local.z};
    } else {   // mundo, sem pai: a coordenada do eixo vai para onde o mouse marca
      val = s.pos[ai] + along;
      if (f.snap) val = std::round(val / step) * step;
      r.pos = s.pos;
      r.pos[ai] = val;
    }
    r.changed = true;
    std::snprintf(buf, sizeof buf, "%s %.2f m", a == 0 ? "X" : a == 1 ? "Y" : "Z", static_cast<double>(val));
    r.readout = buf;
    return r;
  }

  if (s.mode == Mode::Scale) {
    float v = s.scale[ai] * (1.f + along / s.gizmo.len);
    if (f.snap) v = std::round(v / 0.1f) * 0.1f;
    r.scale[ai] = std::clamp(v, 0.01f, 10000.f);
    r.changed = true;
    std::snprintf(buf, sizeof buf, "scale %s %.2f", a == 0 ? "X" : a == 1 ? "Y" : "Z", static_cast<double>(r.scale[ai]));
    r.readout = buf;
  }
  return r;
}

// Linha da grade do chao (Y = 0). `major` = a cada 5 linhas (mais forte). `alpha` 0..255 some na borda.
struct GridLine {
  tool::Vec3 a{}, b{};
  int alpha = 0;
  bool major = false;
};

// Grade infinita que acompanha a camera: o passo cresce com a altura (potencias de 10). Mesma conta do drawGrid antigo.
inline std::vector<GridLine> gridLines(tool::Vec3 eye) {
  const float s = std::max(1.f, std::pow(10.f, std::floor(std::log10(std::max(1.f, std::abs(eye.y)) / 10.f))));
  constexpr int kHalf = 60;
  const float cx = std::round(eye.x / s) * s, cz = std::round(eye.z / s) * s, reach = kHalf * s;
  std::vector<GridLine> out;
  out.reserve(static_cast<std::size_t>(2 * (2 * kHalf + 1)));
  for (int i = -kHalf; i <= kHalf; ++i) {
    const float off = static_cast<float>(i) * s;
    const bool major = i % 5 == 0;
    const float d = std::abs(off) / reach, fade = std::clamp(1.f - d * d, 0.f, 1.f);
    const int alpha = static_cast<int>((major ? 70 : 34) * fade);
    out.push_back({{cx + off, 0, cz - reach}, {cx + off, 0, cz + reach}, alpha, major});
    out.push_back({{cx - reach, 0, cz + off}, {cx + reach, 0, cz + off}, alpha, major});
  }
  return out;
}

}  // namespace editor::scene_view_model
