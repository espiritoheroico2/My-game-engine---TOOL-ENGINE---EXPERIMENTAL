#pragma once

// Ponte compartilhada entre a SpriteUI e o SDL_Renderer: a tela de inicio e as janelas do editor usam
// o mesmo caminho de desenho (FramePacket -> SDL_RenderGeometry) e o mesmo carregamento de fonte.
//
// Regras:
//  - Nada daqui depende de tipos do editor: so SpriteUI, SDL e a biblioteca padrao.
//  - A SpriteUI continua independente da engine; quem liga os dois e o editor (este arquivo e apps/editor).
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/BitmapRasterPolicy.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "text_atlas_set.hpp"
#include "ui_gpu_sink.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
// Atlas das miniaturas de material da janela Assets (material_preview.hpp): grade de kMaterialCols x kMaterialCols
// miniaturas de kMaterialCell pixels; textura criada na primeira miniatura enviada.
inline constexpr std::uint32_t kMaterialSlice = 3u;
inline constexpr int kMaterialCols = 8;
inline constexpr int kMaterialCell = 128;
// Texturas de vista (imagem da Scene e do Jogo): slices a partir de kViewSliceBase, uma por slot. O painel liga o
// SDL_Texture ao slot antes de desenhar; a Image da SpriteUI usa o slice do slot.
inline constexpr std::uint32_t kViewSliceBase = 8u;
inline constexpr std::uint32_t kViewSlots = 4u;

// Contador de chamadas SDL_RenderGeometry feitas pela SpriteUI (todos os renderizadores). Medicao de draw calls:
// tool_editor --frames N imprime a media por quadro no fim.
inline std::uint64_t& SpriteUiDrawCalls() {
  static std::uint64_t calls = 0;
  return calls;
}

class SpriteUiSdlRenderer {
 public:
  ~SpriteUiSdlRenderer() { destroy(); }

  // Backend GPU (Diligent): quando anexado, tudo que o SDL desenhava (pacotes, atlas, icones, miniaturas, vistas) vai para o
  // sink. As copias em CPU abaixo (glifos, icones, miniaturas) existem para recriar as texturas depois de uma troca de
  // API/backend sem que os paineis precisem reenviar nada. Sem backend, o caminho SDL de sempre.
  void attachGpu(UiGpuSink* sink) {
    destroy();   // texturas SDL (se havia renderer) saem; as copias em CPU ficam
    gpu_ = sink;
    cacheValid_ = false;
    if (!gpu_) return;
    if (glyphW_ > 0) gpu_->setSlice(1u, glyphW_, glyphH_, glyphRgba_.data(), true);
    if (iconsW_ > 0) gpu_->setSlice(kIconSlice, iconsW_, iconsH_, iconsRgba_.data(), true);
    if (!materialsRgba_.empty()) gpu_->setSlice(kMaterialSlice, kMaterialCols * kMaterialCell, kMaterialCols * kMaterialCell, materialsRgba_.data(), true);
    // texto por tamanho e vistas: recriados sob demanda / religados pelos paineis a cada quadro
  }
  void detachGpu() { gpu_ = nullptr; cacheValid_ = false; }
  [[nodiscard]] UiGpuSink* gpu() const { return gpu_; }

  // Sobe o atlas de glifos como textura RGBA (cor branca, alfa = cobertura).
  bool uploadAtlas(SDL_Renderer* renderer, const ST::TruetypeAtlas& atlas) {
    if (atlas_) SDL_DestroyTexture(atlas_);   // so o de glifos: o atlas de icones (uploadIcons) fica
    atlas_ = nullptr;
    cacheValid_ = false;   // o conteudo do atlas mudou sem mudar o pacote: a imagem em cache nao serve mais
    glyphW_ = glyphH_ = 0;
    glyphRgba_.clear();
    if (atlas.IsValid()) {
      glyphRgba_ = glyphRgba(atlas);
      glyphW_ = static_cast<int>(atlas.width);
      glyphH_ = static_cast<int>(atlas.height);
    }
    if (gpu_) return glyphW_ > 0 && gpu_->setSlice(1u, glyphW_, glyphH_, glyphRgba_.data(), true);
    if (!renderer) return false;
    atlas_ = createGlyphTexture(renderer, atlas);
    return atlas_ != nullptr;
  }

