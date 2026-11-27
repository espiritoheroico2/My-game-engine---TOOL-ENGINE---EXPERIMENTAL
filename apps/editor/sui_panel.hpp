#pragma once

// Base das janelas SpriteUI do editor (barra de status, mapa de teste). Nao usa LayoutTree: a janela calcula a
// propria geometria. A SpriteUI so exige um dono valido em cada item (o extrator recusa handle invalido).
//
//   Canvas  guarda retangulos e textos ja medidos, em coordenadas locais da janela.
//
// Medida e corte de texto acontecem so ao montar o conteudo. Por quadro, o canvas so desloca e recorta o que guardou.
// Como a UI e remontada todo quadro, a medida (forma + layout) de cada texto fica em um cache limitado (ver
// Canvas::cacheStats). O resultado e identico ao calculo direto: o cache so evita repetir o mesmo trabalho.

#include "spriteui_host.hpp"
#include "text_atlas_set.hpp"

#include "sui_palette.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor::sui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

inline constexpr float kPanelPadding = 14.f;

// Slice do atlas padrao (glifos em 16 px). Os textos novos usam o slice do proprio tamanho (TextAtlasSet).

inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}

// Converte uma cor da paleta (sui_palette.hpp) para o espaco da SpriteUI.
inline SU::Vec4 PaletteColor(const palette::Rgb& c) { return Srgb(c.r, c.g, c.b, c.a); }

// Cores dos paineis SpriteUI. Vem da paleta em uso (Configuracoes > Appearance). Os paineis criados depois
// de uma troca de paleta usam as novas cores.
struct Theme {
  SU::Vec4 panel = PaletteColor(palette::active().panel);
  SU::Vec4 surface = PaletteColor(palette::active().surface);
  SU::Vec4 text = PaletteColor(palette::active().text);
  SU::Vec4 textDim = PaletteColor(palette::active().textDim);
  SU::Vec4 accent = PaletteColor(palette::active().accent);
  SU::Vec4 scroll = PaletteColor(palette::active().scroll);
  SU::Vec4 divider = PaletteColor(palette::active().border);
  SU::Vec4 error = PaletteColor(palette::active().error);
};

// Retangulo local da janela (origem no canto superior esquerdo da janela).
struct Box {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  [[nodiscard]] bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Texto e retangulos ja medidos, em coordenadas locais do canvas.
class Canvas {
 public:
  explicit Canvas(const TextAtlasSet& text) : text_{text} {}
  // Slice do atlas deste tamanho de texto (o campo de texto da biblioteca precisa dele ao desenhar).
  std::uint32_t sliceFor(float size) const { return text_.sliceFor(size * fontScale_); }
  void setFontScale(float scale) {
    const float next = std::clamp(scale, 0.5f, 2.f);
    if (next != fontScale_) clearTextCache();   // a escala muda as metricas de todos os glifos
    fontScale_ = next;
  }

  void clear() {
    prims_.clear();
    texts_.clear();
    validateAtlases();
  }

  // Contadores do cache de texto (para mostrar no Profiler no futuro: hits/(hits+misses) por quadro).
  struct CacheStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;   // vezes que o cache encheu e foi esvaziado
    std::size_t entries = 0;
  };
  [[nodiscard]] CacheStats cacheStats() const {
    CacheStats s;
    s.hits = hits_;
    s.misses = misses_;
    s.evictions = evictions_;
    s.entries = cache_.size();
    return s;
  }
  void resetCacheStats() { hits_ = misses_ = evictions_ = 0; }

  // Esvazia o cache de medidas e o de altura de linha. Chamado ao mudar a escala da fonte; um conjunto de atlas
  // recarregado (outra escala de tela, outra fonte) e detectado em clear() pela impressao digital dos atlas usados.
  void clearTextCache() const {
    cache_.clear();
    lineHeights_.clear();
  }

  // Layout sem cache (referencia para testes e para quem precisa de uma copia independente).
  [[nodiscard]] ST::TextLayoutResult layoutUncached(const std::string& utf8, float fontSize) const {
    fontSize *= fontScale_;
    return computeLayout(utf8, fontSize, text_.atlasFor(fontSize), 0.f, ST::TextWrap::None);
  }

