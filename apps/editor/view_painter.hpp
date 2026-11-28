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

#include "canvas_text.hpp"
#include "sui_panel.hpp"
#include "view_shapes.hpp"

#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
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
// Se `image` nao e vazio, o trecho e uma imagem: 4 vertices (um quadrilatero com UV) e o caminho do arquivo.
struct TriRun {
  std::size_t before = 0;
  std::uint32_t clip = 0;
  std::vector<SDL_Vertex> verts;
  std::string image;   // caminho absoluto da imagem PNG/JPEG (vazio = trecho de triangulos; "?" = sem caminho)
  bool fit = false;    // imagem: encaixa na caixa mantendo a proporcao (a caixa cheia vale so para o placeholder)
};

// Pixels de uma imagem decodificada (RGBA8, linha a linha de cima para baixo).
struct ImagePixels {
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;
};

// Cache de texturas SDL das imagens do Canvas, por caminho. Quem liga o decodificador (o editor, via image_io) chama
// setLoader; sem ele toda imagem vira placeholder. Falha e lembrada: o aviso sai uma vez por arquivo, nao por quadro.
// Limite: as texturas ficam ate trocar o renderizador ou chamar clear(); nao ha recarga automatica se o arquivo muda.
class ImageCache {
public:
  using Loader = std::function<bool(const std::string&, ImagePixels&, std::string*)>;
  using Warn = std::function<void(const std::string&)>;

  // Cache unico do processo.
  static ImageCache& get() { static ImageCache c; return c; }
  // Liga o decodificador e a funcao de aviso (log).
  void setLoader(Loader l, Warn w) { loader_ = std::move(l); warn_ = std::move(w); }
  // true se ja ha decodificador ligado.
  bool hasLoader() const { return static_cast<bool>(loader_); }
  // Esquece tudo e destroi as texturas (o renderizador ainda deve existir).
  void clear() { for (auto& [k, e] : map_) if (e.tex) SDL_DestroyTexture(e.tex); map_.clear(); gpuMap_.clear(); gpuGen_ = 0; }

  // Textura de uma imagem no backend GPU (Diligent): id dinamico + tamanho, ou id 0 (falhou: placeholder). O cache vale
  // para a geracao do backend; backend novo (troca de API) descarta a tabela (as texturas morreram com o antigo).
  struct GpuImage { std::uint32_t id = 0; int w = 0, h = 0; };
  GpuImage gpuTexture(sui::UiGpuSink& sink, const std::string& path) {
    if (sink.generation() != gpuGen_) { gpuMap_.clear(); gpuGen_ = sink.generation(); }
    auto it = gpuMap_.find(path);
    if (it != gpuMap_.end()) return it->second;
    GpuImage e;
    ImagePixels px;
    std::string err;
    if (!loader_ || !loader_(path, px, &err) || px.w <= 0 || px.h <= 0) {
      if (warn_) warn_("canvas image not loaded: '" + path + "' (" + (err.empty() ? "no decoder" : err) + ")");
    } else if ((e.id = sink.createDynamic(px.w, px.h, px.rgba.data())) != 0) {
      e.w = px.w; e.h = px.h;
    }
    gpuMap_[path] = e;
    return e;
  }

  // Textura da imagem, ou nullptr (arquivo ausente, formato ruim ou sem decodificador): o chamador desenha o placeholder.
  SDL_Texture* texture(SDL_Renderer* renderer, const std::string& path) {
    if (renderer != owner_) { map_.clear(); owner_ = renderer; }   // renderizador novo: as texturas antigas morreram junto
    auto it = map_.find(path);
    if (it != map_.end()) return it->second.tex;
    Entry e;
    ImagePixels px;
    std::string err;
    if (!loader_ || !loader_(path, px, &err) || px.w <= 0 || px.h <= 0) {
      if (warn_) warn_("canvas image not loaded: '" + path + "' (" + (err.empty() ? "no decoder" : err) + ")");
    } else if ((e.tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px.w, px.h))) {
      SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
      if (!SDL_UpdateTexture(e.tex, nullptr, px.rgba.data(), px.w * 4)) { SDL_DestroyTexture(e.tex); e.tex = nullptr; }
    }
    map_[path] = e;
    return e.tex;
  }

