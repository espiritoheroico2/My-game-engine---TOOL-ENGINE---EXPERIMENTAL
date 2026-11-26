#pragma once

// Desenha a ShapeList (view_shapes.hpp) com a SpriteUI: linhas, retangulos, circulos e paineis viram itens de
// primitiva (Rendering/Primitives.hpp e Elements/Panel.hpp), e o texto passa pelo Canvas do editor (sui_panel.hpp).
// Os itens entram todos no mesmo quadro, sem um quad por forma: consecutivos com a mesma textura viram um lote so.
// Triangulos (particulas e setas do gizmo) nao sao primitiva da SpriteUI: ficam em trechos (TriRun), e drawPaintList
// os desenha com SDL entre os lotes, na ordem de pintura. Assim um triangulo nao aparece por cima de um menu.
//
// Exemplo: um anel de 24 lados em volta de uma luz vira 24 linhas em um lote. A seta do gizmo de mover e um triangulo
// e vira um trecho; o menu de contexto, gravado depois, cobre a seta quando os dois se encostam. Textos saem depois
// das formas do mesmo grupo, entao a legenda do botao aparece por cima do retangulo.

#include "sui_panel.hpp"
#include "view_shapes.hpp"

#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::view {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace Elm = ToolEngine::SpriteUi::Elements;

inline constexpr int kRingSides = 24;   // lados de um circulo desenhado so com linhas

inline SU::Vec4 premultiplied(Color c) {
  return sui::Srgb(static_cast<float>(c & 0xffu) / 255.f, static_cast<float>((c >> 8) & 0xffu) / 255.f,
                   static_cast<float>((c >> 16) & 0xffu) / 255.f, static_cast<float>((c >> 24) & 0xffu) / 255.f);
}

inline SDL_FColor straight(Color c) {
  return {static_cast<float>(c & 0xffu) / 255.f, static_cast<float>((c >> 8) & 0xffu) / 255.f,
          static_cast<float>((c >> 16) & 0xffu) / 255.f, static_cast<float>((c >> 24) & 0xffu) / 255.f};
}

// Trecho de triangulos: sai depois dos primeiros `before` itens, recortado no clip `clip` (indice da lista de clips).
struct TriRun {
  std::size_t before = 0;
  std::uint32_t clip = 0;
  std::vector<SDL_Vertex> verts;
};

// Um quadro de uma janela: itens da SpriteUI na ordem de pintura, e os trechos de triangulos entre eles.
struct PaintList {
  SU::UIHandle owner{};
  std::uint64_t paint = 1;
  std::vector<SR::UIRenderItem> items;
  std::vector<TriRun> runs;
};

class ShapePainter {
public:
  explicit ShapePainter(const sui::TextAtlasSet& text) : text_{text} {}

  // Largura de um texto na fonte `size` (pixels), para alinhar botoes e legendas antes de desenhar.
  [[nodiscard]] float measure(const std::string& s, float size) const { return text_.layout(s, size).size.x; }

  // Grava as formas de `list` em `out`, com o recorte `clip`. Os textos do grupo saem no fim do grupo, ou antes de
  // um painel (assim o painel cobre o que veio antes dele).
  void append(const ShapeList& list, std::uint32_t clip, PaintList& out) {
    for (const Shape& s : list.shapes()) {
      switch (s.kind) {
        case ShapeKind::Text: text_.addText(textX(s), textY(s), s.text, s.size, premultiplied(s.color), textWidth(s)); break;
        case ShapeKind::Triangle: addTriangle(s, clip, out); break;
        case ShapeKind::Panel:
          flushText(clip, out);
          addPanel(s, clip, out);
          break;
        default: addPrimitive(s, clip, out); break;
      }
    }
    flushText(clip, out);
  }

private:
  static SU::Vec2 toVec(Pixel p) { return {p.x, p.y}; }
  static SU::Rect rectOf(Pixel a, Pixel b) {
    const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {{x, y}, {std::fabs(a.x - b.x), std::fabs(a.y - b.y)}};
  }

  // Texto centrado: a largura medida desloca o ponto de ancoragem para a esquerda.
  float textX(const Shape& s) const { return s.centered ? s.a.x - text_.layout(s.text, s.size).size.x * 0.5f : s.a.x; }
  // Texto com middle: o topo da linha desce metade da altura medida, assim o centro vertical cai em a.y.
  float textY(const Shape& s) const { return s.middle ? s.a.y - text_.layout(s.text, s.size).size.y * 0.5f : s.a.y; }
  static float textWidth(const Shape& s) { return s.r > 0.f ? s.r : 1e6f; }

