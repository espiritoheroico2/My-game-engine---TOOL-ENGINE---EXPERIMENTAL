#pragma once

// Conjunto de atlas de glifos por tamanho de texto (a ideia do TextMeshPro, em bitmap). Cada tamanho em pixels tem
// o seu atlas, rasterizado no tamanho exato, sem reduzir. Assim o contorno cai em pixel inteiro, as letras ficam na
// mesma linha de base e o texto fica nitido. O renderizador sobe uma textura por tamanho, na primeira vez que usa.
//
// Cada tamanho usa um slice proprio (kFirstSlice + pixels - kMinPx). Slice 1 continua sendo o atlas padrao.
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace editor::sui {

namespace ST = ToolEngine::SpriteUi::Text;

class TextAtlasSet {
 public:
  static constexpr int kMinPx = 8;    // menor atlas
  static constexpr int kMaxPx = 64;   // maior atlas
  static constexpr std::uint32_t kSlots = static_cast<std::uint32_t>(kMaxPx - kMinPx + 1);
  static constexpr std::uint32_t kFirstSlice = 32u;   // slices de texto: [kFirstSlice, kFirstSlice + kSlots)

  // Le o arquivo da fonte. displayScale: escala da janela; o atlas usa tamanho * escala pixels reais.
  bool load(const std::string& fontPath, float displayScale) {
    std::ifstream file(fontPath, std::ios::binary);
    if (!file) return false;
    bytes_.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    scale_ = (std::isfinite(displayScale) && displayScale > 0.f) ? displayScale : 1.f;
    slots_.clear();
    slots_.resize(kSlots);
    return !bytes_.empty();
  }

  [[nodiscard]] bool ready() const { return !bytes_.empty(); }

  // Atlas do tamanho logico pedido (em pixels). Constroi na primeira vez que o tamanho aparece.
  [[nodiscard]] const ST::TruetypeAtlas& atlasFor(float size) const {
    const std::uint32_t slot = slotFor(size);
    if (!slots_[slot]) slots_[slot] = std::make_unique<ST::TruetypeAtlas>(build(pixelsFor(size)));
    return *slots_[slot];
  }

  // Slice de textura do tamanho pedido (mesmo critério de atlasFor).
  [[nodiscard]] std::uint32_t sliceFor(float size) const { return kFirstSlice + slotFor(size); }

  // Atlas de um slice de texto, ou nullptr se o slice nao e de texto. Usado pelo renderizador para subir a textura.
  [[nodiscard]] const ST::TruetypeAtlas* atlasForSlice(std::uint32_t slice) const {
    if (slice < kFirstSlice || slice >= kFirstSlice + kSlots) return nullptr;
    const std::uint32_t slot = slice - kFirstSlice;
    if (!slots_[slot]) slots_[slot] = std::make_unique<ST::TruetypeAtlas>(build(kMinPx + static_cast<int>(slot)));
    return slots_[slot].get();
  }

 private:
  // Pixels reais do atlas para um tamanho logico, limitados a [kMinPx, kMaxPx].
  [[nodiscard]] int pixelsFor(float size) const {
    const float px = std::round((std::isfinite(size) ? size : 16.f) * scale_);
    return std::clamp(static_cast<int>(px), kMinPx, kMaxPx);
  }
  [[nodiscard]] std::uint32_t slotFor(float size) const { return static_cast<std::uint32_t>(pixelsFor(size) - kMinPx); }

  [[nodiscard]] ST::TruetypeAtlas build(int px) const {
    ST::TruetypeAtlasConfig cfg;
    cfg.rasterPixelSize = static_cast<float>(px);
    // Tamanho da pagina cresce com o tamanho do glifo: 512 ate 20 px, 1024 ate 40 px, 2048 acima.
    cfg.atlasWidth = px <= 20 ? 512u : (px <= 40 ? 1024u : 2048u);
    cfg.atlasHeight = cfg.atlasWidth;
    cfg.coverageGamma = 0.5f;   // mesmo ajuste do atlas padrao (SDL mistura em sRGB)
    cfg.firstCodepoint = 0x20;
    cfg.lastCodepoint = 0x17F;
    ST::TruetypeAtlas out;
    ST::BuildTruetypeAtlas(bytes_, cfg, out);
    return out;
  }

  std::vector<std::uint8_t> bytes_;
  float scale_ = 1.f;
  mutable std::vector<std::unique_ptr<ST::TruetypeAtlas>> slots_;
};

}  // namespace editor::sui