  // Conjunto de atlas por tamanho: as texturas sobem sob demanda, uma por slice de texto.
  void setTextSet(const TextAtlasSet* set) {
    textSet_ = set;
    cacheValid_ = false;   // outro conjunto de fontes: a imagem em cache nao vale mais
  }

  // Sobe o atlas de icones (RGBA, ja com alfa) como segunda textura, usada pelo textureSlice kIconSlice.
  bool uploadIcons(SDL_Renderer* renderer, int width, int height, const std::uint8_t* rgba) {
    if (icons_) SDL_DestroyTexture(icons_);
    icons_ = nullptr;
    cacheValid_ = false;   // icones novos: a imagem em cache precisa ser refeita
    iconsW_ = iconsH_ = 0;
    iconsRgba_.clear();
    if (width <= 0 || height <= 0 || !rgba) return false;
    iconsRgba_.assign(rgba, rgba + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    iconsW_ = width;
    iconsH_ = height;
    if (gpu_) return gpu_->setSlice(kIconSlice, width, height, iconsRgba_.data(), true);
    if (!renderer) return false;
    return createIconsTexture(renderer);
  }

  // Envia uma miniatura de material (kMaterialCell x kMaterialCell RGBA) para o slot `slot` do atlas de materiais.
  // Cria o atlas (estatico, vazio) na primeira chamada. Slot fora da grade e ignorado.
  bool uploadMaterialThumb(SDL_Renderer* renderer, int slot, const std::uint8_t* rgba) {
    if ((!renderer && !gpu_) || !rgba || slot < 0 || slot >= kMaterialCols * kMaterialCols) return false;
    constexpr int kSide = kMaterialCols * kMaterialCell;
    if (materialsRgba_.empty()) materialsRgba_.assign(static_cast<std::size_t>(kSide) * kSide * 4u, 0u);   // copia em CPU: sobrevive a troca de backend
    const int ox = (slot % kMaterialCols) * kMaterialCell, oy = (slot / kMaterialCols) * kMaterialCell;
    for (int y = 0; y < kMaterialCell; ++y) {
      std::memcpy(materialsRgba_.data() + (static_cast<std::size_t>(oy + y) * kSide + static_cast<std::size_t>(ox)) * 4u,
                  rgba + static_cast<std::size_t>(y) * kMaterialCell * 4u, static_cast<std::size_t>(kMaterialCell) * 4u);
    }
    if (gpu_) return gpu_->setSlice(kMaterialSlice, kSide, kSide, materialsRgba_.data(), true);   // textura inteira (sem envio parcial no RHI)
    if (!materials_ && !createMaterialsTexture(renderer)) return false;
    const SDL_Rect r{ox, oy, kMaterialCell, kMaterialCell};
    return SDL_UpdateTexture(materials_, &r, rgba, kMaterialCell * 4);
  }

  // Liga a textura de um slot de vista (ou nullptr para soltar). Slots fora da faixa sao ignorados.
  void setView(std::uint32_t slot, SDL_Texture* texture) {
    if (slot < kViewSlots) views_[slot] = texture;
  }
  // Versao com o handle das vistas: SDL_Texture no caminho SDL, id dinamico + filtro no caminho GPU.
  void setView(std::uint32_t slot, const ViewImage& image) {
    if (slot >= kViewSlots) return;
    if (gpu_) gpu_->bindView(slot, image.gpu, image.linear);
    else views_[slot] = image.sdl;
  }

  // Desenho com cache por painel: se o pacote e identico ao do quadro anterior (e a janela nao mudou de tamanho),
  // a imagem ja pronta numa textura-alvo e so copiada para a tela, sem gerar vertices nem chamar SDL_RenderGeometry.
  // Pacotes que leem texturas que mudam sem mudar o pacote (miniaturas de material e vistas Scene/Jogo) nao usam o
  // cache: caem em drawDirect todo quadro. A ordem de desenho entre paineis nao muda: cada um copia no seu turno.
  bool draw(SDL_Renderer* renderer, const SR::FramePacket& packet) {
    if (gpu_) return drawGpu(packet);   // GPU: sem cache por painel (o custo e so gravar vertices; ver docs/planos/38, etapa 3e)
    if (!renderer) return false;
    if (usesLiveTexture(packet)) {
      cacheValid_ = false;
      return drawDirect(renderer, packet);
    }
    int w = 0, h = 0;
    if (!SDL_GetRenderOutputSize(renderer, &w, &h) || w <= 0 || h <= 0) return drawDirect(renderer, packet);
    const std::uint64_t key = hashPacket(packet, static_cast<std::uint64_t>(w) << 32 | static_cast<std::uint64_t>(h));
    if (!cacheValid_ || key != cacheKey_ || !cacheTex_ || w != cacheW_ || h != cacheH_) {
      if (!ensureCacheTexture(renderer, w, h)) return drawDirect(renderer, packet);
      SDL_Texture* previous = SDL_GetRenderTarget(renderer);
      SDL_SetRenderTarget(renderer, cacheTex_);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
      SDL_RenderClear(renderer);
      const bool ok = drawDirect(renderer, packet);   // desenho real, no alvo (nao na tela)
      SDL_SetRenderTarget(renderer, previous);
      cacheValid_ = ok;
      cacheKey_ = key;
      if (!ok) return false;
      ++cacheMisses_;
    } else {
      ++cacheHits_;
    }
    // Copia para a tela. A textura guarda cor ja multiplicada pelo alfa (ver ensureCacheTexture): mistura premultiplicada.
    activeClipIndex_ = SR::NoClipIndex;
    SDL_SetRenderClipRect(renderer, nullptr);
    return SDL_RenderTexture(renderer, cacheTex_, nullptr, nullptr);
  }

  // Contadores do cache (acertos e erros por quadro de painel): usados nos testes e para medir o ganho.
  [[nodiscard]] std::uint64_t cacheHits() const { return cacheHits_; }
  [[nodiscard]] std::uint64_t cacheMisses() const { return cacheMisses_; }

  // Desenho direto (sem cache): gera os vertices do pacote e envia lote por lote para o renderizador atual.
  bool drawDirect(SDL_Renderer* renderer, const SR::FramePacket& packet) {
    if (gpu_) return drawGpu(packet);
    if (!renderer) return false;
    activeClipIndex_ = SR::NoClipIndex;
    for (const auto& batch : packet.batches) {
      if (!setClip(renderer, packet, batch.clipIndex)) return false;
      vertices_.clear();
      indices_.clear();
      std::size_t vertexReserve = 0;
      std::size_t indexReserve = 0;
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& inst = packet.instances[batch.firstInstance + i];
        if (inst.tint[3] <= 0.f) continue;
        if (isRounded(inst)) {
          vertexReserve += static_cast<std::size_t>(kCornerSegments * 4 + 5);
          indexReserve += static_cast<std::size_t>((kCornerSegments + 1) * 4 * 3);
        } else {
          vertexReserve += 4;
          indexReserve += 6;
        }
      }
      vertices_.reserve(vertexReserve);
      indices_.reserve(indexReserve);
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& inst = packet.instances[batch.firstInstance + i];
        // Alfa zero e invisivel por definicao. Sem este pulo, o SDL desenhava preto nas barras "passivas".
        if (inst.tint[3] <= 0.f) continue;
        const SDL_FColor color = sdlColor(inst);
        const bool textured = batch.textureSlice != SR::WhiteTextureSlice;
        if (isRounded(inst)) {
          appendRoundedFan(inst, color, textured);
          continue;
        }
        const SDL_FPoint p[4] = {transform(inst, 0.f, 0.f), transform(inst, 1.f, 0.f),
                                 transform(inst, 1.f, 1.f), transform(inst, 0.f, 1.f)};
        const SDL_FPoint uv[4] = {uvAt(inst, 0.f, 0.f, textured), uvAt(inst, 1.f, 0.f, textured),
                                  uvAt(inst, 1.f, 1.f, textured), uvAt(inst, 0.f, 1.f, textured)};
        const int base = static_cast<int>(vertices_.size());
        for (int k = 0; k < 4; ++k) vertices_.push_back({p[k], color, uv[k]});
        indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      }
      if (vertices_.empty()) continue;
      SDL_Texture* texture = textureFor(renderer, batch.textureSlice);
      // Sem isso o SDL usa BLENDMODE_NONE para geometria sem textura e ignora o alfa (tudo sai opaco).
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      if (!SDL_RenderGeometry(renderer, texture, vertices_.data(), static_cast<int>(vertices_.size()),
                              indices_.data(), static_cast<int>(indices_.size()))) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }
      ++SpriteUiDrawCalls();
    }
    return SDL_SetRenderClipRect(renderer, nullptr);
  }

  // Cor do item: tint linear premultiplicada -> sRGB reto (SDL mistura com alfa reto). O backend GPU usa a mesma conversao
  // para o resultado bater com o do SDL.
  static SDL_FColor sdlColor(const SR::SpriteInstanceGPU& inst) {
    const float a = inst.tint[3];
    if (a <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    return {encode(inst.tint[0] / a), encode(inst.tint[1] / a), encode(inst.tint[2] / a), a};
  }

  // Libera as texturas SDL (as copias em CPU de glifos, icones e miniaturas ficam: ver attachGpu).
  void destroy() {
    if (cacheTex_) SDL_DestroyTexture(cacheTex_);
    cacheTex_ = nullptr;
    cacheValid_ = false;
    if (atlas_) SDL_DestroyTexture(atlas_);
    if (icons_) SDL_DestroyTexture(icons_);
    if (materials_) SDL_DestroyTexture(materials_);
    materials_ = nullptr;
    for (SDL_Texture*& t : sizeTex_) {
      if (t) SDL_DestroyTexture(t);
      t = nullptr;
    }
    atlas_ = nullptr;
    icons_ = nullptr;
    for (SDL_Texture*& v : views_) v = nullptr;   // as vistas sao de quem as criou (ViewTex); aqui so se solta a referencia
  }

 private:
  // true se o pacote usa uma textura cujo conteudo muda sem mudar o pacote (miniatura de material ou vista de cena/jogo).
  static bool usesLiveTexture(const SR::FramePacket& packet) {
    for (const auto& batch : packet.batches) {
      if (batch.textureSlice == kMaterialSlice) return true;
      if (batch.textureSlice >= kViewSliceBase && batch.textureSlice - kViewSliceBase < kViewSlots) return true;
    }
    return false;
  }

  // Hash FNV-1a por palavras de 64 bits sobre instancias, lotes e recortes, com a tela (w,h) como semente.
  // Erro de hash so custa um redesenho; nunca mostra imagem antiga por engano em caso de colisao rara.
  static std::uint64_t hashPacket(const SR::FramePacket& packet, std::uint64_t seed) {
    std::uint64_t h = 1469598103934665603ull ^ seed;
    const auto mix = [&h](const void* data, std::size_t bytes) {
      const auto* p = static_cast<const unsigned char*>(data);
      std::size_t i = 0;
      for (; i + 8 <= bytes; i += 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, p + i, 8);
        h = (h ^ word) * 1099511628211ull;
      }
      for (; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
    };
    mix(packet.instances.data(), packet.instances.size() * sizeof(SR::SpriteInstanceGPU));
    mix(packet.batches.data(), packet.batches.size() * sizeof(SR::DrawBatch));
    mix(packet.clipRects.data(), packet.clipRects.size() * sizeof(SR::ClipRectGPU));
    return h;
  }

  // Textura-alvo do cache, com o tamanho da saida. Mistura premultiplicada ao copiar para a tela: o desenho no alvo
  // usa BLEND (cor*alfa somada ao destino), e isso produz cor ja premultiplicada.
  bool ensureCacheTexture(SDL_Renderer* renderer, int w, int h) {
    if (cacheTex_ && w == cacheW_ && h == cacheH_) return true;
    if (cacheTex_) SDL_DestroyTexture(cacheTex_);
    cacheTex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    cacheW_ = cacheH_ = 0;
    cacheValid_ = false;
    if (!cacheTex_) return false;
    SDL_SetTextureBlendMode(cacheTex_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    cacheW_ = w;
    cacheH_ = h;
    return true;
  }

  // Glifos: RGBA branco com alfa = cobertura.
  static std::vector<std::uint8_t> glyphRgba(const ST::TruetypeAtlas& atlas) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(atlas.width) * atlas.height * 4u, 255u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(atlas.width) * atlas.height; ++i) {
      rgba[i * 4 + 3] = atlas.pixels[i];
    }
    return rgba;
  }

  static SDL_Texture* createGlyphTexture(SDL_Renderer* renderer, const ST::TruetypeAtlas& atlas) {
    if (!atlas.IsValid()) return nullptr;
    const std::vector<std::uint8_t> rgba = glyphRgba(atlas);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                         static_cast<int>(atlas.width), static_cast<int>(atlas.height));
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    if (!SDL_UpdateTexture(tex, nullptr, rgba.data(), static_cast<int>(atlas.width * 4u))) {
      SDL_DestroyTexture(tex);
      return nullptr;
    }
    return tex;
  }

  // Recria a textura SDL dos icones a partir da copia em CPU (primeiro envio ou depois de trocar o renderer).
  bool createIconsTexture(SDL_Renderer* renderer) {
    if (iconsW_ <= 0 || iconsH_ <= 0 || iconsRgba_.empty()) return false;
    icons_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, iconsW_, iconsH_);
    if (!icons_) return false;
    SDL_SetTextureBlendMode(icons_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(icons_, SDL_SCALEMODE_LINEAR);
    return SDL_UpdateTexture(icons_, nullptr, iconsRgba_.data(), iconsW_ * 4);
  }

  // Cria a textura SDL do atlas de materiais com o conteudo da copia em CPU.
  bool createMaterialsTexture(SDL_Renderer* renderer) {
    constexpr int kSide = kMaterialCols * kMaterialCell;
    materials_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kSide, kSide);
    if (!materials_) return false;
    SDL_SetTextureBlendMode(materials_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(materials_, SDL_SCALEMODE_LINEAR);
    if (!materialsRgba_.empty()) SDL_UpdateTexture(materials_, nullptr, materialsRgba_.data(), kSide * 4);
    return true;
  }

  // Desenho pelo backend GPU: garante as texturas de texto dos slices usados e grava o pacote no quadro.
  bool drawGpu(const SR::FramePacket& packet) {
    for (const auto& batch : packet.batches) {
      const std::uint32_t slice = batch.textureSlice;
      if (slice < TextAtlasSet::kFirstSlice || slice >= TextAtlasSet::kFirstSlice + TextAtlasSet::kSlots) continue;
      if (gpu_->hasSlice(slice) || !textSet_) continue;
      if (const ST::TruetypeAtlas* a = textSet_->atlasForSlice(slice)) {
        if (a->IsValid()) {
          const std::vector<std::uint8_t> rgba = glyphRgba(*a);
          gpu_->setSlice(slice, static_cast<int>(a->width), static_cast<int>(a->height), rgba.data(), true);
        }
      }
    }
    return gpu_->drawPacket(packet);
  }

  // Textura do slice. Slices de texto por tamanho sobem na primeira vez; os demais seguem a tabela fixa.
  SDL_Texture* textureFor(SDL_Renderer* renderer, std::uint32_t slice) {
    if (slice == SR::WhiteTextureSlice) return nullptr;
    if (slice == kIconSlice) {
      if (!icons_ && iconsW_ > 0) createIconsTexture(renderer);   // renderer novo: recria da copia em CPU
      return icons_;
    }
    if (slice == kMaterialSlice) {
      if (!materials_ && !materialsRgba_.empty()) createMaterialsTexture(renderer);
      return materials_;
    }
    if (slice >= kViewSliceBase && slice - kViewSliceBase < kViewSlots) return views_[slice - kViewSliceBase];
    if (slice >= TextAtlasSet::kFirstSlice && slice < TextAtlasSet::kFirstSlice + TextAtlasSet::kSlots) {
      SDL_Texture*& tex = sizeTex_[slice - TextAtlasSet::kFirstSlice];
      if (!tex && textSet_) {
        if (const ST::TruetypeAtlas* a = textSet_->atlasForSlice(slice)) tex = createGlyphTexture(renderer, *a);
      }
      return tex;
    }
    if (!atlas_ && glyphW_ > 0) {   // renderer novo: o atlas de glifos volta da copia em CPU
      atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, glyphW_, glyphH_);
      if (atlas_) {
        SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(atlas_, nullptr, glyphRgba_.data(), glyphW_ * 4);
      }
    }
    return atlas_;
  }

  bool setClip(SDL_Renderer* renderer, const SR::FramePacket& packet, std::uint32_t clipIndex) {
    if (clipIndex == activeClipIndex_) return true;
    activeClipIndex_ = clipIndex;
    if (clipIndex == SR::NoClipIndex || clipIndex >= packet.clipRects.size()) return SDL_SetRenderClipRect(renderer, nullptr);
    const auto& clip = packet.clipRects[clipIndex];
    const int left = static_cast<int>(std::floor(clip.minX));
    const int top = static_cast<int>(std::floor(clip.minY));
    const int right = static_cast<int>(std::ceil(clip.maxX));
    const int bottom = static_cast<int>(std::ceil(clip.maxY));
    const SDL_Rect rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    return SDL_SetRenderClipRect(renderer, &rect);
  }

  // Retangulo com cantos arredondados (FlagRoundedPanel e raio positivo). O SDL nao tem o SDF do shader, entao o
  // contorno vira um leque: o centro e os pontos dos quatro arcos, kCornerSegments por canto.
  // O raio vem de transformRow1[3], em pixels logicos; o item e so uma caixa com a matriz do retangulo.
  static bool isRounded(const SR::SpriteInstanceGPU& inst) {
    return (inst.resourceIds[3] & SR::FlagRoundedPanel) != 0u && inst.transformRow1[3] > 0.f;
  }

  void appendRoundedFan(const SR::SpriteInstanceGPU& inst, SDL_FColor color, bool textured) {
    const float width = std::hypot(inst.transformRow0[0], inst.transformRow1[0]);
    const float height = std::hypot(inst.transformRow0[1], inst.transformRow1[1]);
    if (!(width > 0.f) || !(height > 0.f)) return;
    // Raio em fracao do retangulo local (no maximo 0,5 em cada eixo).
    const float rx = std::min(0.5f, inst.transformRow1[3] / width);
    const float ry = std::min(0.5f, inst.transformRow1[3] / height);
    // Arcos no sentido horario da tela: centro do arco e angulo inicial; cada arco gira um quarto de volta.
    struct Corner {
      float cx, cy;
    };
    const Corner corners[4] = {
        {rx, ry},
        {1.f - rx, ry},
        {1.f - rx, 1.f - ry},
        {rx, 1.f - ry},
    };
    const int base = static_cast<int>(vertices_.size());
    const auto addVertex = [&](float u, float v) {
      vertices_.push_back({transform(inst, u, v), color, uvAt(inst, u, v, textured)});
    };
    addVertex(0.5f, 0.5f);
    static const auto arc = [] {
      std::array<std::array<float, 2>, kCornerSegments * 4 + 4> values{};
      const float starts[4] = {kPi, kPi + kHalfPi, 0.f, kHalfPi};
      std::size_t index = 0;
      for (float start : starts) {
        for (int k = 0; k <= kCornerSegments; ++k) {
          const float a = start + kHalfPi * static_cast<float>(k) / static_cast<float>(kCornerSegments);
          values[index++] = {std::cos(a), std::sin(a)};
        }
      }
      return values;
    }();
    std::size_t arcIndex = 0;
    for (const Corner& c : corners) {
      for (int k = 0; k <= kCornerSegments; ++k) {
        addVertex(c.cx + rx * arc[arcIndex][0], c.cy + ry * arc[arcIndex][1]);
        ++arcIndex;
      }
    }
    const int ring = 4 * (kCornerSegments + 1);
    for (int k = 0; k < ring; ++k) {
      indices_.insert(indices_.end(), {base, base + 1 + k, base + 1 + (k + 1) % ring});
    }
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

  static float encode(float linear) {
    linear = std::clamp(linear, 0.f, 1.f);
    static const auto table = [] {
      std::array<float, 256> values{};
      for (std::size_t i = 0; i < values.size(); ++i) {
        const float v = static_cast<float>(i) / 255.f;
        values[i] = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
      }
      return values;
    }();
    const int index = static_cast<int>(linear * 255.f + 0.5f);
    return table[std::clamp(index, 0, 255)];
  }

  static constexpr int kCornerSegments = 6;   // segmentos de cada arco de canto (fan do retangulo arredondado)
  static constexpr float kPi = 3.14159265f;
  static constexpr float kHalfPi = 0.5f * kPi;

  SDL_Texture* atlas_ = nullptr;
  SDL_Texture* icons_ = nullptr;
  SDL_Texture* materials_ = nullptr;   // atlas das miniaturas de material (uploadMaterialThumb)
  SDL_Texture* views_[kViewSlots] = {};
  const TextAtlasSet* textSet_ = nullptr;          // conjunto de atlas por tamanho (opcional)
  SDL_Texture* sizeTex_[TextAtlasSet::kSlots] = {};   // uma textura por tamanho, criada sob demanda
  // Backend GPU (nao dono) e copias em CPU dos conteudos de textura que sobrevivem a troca de backend/renderer.
  UiGpuSink* gpu_ = nullptr;
  std::vector<std::uint8_t> glyphRgba_, iconsRgba_, materialsRgba_;
  int glyphW_ = 0, glyphH_ = 0, iconsW_ = 0, iconsH_ = 0;
  std::uint32_t activeClipIndex_ = SR::NoClipIndex;
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
  // Cache por painel: imagem do ultimo pacote, a chave (hash + tela) que a gerou e os contadores de acerto/erro.
  SDL_Texture* cacheTex_ = nullptr;
  int cacheW_ = 0, cacheH_ = 0;
  std::uint64_t cacheKey_ = 0;
  bool cacheValid_ = false;
  std::uint64_t cacheHits_ = 0, cacheMisses_ = 0;
};