  // Linha, retangulo, circulo (ou anel de linhas): um item por forma, todos com a mesma cor e o mesmo recorte.
  void addPrimitive(const Shape& s, std::uint32_t clip, PaintList& out) {
    const SU::Vec4 col = premultiplied(s.color);
    switch (s.kind) {
      case ShapeKind::Line:
        if (auto item = SR::MakeLine(out.owner, toVec(s.a), toVec(s.b), s.thickness, col, clip, 0.f, out.paint++)) out.items.push_back(*item);
        break;
      case ShapeKind::RoundRect: {
        const SU::Rect rect = rectOf(s.a, s.b);
        if (s.radius > 0.f) {
          if (auto item = SR::MakeRoundedRect(out.owner, rect, s.radius, col, clip, 0.f, out.paint++)) out.items.push_back(*item);
        } else {
          out.items.push_back(SR::MakeSolidRect(out.owner, rect, col, clip, 0.f, out.paint++));
        }
        break;
      }
      case ShapeKind::CircleFilled: {
        // Circulo = retangulo de cantos no raio: o SDF do shader desenha o disco liso.
        const SU::Rect rect{{s.a.x - s.r, s.a.y - s.r}, {2.f * s.r, 2.f * s.r}};
        if (auto item = SR::MakeRoundedRect(out.owner, rect, s.r, col, clip, 0.f, out.paint++)) out.items.push_back(*item);
        break;
      }
      case ShapeKind::Ring:
        for (int k = 0; k < kRingSides; ++k) {
          const float a0 = 6.2831853f * static_cast<float>(k) / kRingSides, a1 = 6.2831853f * static_cast<float>(k + 1) / kRingSides;
          const Pixel p0{s.a.x + std::cos(a0) * s.r, s.a.y + std::sin(a0) * s.r}, p1{s.a.x + std::cos(a1) * s.r, s.a.y + std::sin(a1) * s.r};
          if (auto item = SR::MakeLine(out.owner, toVec(p0), toVec(p1), s.thickness, col, clip, 0.f, out.paint++)) out.items.push_back(*item);
        }
        break;
      default: break;
    }
  }

  // Painel da biblioteca (fundo, contorno e cantos): o mesmo desenho da janela Window/Panel da SpriteUI.
  void addPanel(const Shape& s, std::uint32_t clip, PaintList& out) {
    Elm::Panel box;
    box.handle = out.owner;
    box.border = {{std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y)}, {std::fabs(s.a.x - s.b.x), std::fabs(s.a.y - s.b.y)}};
    Elm::PanelStyle style;
    style.cornerRadius = s.radius;
    style.borderWidth = s.thickness;
    style.background = premultiplied(s.color);
    style.border = premultiplied(s.edge);
    std::vector<SR::UIRenderItem> built = Elm::BuildPanelItems(box, style, 0.f, out.paint);
    for (SR::UIRenderItem& item : built) item.clipIndex = clip;
    out.paint += built.size() + 1;
    out.items.insert(out.items.end(), built.begin(), built.end());
  }

  // Triangulo: entra no trecho aberto (ou abre um novo, se ja ha itens depois do ultimo trecho ou o recorte mudou).
  static void addTriangle(const Shape& s, std::uint32_t clip, PaintList& out) {
    if (out.runs.empty() || out.runs.back().before != out.items.size() || out.runs.back().clip != clip) {
      out.runs.push_back({out.items.size(), clip, {}});
    }
    const SDL_FColor col = straight(s.color);
    for (const Pixel& p : {s.a, s.b, s.c}) out.runs.back().verts.push_back({{p.x, p.y}, col, {0.f, 0.f}});
  }

  void flushText(std::uint32_t clip, PaintList& out) {
    text_.appendItems(out.items, 0.f, 0.f, 0.f, -1e9f, 1e9f, out.owner, clip, out.paint);
    text_.clear();
  }

  sui::Canvas text_;
};

// Desenha o quadro: lotes da SpriteUI entre os trechos de triangulos (SDL), cada trecho com o seu recorte.
// `clips` e a lista de recortes do quadro (indice 0 = janela); o item e o trecho usam os mesmos indices.
// Devolve false se o SDL falhar.
inline bool drawItemRange(SDL_Renderer* renderer, sui::SpriteUiSdlRenderer& sdl, SR::RenderExtractor& extractor, SR::FramePacket& packet,
                          std::uint64_t& frameId, const std::vector<SR::ClipRectGPU>& clips, const std::vector<SR::UIRenderItem>& items,
                          std::size_t from, std::size_t to) {
  if (from >= to) return true;
  const std::vector<SR::UIRenderItem> part(items.begin() + static_cast<std::ptrdiff_t>(from), items.begin() + static_cast<std::ptrdiff_t>(to));
  packet = extractor.Build(++frameId, part, nullptr, clips);
  if (!SR::IsFramePacketValid(packet)) return true;
  return sdl.draw(renderer, packet);
}

inline bool drawTriRun(SDL_Renderer* renderer, const std::vector<SR::ClipRectGPU>& clips, const TriRun& run) {
  if (run.verts.empty()) return true;
  const bool clipped = run.clip < clips.size();
  if (clipped) {
    const SR::ClipRectGPU& c = clips[run.clip];
    const SDL_Rect rect{static_cast<int>(c.minX), static_cast<int>(c.minY), static_cast<int>(c.maxX - c.minX), static_cast<int>(c.maxY - c.minY)};
    SDL_SetRenderClipRect(renderer, &rect);
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);   // cor reta: o alfa vale na mistura (ver canvas_paint.hpp)
  const bool ok = SDL_RenderGeometry(renderer, nullptr, run.verts.data(), static_cast<int>(run.verts.size()), nullptr, 0);
  SDL_SetRenderClipRect(renderer, nullptr);
  return ok;
}

inline bool drawPaintList(SDL_Renderer* renderer, sui::SpriteUiSdlRenderer& sdl, SR::RenderExtractor& extractor, SR::FramePacket& packet,
                          std::uint64_t& frameId, const std::vector<SR::ClipRectGPU>& clips, const PaintList& list) {
  std::size_t from = 0;
  for (const TriRun& run : list.runs) {
    if (!drawItemRange(renderer, sdl, extractor, packet, frameId, clips, list.items, from, run.before)) return false;
    if (!drawTriRun(renderer, clips, run)) return false;
    from = run.before;
  }
  return drawItemRange(renderer, sdl, extractor, packet, frameId, clips, list.items, from, list.items.size());
}

}  // namespace editor::view
