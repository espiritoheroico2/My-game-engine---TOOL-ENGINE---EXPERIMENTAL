// Backend do SpriteUI sobre o RHI (so com TOOL_WITH_SPRITEUI). Roda no dispositivo Null: verifica a montagem
// das chamadas (lotes, recorte, atlas, ordem de uso), nao os pixels. Os pixels ficam para o teste com GPU.
#include "test.hpp"

#include "tool/rhi/null_device.hpp"
#include "SpriteUi/Adapters/ToolEngineRhi.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#ifndef TOOL_SHADERS_DIR
#error "TOOL_SHADERS_DIR deve ser definido pelo CMake"
#endif

namespace {

namespace R = ToolEngine::SpriteUi::Rendering;

// Retangulo branco de 10x10 px na posicao (x, y), com tint dada.
R::SpriteInstanceGPU Rect(float x, float y, std::uint32_t clip, std::uint32_t flags = 0) {
  R::SpriteInstanceGPU inst{};
  inst.transformRow0[0] = 10.0f; inst.transformRow0[1] = 0.0f; inst.transformRow0[2] = x;
  inst.transformRow1[0] = 0.0f;  inst.transformRow1[1] = 10.0f; inst.transformRow1[2] = y;
  inst.uvRect[2] = 1.0f; inst.uvRect[3] = 1.0f;
  for (float& c : inst.tint) c = 1.0f;
  inst.resourceIds[0] = R::WhiteTextureSlice;
  inst.resourceIds[2] = clip;
  inst.resourceIds[3] = flags;
  return inst;
}

// Pacote de tres quads: dois sem recorte (um lote) e um com recorte 0 (outro lote).
R::FramePacket ThreeQuads() {
  R::FramePacket p;
  p.frameId = 1;
  p.instances = {Rect(0, 0, R::NoClipIndex), Rect(20, 0, R::NoClipIndex), Rect(40, 0, 0)};
  p.clipRects = {R::ClipRectGPU{0.0f, 0.0f, 32.0f, 32.0f}};
  p.batches = {
    R::DrawBatch{0, 2, R::WhiteTextureSlice, 0, R::NoClipIndex, 0},
    R::DrawBatch{2, 1, R::WhiteTextureSlice, 0, 0, 0},
  };
  return p;
}

struct Fixture {
  std::unique_ptr<tool::rhi::IDevice> dev = tool::rhi::createNullDevice();
  tool::rhi::TextureId target;
  Fixture() { target = dev->createTexture({64, 64, 1, tool::rhi::Format::RGBA8, true}, nullptr); }
};

} // namespace

TEST(spriteui_rhi_draws_batches_and_clips_on_null_device) {
  Fixture f;
  ToolEngine::SpriteUi::Adapters::SpriteUiRhiBackend backend(*f.dev, TOOL_SHADERS_DIR);
  CHECK(backend.init());
  const std::vector<std::uint8_t> atlas(8 * 8, 255);
  CHECK(backend.uploadAtlas(atlas.data(), 8, 8));
  CHECK(backend.setTarget(f.target, 64, 64));

  CHECK(backend.Submit(ThreeQuads()));
  // Dois trechos: os dois primeiros quads (sem recorte) e o terceiro (com recorte).
  CHECK(backend.stats().draws == 2);
  CHECK(backend.stats().quads == 3);
  CHECK(backend.stats().clippedRuns == 1);
  CHECK(backend.stats().emptyClips == 0);
  CHECK(backend.lastError().empty());
}

TEST(spriteui_rhi_skips_zero_area_clip) {
  Fixture f;
  ToolEngine::SpriteUi::Adapters::SpriteUiRhiBackend backend(*f.dev, TOOL_SHADERS_DIR);
  CHECK(backend.init());
  const std::vector<std::uint8_t> atlas(8 * 8, 255);
  CHECK(backend.uploadAtlas(atlas.data(), 8, 8));
  CHECK(backend.setTarget(f.target, 64, 64));

  R::FramePacket p = ThreeQuads();
  p.clipRects = {R::ClipRectGPU{0.0f, 0.0f, 0.0f, 0.0f}}; // area zero: o trecho com recorte nao desenha
  CHECK(backend.Submit(p));
  CHECK(backend.stats().emptyClips == 1);
  CHECK(backend.stats().draws == 1);
}

TEST(spriteui_rhi_rejects_missing_setup) {
  Fixture f;
  ToolEngine::SpriteUi::Adapters::SpriteUiRhiBackend backend(*f.dev, TOOL_SHADERS_DIR);
  // Antes de init(): nada pode ser desenhado.
  CHECK(!backend.Submit(ThreeQuads()));
  CHECK(backend.init());
  // Sem atlas nem alvo: recusa.
  CHECK(!backend.Submit(ThreeQuads()));
  const std::vector<std::uint8_t> atlas(8 * 8, 255);
  CHECK(backend.uploadAtlas(atlas.data(), 8, 8));
  CHECK(!backend.Submit(ThreeQuads()));
  CHECK(backend.setTarget(f.target, 64, 64));
  CHECK(backend.Submit(ThreeQuads()));
  // Atlas vazio e alvo invalido sao recusados sem tocar no estado anterior.
  CHECK(!backend.uploadAtlas(nullptr, 0, 0));
  CHECK(!backend.setTarget(tool::rhi::TextureId{}, 0, 0));
  CHECK(backend.Submit(ThreeQuads()));
}

TEST(spriteui_rhi_rounded_panel_uses_sdf_parameters) {
  Fixture f;
  ToolEngine::SpriteUi::Adapters::SpriteUiRhiBackend backend(*f.dev, TOOL_SHADERS_DIR);
  CHECK(backend.init());
  const std::vector<std::uint8_t> atlas(8 * 8, 255);
  CHECK(backend.uploadAtlas(atlas.data(), 8, 8));
  CHECK(backend.setTarget(f.target, 64, 64));

  R::FramePacket p;
  R::SpriteInstanceGPU panel = Rect(0, 0, R::NoClipIndex, R::FlagRoundedPanel);
  panel.transformRow1[3] = 3.0f;                  // raio em pixels
  p.instances = {panel};
  p.batches = {R::DrawBatch{0, 1, R::WhiteTextureSlice, 0, R::NoClipIndex, R::FlagRoundedPanel}};
  CHECK(backend.Submit(p));
  CHECK(backend.stats().quads == 1);
  CHECK(backend.lastError().empty());
}