// Carrega a fonte e monta o atlas de glifos (ASCII, Latin-1 e Latin Extended-A, para o portugues).
inline bool LoadTextAtlas(const std::string& fontPath, ST::TruetypeAtlas& out, float displayScale = 1.f) {
  std::ifstream file(fontPath, std::ios::binary);
  if (!file) return false;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  ST::TruetypeAtlasConfig cfg;
  // Rasteriza com o dobro da resolucao (sobreamostragem 2): o texto de 13 a 20 px e reduzido de uma fonte
  // mais nitida, sem o serrilhado de reduzir pouco mais de 1,25x (medido no Hierarchy).
  ST::BitmapRasterPolicy policy;
  policy.oversampling = 2.0f;
  cfg.rasterPixelSize = static_cast<float>(ST::ChooseBitmapRasterPixelSize(16.f, displayScale, policy));
  cfg.atlasWidth = cfg.rasterPixelSize >= 64.f ? 2048u : 1024u;
  cfg.atlasHeight = cfg.atlasWidth;
  // Gama de cobertura 0,5: o SDL mistura em sRGB; sem isso o texto claro sobre fundo escuro sai fino (medido).
  cfg.coverageGamma = 0.5f;
  cfg.firstCodepoint = 0x20;
  cfg.lastCodepoint = 0x17F;
  return ST::BuildTruetypeAtlas(bytes, cfg, out);
}


} // namespace editor::sui