  [[nodiscard]] ST::TextLayoutResult layout(const std::string& utf8, float fontSize) const {
    return *cachedLayout(utf8, fontSize, Kind::Line, 0.f);
  }

  // Altura de uma linha. Uma entrada por tamanho (alternar tamanhos nao recalcula).
  [[nodiscard]] float lineHeight(float fontSize) const {
    for (const auto& e : lineHeights_)
      if (e.first == fontSize) return e.second;
    const float lh = cachedLayout("Ag", fontSize, Kind::Line, 0.f)->lineHeight;
    if (lineHeights_.size() >= kMaxLineHeights) lineHeights_.clear();
    lineHeights_.emplace_back(fontSize, lh);
    return lh;
  }

  // Texto cortado com "..." para caber em maxWidth, e a largura resultante.
  struct Fitted {
    std::string text;
    float width = 0.f;
  };
  [[nodiscard]] Fitted fitted(const std::string& text, float fontSize, float maxWidth) const {
    Fitted f;
    f.width = fitShared(text, fontSize, maxWidth, &f.text)->size.x;
    return f;
  }

  // Texto de largura limitada (cortado com "..."). Retorna a largura desenhada, para alinhar o que vem depois.
  float addText(float x, float y, const std::string& text, float fontSize, SU::Vec4 color, float maxWidth, bool alignRight = false) {
    if (text.empty() || maxWidth <= 0.f) return 0.f;
    LayoutPtr lay = fitShared(text, fontSize, maxWidth, nullptr);
    const float w = lay->size.x;
    pushText(alignRight ? x - w : x, y, std::move(lay), color, text_.sliceFor(fontSize * fontScale_));
    return w;
  }

  // Paragrafo quebrado por palavras dentro de `width`. Retorna a altura ocupada.
  float addParagraph(float x, float y, const std::string& text, float fontSize, SU::Vec4 color, float width) {
    LayoutPtr lay = cachedLayout(text, fontSize, Kind::Paragraph, width);
    const float height = lay->size.y;
    pushText(x, y, std::move(lay), color, text_.sliceFor(fontSize * fontScale_));
    return height;
  }

  // Z dos textos deste canvas (padrao 0.9). Um canvas de menu aberto usa um Z maior, por cima dos outros.
  void setTextZ(float z) { textZ_ = z; }

  // Segmento de reta (cabos em curva, por exemplo): de (x1, y1) a (x2, y2), com espessura em pixels logicos.
  void addLine(float x1, float y1, float x2, float y2, float thickness, SU::Vec4 color, float z = 0.2f) {
    if (thickness <= 0.f) return;
    Prim p;
    p.kind = Prim::Kind::Line;
    p.x = x1;
    p.y = std::min(y1, y2) - thickness;
    p.w = std::abs(x2 - x1);
    p.h = std::abs(y2 - y1) + 2.f * thickness;
    p.x2 = x2;
    p.y2 = y2;
    p.y1 = y1;
    p.thickness = thickness;
    p.color = color;
    p.z = z;
    prims_.push_back(p);
  }

  void addSolid(float x, float y, float w, float h, SU::Vec4 color, float z = 0.2f, float cornerRadius = 0.f) {
    if (w <= 0.f || h <= 0.f) return;
    Prim p;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.color = color;
    p.z = z;
    p.cornerRadius = cornerRadius;
    prims_.push_back(p);
  }

