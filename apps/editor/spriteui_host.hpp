#pragma once

// Ponte compartilhada entre a SpriteUI e o SDL_Renderer: a tela de inicio e as janelas do editor usam
// o mesmo caminho de desenho (FramePacket -> SDL_RenderGeometry) e o mesmo carregamento de fonte.
//
// Regras:
//  - Nada daqui depende de ImGui nem de tipos do editor: so SpriteUI, SDL e a biblioteca padrao.
//  - A SpriteUI continua independente da engine; quem liga os dois e o editor (este arquivo e apps/editor).
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace editor::sui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

// --------------------------------------------------------------------------------------------------------------
// Ligacao com o SDL_Renderer: converte o FramePacket em SDL_RenderGeometry, um lote por vez, com recorte e atlas.
// --------------------------------------------------------------------------------------------------------------
// textureSlice dos sprites de icone (ver uploadIcons). 0 = pixel branco, 1 = glifos.
inline constexpr std::uint32_t kIconSlice = 2u;
// Texturas de vista (imagem da Scene e do Jogo): slices a partir de kViewSliceBase, uma por slot. O painel liga o
// SDL_Texture ao slot antes de desenhar; a Image da SpriteUI usa o slice do slot.
inline constexpr std::uint32_t kViewSliceBase = 8u;
inline constexpr std::uint32_t kViewSlots = 4u;

class SpriteUiSdlRenderer {
 public:
  ~SpriteUiSdlRenderer() { destroy(); }