private:
  // Uma imagem do cache (textura nula = falhou, nao tenta de novo).
  struct Entry { SDL_Texture* tex = nullptr; };
  std::unordered_map<std::string, Entry> map_;
  std::unordered_map<std::string, GpuImage> gpuMap_;   // caminho GPU (ver gpuTexture)
  std::uint32_t gpuGen_ = 0;
  SDL_Renderer* owner_ = nullptr;
  Loader loader_;
  Warn warn_;
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
        case ShapeKind::Text:
          if (s.wrap > 0.f) addWrapped(s);
          else text_.addText(textX(s), textY(s), s.text, s.size, premultiplied(s.color), textWidth(s));
          break;
        case ShapeKind::Image:
          flushText(clip, out);   // o que veio antes (texto) fica embaixo da imagem
          addImage(s, clip, out);
          break;
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

  // Texto com quebra: as linhas saem do wrapLines com a mesma medida do desenho; altura de linha da fonte.
  void addWrapped(const Shape& s) {
    const float lh = text_.lineHeight(s.size);
    const std::vector<std::string> lines = canvas_text::wrapLines(s.text, s.wrap, [&](const std::string& t) { return measure(t, s.size); });
    float y = s.a.y;
    for (const std::string& line : lines) {
      if (!line.empty()) {
        const float x = s.centered ? s.a.x - measure(line, s.size) * 0.5f : s.a.x;
        text_.addText(x, y, line, s.size, premultiplied(s.color), 1e6f);
      }
      y += lh;
    }
  }

  // Imagem: um trecho proprio (quadrilatero com UV), desenhado pelo SDL entre os lotes, na ordem de pintura.
  static void addImage(const Shape& s, std::uint32_t clip, PaintList& out) {
    const SU::Rect r = rectOf(s.a, s.b);
    const SDL_FColor col = straight(s.color);
    TriRun run{out.items.size(), clip, {}, s.text.empty() ? std::string("?") : s.text, s.fit};
    const SDL_FPoint pt[4] = {{r.position.x, r.position.y}, {r.position.x + r.size.x, r.position.y}, {r.position.x + r.size.x, r.position.y + r.size.y}, {r.position.x, r.position.y + r.size.y}};
    const SDL_FPoint uv[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    for (int k = 0; k < 4; ++k) run.verts.push_back({pt[k], col, uv[k]});
    out.runs.push_back(std::move(run));
  }

  // Triangulo: entra no trecho aberto (ou abre um novo, se ja ha itens depois do ultimo trecho ou o recorte mudou).
  static void addTriangle(const Shape& s, std::uint32_t clip, PaintList& out) {
    if (out.runs.empty() || out.runs.back().before != out.items.size() || out.runs.back().clip != clip || !out.runs.back().image.empty()) {
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

// Imagem ausente ou invalida: caixa magenta translucida com um X, para o erro ficar visivel na tela.
inline bool drawImagePlaceholder(SDL_Renderer* renderer, const TriRun& run) {
  if (run.verts.size() < 4) return true;
  const SDL_FPoint a = run.verts[0].position, b = run.verts[2].position;
  const SDL_FRect box{a.x, a.y, b.x - a.x, b.y - a.y};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 200, 40, 200, 110);
  SDL_RenderFillRect(renderer, &box);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
  SDL_RenderRect(renderer, &box);
  SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
  return SDL_RenderLine(renderer, b.x, a.y, a.x, b.y);
}

inline bool drawTriRun(SDL_Renderer* renderer, const std::vector<SR::ClipRectGPU>& clips, const TriRun& run) {
  if (run.verts.empty()) return true;
  const bool clipped = run.clip < clips.size();
  if (clipped) {
    const SR::ClipRectGPU& c = clips[run.clip];
    const SDL_Rect rect{static_cast<int>(c.minX), static_cast<int>(c.minY), static_cast<int>(c.maxX - c.minX), static_cast<int>(c.maxY - c.minY)};
    SDL_SetRenderClipRect(renderer, &rect);
  }
  if (!run.image.empty()) {   // imagem do Canvas: textura do cache, ou placeholder se falhou
    SDL_Texture* tex = run.image == "?" ? nullptr : ImageCache::get().texture(renderer, run.image);
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    std::vector<SDL_Vertex> fitted;   // proporcao mantida: so os vertices mudam (UV inteira)
    const SDL_Vertex* vs = run.verts.data();
    float iw = 0.f, ih = 0.f;
    if (tex && run.fit && run.verts.size() >= 4 && SDL_GetTextureSize(tex, &iw, &ih)) {
      const SDL_FPoint p0 = run.verts[0].position, p2 = run.verts[2].position;
      const FitRect f = fitAspect(p2.x - p0.x, p2.y - p0.y, iw, ih);
      fitted = run.verts;
      fitted[0].position = {p0.x + f.x, p0.y + f.y};
      fitted[1].position = {p0.x + f.x + f.w, p0.y + f.y};
      fitted[2].position = {p0.x + f.x + f.w, p0.y + f.y + f.h};
      fitted[3].position = {p0.x + f.x, p0.y + f.y + f.h};
      vs = fitted.data();
    }
    const bool ok = tex ? SDL_RenderGeometry(renderer, tex, vs, 4, idx, 6) : drawImagePlaceholder(renderer, run);
    SDL_SetRenderClipRect(renderer, nullptr);
    return ok;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);   // cor reta: o alfa vale na mistura (ver canvas_paint.hpp)
  const bool ok = SDL_RenderGeometry(renderer, nullptr, run.verts.data(), static_cast<int>(run.verts.size()), nullptr, 0);
  SDL_SetRenderClipRect(renderer, nullptr);
  return ok;
}

// Linha fina como um quadrilatero (o backend GPU nao tem SDL_RenderLine): espessura em pixels.
inline void appendLineQuad(std::vector<sui::UiVertex>& out, float x0, float y0, float x1, float y1, float thickness, SDL_FColor c) {
  const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.f) return;
  const float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
  const sui::UiVertex a{x0 + nx, y0 + ny, 0.f, 0.f, c.r, c.g, c.b, c.a}, b{x1 + nx, y1 + ny, 0.f, 0.f, c.r, c.g, c.b, c.a};
  const sui::UiVertex d{x1 - nx, y1 - ny, 0.f, 0.f, c.r, c.g, c.b, c.a}, e{x0 - nx, y0 - ny, 0.f, 0.f, c.r, c.g, c.b, c.a};
  out.insert(out.end(), {a, b, d, a, d, e});
}

// Como drawTriRun, mas para o backend GPU (Diligent): triangulos coloridos e imagens do Canvas viram geometria avulsa do
// mesmo quadro, na ordem de pintura. As cores ja sao retas em sRGB (como no SDL).
inline bool drawTriRunGpu(sui::UiGpuSink& gpu, const std::vector<SR::ClipRectGPU>& clips, const TriRun& run) {
  if (run.verts.empty()) return true;
  const SR::ClipRectGPU* clip = run.clip < clips.size() ? &clips[run.clip] : nullptr;
  const auto conv = [](const SDL_Vertex& v) {
    return sui::UiVertex{v.position.x, v.position.y, v.tex_coord.x, v.tex_coord.y, v.color.r, v.color.g, v.color.b, v.color.a};
  };
  std::vector<sui::UiVertex> vs;
  if (run.image.empty()) {   // triangulos puros (setas do gizmo, particulas)
    vs.reserve(run.verts.size());
    for (const SDL_Vertex& v : run.verts) vs.push_back(conv(v));
    return gpu.drawGeometry(vs.data(), static_cast<int>(vs.size()), nullptr, 0, 0, false, clip);
  }
  const ImageCache::GpuImage img = run.image == "?" ? ImageCache::GpuImage{} : ImageCache::get().gpuTexture(gpu, run.image);
  if (run.verts.size() < 4) return true;
  if (img.id == 0) {   // placeholder: caixa magenta translucida, contorno e X brancos (igual ao SDL)
    const SDL_FPoint a = run.verts[0].position, b = run.verts[2].position;
    const SDL_FColor fill{200.f / 255.f, 40.f / 255.f, 200.f / 255.f, 110.f / 255.f}, line{1.f, 1.f, 1.f, 220.f / 255.f};
    appendLineQuad(vs, a.x, (a.y + b.y) * 0.5f, b.x, (a.y + b.y) * 0.5f, std::fabs(b.y - a.y), fill);
    appendLineQuad(vs, a.x, a.y, b.x, a.y, 1.f, line);
    appendLineQuad(vs, a.x, b.y, b.x, b.y, 1.f, line);
    appendLineQuad(vs, a.x, a.y, a.x, b.y, 1.f, line);
    appendLineQuad(vs, b.x, a.y, b.x, b.y, 1.f, line);
    appendLineQuad(vs, a.x, a.y, b.x, b.y, 1.f, line);
    appendLineQuad(vs, b.x, a.y, a.x, b.y, 1.f, line);
    return gpu.drawGeometry(vs.data(), static_cast<int>(vs.size()), nullptr, 0, 0, false, clip);
  }
  for (int k = 0; k < 4; ++k) vs.push_back(conv(run.verts[static_cast<std::size_t>(k)]));
  if (run.fit) {   // proporcao mantida: so os vertices mudam (UV inteira)
    const FitRect f = fitAspect(vs[2].x - vs[0].x, vs[2].y - vs[0].y, static_cast<float>(img.w), static_cast<float>(img.h));
    const float x0 = vs[0].x, y0 = vs[0].y;
    vs[0].x = x0 + f.x;       vs[0].y = y0 + f.y;
    vs[1].x = x0 + f.x + f.w; vs[1].y = y0 + f.y;
    vs[2].x = x0 + f.x + f.w; vs[2].y = y0 + f.y + f.h;
    vs[3].x = x0 + f.x;       vs[3].y = y0 + f.y + f.h;
  }
  const int idx[6] = {0, 1, 2, 0, 2, 3};
  return gpu.drawGeometry(vs.data(), 4, idx, 6, img.id, true, clip);
}

inline bool drawPaintList(SDL_Renderer* renderer, sui::SpriteUiSdlRenderer& sdl, SR::RenderExtractor& extractor, SR::FramePacket& packet,
                          std::uint64_t& frameId, const std::vector<SR::ClipRectGPU>& clips, const PaintList& list) {
  std::size_t from = 0;
  for (const TriRun& run : list.runs) {
    if (!drawItemRange(renderer, sdl, extractor, packet, frameId, clips, list.items, from, run.before)) return false;
    if (sui::UiGpuSink* gpu = sdl.gpu()) { if (!drawTriRunGpu(*gpu, clips, run)) return false; }
    else if (!drawTriRun(renderer, clips, run)) return false;
    from = run.before;
  }
  return drawItemRange(renderer, sdl, extractor, packet, frameId, clips, list.items, from, list.items.size());
}

}  // namespace editor::view