  // Adiciona os itens visiveis: desloca o conteudo por (originX, originY - scrollY) e descarta o que sai da faixa
  // [viewTop, viewBottom]. `owner` e o handle do dono (valido); `clip` e o indice do recorte; `paint` numera a ordem.
  void appendItems(std::vector<SR::UIRenderItem>& out, float originX, float originY, float scrollY, float viewTop, float viewBottom,
                   SU::UIHandle owner, std::uint32_t clip, std::uint64_t& paint) const {
    for (const Prim& p : prims_) {
      const float sy = originY + p.y - scrollY;
      if (sy + p.h < viewTop || sy > viewBottom) continue;
      if (p.kind == Prim::Kind::Line) {
        const float ly1 = originY + p.y1 - scrollY, ly2 = originY + p.y2 - scrollY;
        if (auto line = SR::MakeLine(owner, {originX + p.x, ly1}, {originX + p.x2, ly2}, p.thickness, p.color, clip, p.z, paint++)) out.push_back(*line);
        continue;
      }
      if (p.kind == Prim::Kind::Solid) {
        SR::UIRenderItem item = solidItem(originX + p.x, sy, p.w, p.h, p.color, p.z, owner, clip, paint++);
        if (p.cornerRadius > 0.f) {
          item.flags |= SR::FlagRoundedPanel;
          item.cornerRadius = p.cornerRadius;
        }
        out.push_back(item);
        continue;
      }
      SU::UILayoutRect box;
      box.resolvedPosition = {originX + p.x, sy};
      box.resolvedSize = {p.w, p.h};
      auto items = ST::GenerateTextRenderItems(*texts_[p.text], box, p.color, p.slice, clip, p.z, paint, owner, 0u);
      paint += items.size() + 1;
      out.insert(out.end(), items.begin(), items.end());
    }
  }

  static SR::UIRenderItem solidItem(float x, float y, float w, float h, SU::Vec4 color, float z, SU::UIHandle owner, std::uint32_t clip,
                                    std::uint64_t paint) {
    SR::UIRenderItem item;
    item.handle = owner;
    item.layout.resolvedPosition = {x, y};
    item.layout.resolvedSize = {w, h};
    item.tintPremultipliedLinear = color;
    item.textureSlice = SR::WhiteTextureSlice;
    item.clipIndex = clip;
    item.zOrder = z;
    item.paintOrder = paint;
    return item;
  }

 private:
  struct Prim {
    enum class Kind : std::uint8_t { Solid, Text, Line };
    Kind kind = Kind::Solid;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float x2 = 0.f, y1 = 0.f, y2 = 0.f, thickness = 0.f;   // Line: (x, y1) -> (x2, y2)
    SU::Vec4 color{};
    float z = 0.f;
    float cornerRadius = 0.f;
    std::uint32_t text = 0;   // Text: indice em texts_
    std::uint32_t slice = 1;  // Text: slice de textura do tamanho (TextAtlasSet)
  };

  using LayoutPtr = std::shared_ptr<const ST::TextLayoutResult>;
  enum class Kind : std::uint8_t { Line = 0, Paragraph = 1, Fit = 2 };

  void pushText(float x, float y, LayoutPtr lay, SU::Vec4 color, std::uint32_t slice) {
    Prim p;
    p.kind = Prim::Kind::Text;
    p.x = x;
    p.y = y;
    p.w = lay->size.x;
    p.h = lay->size.y;
    p.color = color;
    p.z = textZ_;
    p.text = static_cast<std::uint32_t>(texts_.size());
    p.slice = slice;
    texts_.push_back(std::move(lay));
    prims_.push_back(p);
  }

  // Calculo direto (sem cache). `fontSize` ja inclui fontScale_.
  [[nodiscard]] static ST::TextLayoutResult computeLayout(const std::string& utf8, float fontSize, const ST::TruetypeAtlas& atlas,
                                                          float maxWidth, ST::TextWrap wrap) {
    ST::TextLayoutConfig cfg;
    cfg.fontSize = fontSize;
    cfg.maxWidth = maxWidth;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = wrap;
    return ST::LayoutText(utf8, ST::ShapeTruetype(utf8, atlas), atlas.metrics, cfg);
  }

