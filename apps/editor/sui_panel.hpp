#pragma once

// Base das janelas SpriteUI do editor (barra de status, mapa de teste). Nao usa LayoutTree: a janela calcula a
// propria geometria. A SpriteUI so exige um dono valido em cada item (o extrator recusa handle invalido).
//
//   Canvas  guarda retangulos e textos ja medidos, em coordenadas locais da janela.
//
// Medida e corte de texto acontecem so ao montar o conteudo. Por quadro, o canvas so desloca e recorta o que guardou.

#include "spriteui_host.hpp"
#include "text_atlas_set.hpp"

#include "sui_palette.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
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
    fontScale_ = std::clamp(scale, 0.5f, 2.f);
    lineHeightValid_ = false;
  }

  void clear() {
    prims_.clear();
    texts_.clear();
  }

  [[nodiscard]] ST::TextLayoutResult layout(const std::string& utf8, float fontSize) const {
    fontSize *= fontScale_;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(fontSize);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = fontSize;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::None;
    return ST::LayoutText(utf8, ST::ShapeTruetype(utf8, atlas), atlas.metrics, cfg);
  }

  [[nodiscard]] float lineHeight(float fontSize) const {
    if (lineHeightValid_ && lineHeightSize_ == fontSize) return lineHeight_;
    lineHeightSize_ = fontSize;
    lineHeight_ = layout("Ag", fontSize).lineHeight;
    lineHeightValid_ = true;
    return lineHeight_;
  }

  // Texto de largura limitada (cortado com "..."). Retorna a largura desenhada, para alinhar o que vem depois.
  float addText(float x, float y, const std::string& text, float fontSize, SU::Vec4 color, float maxWidth, bool alignRight = false) {
    if (text.empty() || maxWidth <= 0.f) return 0.f;
    ST::TextLayoutResult lay = layout(fit(text, fontSize, maxWidth), fontSize);
    const float w = lay.size.x;
    pushText(alignRight ? x - w : x, y, std::move(lay), color, text_.sliceFor(fontSize * fontScale_));
    return w;
  }

  // Paragrafo quebrado por palavras dentro de `width`. Retorna a altura ocupada.
  float addParagraph(float x, float y, const std::string& text, float fontSize, SU::Vec4 color, float width) {
    fontSize *= fontScale_;
    const ST::TruetypeAtlas& atlas = text_.atlasFor(fontSize);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = fontSize;
    cfg.maxWidth = width;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::Word;
    ST::TextLayoutResult lay = ST::LayoutText(text, ST::ShapeTruetype(text, atlas), atlas.metrics, cfg);
    const float height = lay.size.y;
    pushText(x, y, std::move(lay), color, text_.sliceFor(fontSize));
    return height;
  }

  // Z dos textos deste canvas (padrao 0.9). Um canvas de menu aberto usa um Z maior, por cima dos outros.
  void setTextZ(float z) { textZ_ = z; }

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
      auto items = ST::GenerateTextRenderItems(texts_[p.text], box, p.color, p.slice, clip, p.z, paint, owner, 0u);
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
    enum class Kind : std::uint8_t { Solid, Text };
    Kind kind = Kind::Solid;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    SU::Vec4 color{};
    float z = 0.f;
    float cornerRadius = 0.f;
    std::uint32_t text = 0;   // Text: indice em texts_
    std::uint32_t slice = 1;  // Text: slice de textura do tamanho (TextAtlasSet)
  };

  void pushText(float x, float y, ST::TextLayoutResult lay, SU::Vec4 color, std::uint32_t slice) {
    Prim p;
    p.kind = Prim::Kind::Text;
    p.x = x;
    p.y = y;
    p.w = lay.size.x;
    p.h = lay.size.y;
    p.color = color;
    p.z = textZ_;
    p.text = static_cast<std::uint32_t>(texts_.size());
    p.slice = slice;
    texts_.push_back(std::move(lay));
    prims_.push_back(p);
  }

  // Corta o texto com "..." para caber em maxWidth (sem cortar um caractere UTF-8 ao meio).
  std::string fit(const std::string& s, float fontSize, float maxWidth) const {
    const float full = layout(s, fontSize).size.x;
    if (full <= maxWidth) return s;
    std::size_t cut = std::min(s.size(), static_cast<std::size_t>(std::floor(static_cast<float>(s.size()) * maxWidth / std::max(1.f, full))));
    while (cut > 0) {
      while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
      const std::string candidate = s.substr(0, cut) + "...";
      if (layout(candidate, fontSize).size.x <= maxWidth) return candidate;
      --cut;
    }
    return "...";
  }

  const TextAtlasSet& text_;
  float textZ_ = 0.9f;
  float fontScale_ = 1.f;
  mutable float lineHeightSize_ = -1.f;
  mutable float lineHeight_ = 0.f;
  mutable bool lineHeightValid_ = false;
  std::vector<Prim> prims_;
  std::vector<ST::TextLayoutResult> texts_;
};

// Handle valido e unico para o indice i (a geracao 1 nunca e zero).
inline SU::UIHandle handleFor(std::uint32_t index) { return SU::UIHandle{index, 1u}; }

}  // namespace editor::sui