  // Sobe o atlas de glifos como textura RGBA (cor branca, alfa = cobertura).
  bool uploadAtlas(SDL_Renderer* renderer, const ST::TruetypeAtlas& atlas) {
    if (atlas_) SDL_DestroyTexture(atlas_);   // so o de glifos: o atlas de icones (uploadIcons) fica
    atlas_ = nullptr;
    if (!renderer || !atlas.IsValid()) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(atlas.width) * atlas.height * 4u, 255u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(atlas.width) * atlas.height; ++i) {
      rgba[i * 4 + 3] = atlas.pixels[i];
    }
    atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                               static_cast<int>(atlas.width), static_cast<int>(atlas.height));
    if (!atlas_) return false;
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_LINEAR);
    return SDL_UpdateTexture(atlas_, nullptr, rgba.data(), static_cast<int>(atlas.width * 4u));
  }

  // Sobe o atlas de icones (RGBA, ja com alfa) como segunda textura, usada pelo textureSlice kIconSlice.
  bool uploadIcons(SDL_Renderer* renderer, int width, int height, const std::uint8_t* rgba) {
    if (icons_) SDL_DestroyTexture(icons_);
    icons_ = nullptr;
    if (!renderer || width <= 0 || height <= 0 || !rgba) return false;
    icons_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!icons_) return false;
    SDL_SetTextureBlendMode(icons_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(icons_, SDL_SCALEMODE_LINEAR);
    return SDL_UpdateTexture(icons_, nullptr, rgba, width * 4);
  }

  // Liga a textura de um slot de vista (ou nullptr para soltar). Slots fora da faixa sao ignorados.
  void setView(std::uint32_t slot, SDL_Texture* texture) {
    if (slot < kViewSlots) views_[slot] = texture;
  }

  bool draw(SDL_Renderer* renderer, const SR::FramePacket& packet) {
    if (!renderer) return false;
    for (const auto& batch : packet.batches) {
      if (!setClip(renderer, packet, batch.clipIndex)) return false;
      vertices_.clear();
      indices_.clear();
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& inst = packet.instances[batch.firstInstance + i];
        const SDL_FColor color = sdlColor(inst);
        const bool textured = batch.textureSlice != SR::WhiteTextureSlice;
        const SDL_FPoint p[4] = {transform(inst, 0.f, 0.f), transform(inst, 1.f, 0.f),
                                 transform(inst, 1.f, 1.f), transform(inst, 0.f, 1.f)};
        const SDL_FPoint uv[4] = {uvAt(inst, 0.f, 0.f, textured), uvAt(inst, 1.f, 0.f, textured),
                                  uvAt(inst, 1.f, 1.f, textured), uvAt(inst, 0.f, 1.f, textured)};
        const int base = static_cast<int>(vertices_.size());
        for (int k = 0; k < 4; ++k) vertices_.push_back({p[k], color, uv[k]});
        indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      }
      if (vertices_.empty()) continue;
      SDL_Texture* texture = textureFor(batch.textureSlice);
      if (!SDL_RenderGeometry(renderer, texture, vertices_.data(), static_cast<int>(vertices_.size()),
                              indices_.data(), static_cast<int>(indices_.size()))) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }
    }
    return SDL_SetRenderClipRect(renderer, nullptr);
  }

  void destroy() {
    if (atlas_) SDL_DestroyTexture(atlas_);
    if (icons_) SDL_DestroyTexture(icons_);
    atlas_ = nullptr;
    icons_ = nullptr;
  }

 private:
  SDL_Texture* textureFor(std::uint32_t slice) const {
    if (slice == SR::WhiteTextureSlice) return nullptr;
    if (slice == kIconSlice) return icons_;
    if (slice >= kViewSliceBase && slice - kViewSliceBase < kViewSlots) return views_[slice - kViewSliceBase];
    return atlas_;
  }

  static bool setClip(SDL_Renderer* renderer, const SR::FramePacket& packet, std::uint32_t clipIndex) {
    if (clipIndex == SR::NoClipIndex || clipIndex >= packet.clipRects.size()) return SDL_SetRenderClipRect(renderer, nullptr);
    const auto& clip = packet.clipRects[clipIndex];
    const int left = static_cast<int>(std::floor(clip.minX));
    const int top = static_cast<int>(std::floor(clip.minY));
    const int right = static_cast<int>(std::ceil(clip.maxX));
    const int bottom = static_cast<int>(std::ceil(clip.maxY));
    const SDL_Rect rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    return SDL_SetRenderClipRect(renderer, &rect);
  }

  static SDL_FPoint transform(const SR::SpriteInstanceGPU& inst, float u, float v) {
    // Retangulo local [0,1]^2 -> tela: linha 0 = (m00, m01, tx), linha 1 = (m10, m11, ty).
    const float lx = u, ly = v;
    return {inst.transformRow0[0] * lx + inst.transformRow0[1] * ly + inst.transformRow0[2],
            inst.transformRow1[0] * lx + inst.transformRow1[1] * ly + inst.transformRow1[2]};
  }

  static SDL_FPoint uvAt(const SR::SpriteInstanceGPU& inst, float u, float v, bool textured) {
    if (!textured) return {0.f, 0.f};
    return {inst.uvRect[0] + u * inst.uvRect[2], inst.uvRect[1] + v * inst.uvRect[3]};
  }

  // Cor do item: tint linear premultiplicada -> sRGB reto (SDL mistura com alfa reto).
  static SDL_FColor sdlColor(const SR::SpriteInstanceGPU& inst) {
    const float a = inst.tint[3];
    if (a <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    return {encode(inst.tint[0] / a), encode(inst.tint[1] / a), encode(inst.tint[2] / a), a};
  }
  static float encode(float linear) {
    linear = std::clamp(linear, 0.f, 1.f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
  }

  SDL_Texture* atlas_ = nullptr;
  SDL_Texture* icons_ = nullptr;
  SDL_Texture* views_[kViewSlots] = {};
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
};


// Carrega a fonte e monta o atlas de glifos (ASCII, Latin-1 e Latin Extended-A, para o portugues).
inline bool LoadTextAtlas(const std::string& fontPath, ST::TruetypeAtlas& out) {
  std::ifstream file(fontPath, std::ios::binary);
  if (!file) return false;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  ST::TruetypeAtlasConfig cfg;
  cfg.rasterPixelSize = 32.f;
  cfg.atlasWidth = 1024;
  cfg.atlasHeight = 1024;
  cfg.firstCodepoint = 0x20;
  cfg.lastCodepoint = 0x17F;
  return ST::BuildTruetypeAtlas(bytes, cfg, out);
}


} // namespace editor::sui