  // Cache de medidas. Chave: tipo, texto, tamanho efetivo (com fontScale_), largura (Paragraph/Fit) e o atlas do
  // tamanho (endereco). O hash de 64 bits indexa; o conteudo e conferido, e uma colisao so vira falha de cache.
  struct Entry {
    std::string text;
    std::uint32_t sizeBits = 0;
    std::uint32_t widthBits = 0;
    Kind kind = Kind::Line;
    const void* atlas = nullptr;
    LayoutPtr lay;
    std::string fitted;   // Fit: texto cortado
  };
  struct Probe {
    const std::string& text;
    std::uint32_t sizeBits;
    std::uint32_t widthBits;
    Kind kind;
    const void* atlas;
  };
  struct Fingerprint {
    const void* ptr = nullptr;
    std::uint32_t width = 0;
    std::size_t glyphs = 0;
    float advanceM = 0.f;
    float rectM[8] = {};   // uvRect e planeBounds de 'M': mudam com o tamanho rasterizado do atlas
    bool operator==(const Fingerprint& o) const {
      return ptr == o.ptr && width == o.width && glyphs == o.glyphs && advanceM == o.advanceM && std::equal(std::begin(rectM), std::end(rectM), std::begin(o.rectM));
    }
  };

  static constexpr std::size_t kMaxEntries = 4096;   // acima disso o cache e esvaziado
  static constexpr std::size_t kMaxLineHeights = 16;

  static std::uint64_t hashOf(const Probe& k) {
    std::uint64_t h = std::hash<std::string_view>{}(k.text);
    auto mix = [&h](std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    mix(k.sizeBits);
    mix(k.widthBits);
    mix(static_cast<std::uint64_t>(k.kind));
    mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(k.atlas)));
    return h;
  }
  static bool same(const Entry& e, const Probe& k) {
    return e.kind == k.kind && e.sizeBits == k.sizeBits && e.widthBits == k.widthBits && e.atlas == k.atlas && e.text == k.text;
  }

  const Entry* find(const Probe& k, std::uint64_t h) const {
    auto it = cache_.find(h);
    if (it != cache_.end() && same(it->second, k)) {
      ++hits_;
      return &it->second;
    }
    ++misses_;
    return nullptr;
  }

  Entry& insert(const Probe& k, std::uint64_t h) const {
    if (cache_.size() >= kMaxEntries) {
      cache_.clear();   // esvaziamento total: simples e sem custo por acesso
      ++evictions_;
    }
    if (cache_.bucket_count() < kMaxEntries) cache_.reserve(kMaxEntries);
    Entry& e = cache_[h];
    e = Entry{};
    e.text = k.text;
    e.sizeBits = k.sizeBits;
    e.widthBits = k.widthBits;
    e.kind = k.kind;
    e.atlas = k.atlas;
    return e;
  }

  void noteAtlas(float effSize) const { usedSlots_ |= 1ull << (text_.sliceFor(effSize) - TextAtlasSet::kFirstSlice); }

  LayoutPtr cachedLayout(const std::string& utf8, float fontSize, Kind kind, float width) const {
    const float eff = fontSize * fontScale_;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(eff);
    noteAtlas(eff);
    const bool para = kind == Kind::Paragraph;
    const Probe k{utf8, std::bit_cast<std::uint32_t>(eff), para ? std::bit_cast<std::uint32_t>(width) : 0u, kind, &atlas};
    const std::uint64_t h = hashOf(k);
    if (const Entry* e = find(k, h)) return e->lay;
    LayoutPtr lay = std::make_shared<const ST::TextLayoutResult>(
        computeLayout(utf8, eff, atlas, para ? width : 0.f, para ? ST::TextWrap::Word : ST::TextWrap::None));
    insert(k, h).lay = lay;
    return lay;
  }

  // Corte com "..." para caber em maxWidth (sem cortar um caractere UTF-8 ao meio). Devolve o layout do texto
  // final (guardado no cache) e, se pedido, o texto. Equivale ao laco linear antigo: parte da estimativa
  // proporcional e fica com o maior prefixo (em fronteira UTF-8) que cabe; aqui por busca binaria.
  LayoutPtr fitShared(const std::string& s, float fontSize, float maxWidth, std::string* outText) const {
    const float eff = fontSize * fontScale_;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(eff);
    noteAtlas(eff);
    const Probe k{s, std::bit_cast<std::uint32_t>(eff), std::bit_cast<std::uint32_t>(maxWidth), Kind::Fit, &atlas};
    const std::uint64_t h = hashOf(k);
    if (const Entry* e = find(k, h)) {
      if (outText) *outText = e->fitted;
      return e->lay;
    }
    std::string fitted;
    LayoutPtr result;
    LayoutPtr full = cachedLayout(s, fontSize, Kind::Line, 0.f);
    if (full->size.x <= maxWidth) {
      fitted = s;
      result = std::move(full);
    } else {
      const float fullW = full->size.x;
      const std::size_t est =
          std::min(s.size(), static_cast<std::size_t>(std::floor(static_cast<float>(s.size()) * maxWidth / std::max(1.f, fullW))));
      // Maior fronteira de caractere <= pos (0 se nao houver; pos == size() e fronteira).
      auto snap = [&s](std::size_t pos) {
        while (pos > 0 && pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
        return pos;
      };
      std::size_t lo = 1, hi = snap(est), best = 0;
      ST::TextLayoutResult bestLay;
      std::string cand;
      while (lo <= hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const std::size_t b = snap(mid);
        if (b < lo) {   // nenhuma fronteira em [lo, mid]
          lo = mid + 1;
          continue;
        }
        cand.assign(s, 0, b);
        cand += "...";
        ST::TextLayoutResult lay = computeLayout(cand, eff, atlas, 0.f, ST::TextWrap::None);
        if (lay.size.x <= maxWidth) {
          best = b;
          bestLay = std::move(lay);
          lo = mid + 1;
        } else {
          hi = b - 1;
        }
      }
      if (best == 0) {
        fitted = "...";
        result = cachedLayout(fitted, fontSize, Kind::Line, 0.f);
      } else {
        fitted.assign(s, 0, best);
        fitted += "...";
        result = std::make_shared<const ST::TextLayoutResult>(std::move(bestLay));
      }
    }
    Entry& e = insert(k, h);
    e.lay = result;
    e.fitted = fitted;
    if (outText) *outText = std::move(fitted);
    return result;
  }

  // Impressao digital de cada atlas usado: se o conjunto foi recarregado (outra escala de tela, outra fonte), o
  // endereco, a largura da pagina, o numero de glifos ou a geometria de 'M' mudam e o cache inteiro e descartado.
  void validateAtlases() const {
    std::uint64_t used = usedSlots_;
    while (used) {
      const std::uint32_t slot = static_cast<std::uint32_t>(std::countr_zero(used));
      used &= used - 1;
      const ST::TruetypeAtlas* a = text_.atlasForSlice(TextAtlasSet::kFirstSlice + slot);
      Fingerprint fp;
      if (a) {
        fp.ptr = a;
        fp.width = a->width;
        fp.glyphs = a->glyphs.size();
        const auto m = a->glyphs.find('M');
        if (m != a->glyphs.end()) {
          fp.advanceM = m->second.advance;
          for (int i = 0; i < 4; ++i) {
            fp.rectM[i] = m->second.uvRect[i];
            fp.rectM[4 + i] = m->second.planeBounds[i];
          }
        }
      }
      Fingerprint& old = fingerprints_[slot];
      if (old.ptr && !(old == fp)) clearTextCache();
      old = fp;
    }
  }

  const TextAtlasSet& text_;
  float textZ_ = 0.9f;
  float fontScale_ = 1.f;
  mutable std::vector<std::pair<float, float>> lineHeights_;   // (tamanho, altura): uma entrada por tamanho
  mutable std::unordered_map<std::uint64_t, Entry> cache_;
  mutable std::array<Fingerprint, TextAtlasSet::kSlots> fingerprints_{};
  mutable std::uint64_t usedSlots_ = 0;
  mutable std::uint64_t hits_ = 0, misses_ = 0, evictions_ = 0;
  std::vector<Prim> prims_;
  std::vector<LayoutPtr> texts_;
};

// Handle valido e unico para o indice i (a geracao 1 nunca e zero).
inline SU::UIHandle handleFor(std::uint32_t index) { return SU::UIHandle{index, 1u}; }

}  // namespace editor::sui
