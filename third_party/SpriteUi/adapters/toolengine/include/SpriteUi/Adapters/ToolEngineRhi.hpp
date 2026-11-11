// Adaptador opcional: backend de desenho do SpriteUI sobre o RHI da ToolEngine (tool::rhi::IDevice).
// Nao faz parte do nucleo: so compila com SPRITEUI_WITH_TOOLENGINE_RHI=ON e um alvo RHI do consumidor.
//
// Como funciona: cada SpriteInstanceGPU vira um quad de 4 vertices (expandido na CPU). Os quads ficam num
// unico vertex buffer por quadro, na ordem do FramePacket (a ordem de pintura e preservada). Cada DrawBatch vira
// uma ou mais chamadas drawIndexedRange, com o recorte (scissor) do clip do lote.
//
// Limites desta versao (de proposito):
//  - Um unico atlas de glifos/recursos, em alfa 8 bits (RGB = 1, A = cobertura). O slice 0 (WhiteTextureSlice)
//    e o retangulo branco; qualquer outro slice amostra o atlas. FlagBitmapText e FlagMsdfText valem como texto
//    de cobertura alfa; SDF/MSDF real fica para depois.
//  - O pixel (0,0) do atlas e reservado como branco solido: o atlas deve deixar essa celula vazia (padding >= 1).
//  - Cantos arredondados (FlagRoundedPanel) usam o mesmo SDF de caixa do SpriteUI, com 1 px de anti-aliasing.
//  - Coordenadas do FramePacket sao pixels do alvo (DPI 1). O alvo e definido com setTarget().
//
// Precisa de SpriteUI (SpriteUi::SpriteUI) e do RHI. Os shaders vem de assets/shaders/spriteui.hlsl; em OpenGL
// usa os GLSL gerados em assets/shaders/glsl/spriteui.{vs,ps}.430.glsl (tools/shaders/build_glsl.py).

#pragma once

#include "SpriteUi/SpriteUi.hpp"
#include "tool/rhi/rhi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <functional>

namespace ToolEngine::SpriteUi::Adapters {

namespace R = ToolEngine::SpriteUi::Rendering;
namespace rhi = tool::rhi;

class SpriteUiRhiBackend final : public R::IRenderBackend {
public:
  struct Stats {
    std::uint64_t frames = 0;
    std::uint64_t quads = 0;
    std::uint64_t draws = 0;        // chamadas drawIndexedRange emitidas
    std::uint64_t clippedRuns = 0;  // trechos com recorte
    std::uint64_t emptyClips = 0;   // trechos pulados por recorte de area zero
  };

  // device: dispositivo da engine (deve sobreviver ao backend). shaderDir: pasta de assets/shaders.
  // colorFormat: formato do alvo onde a UI sera desenhada (o pipeline precisa bater com ele).
  // Destino de mensagens de erro. Sem sink, as mensagens vao para stderr.
  using ErrorSink = std::function<void(const std::string&)>;

  SpriteUiRhiBackend(rhi::IDevice& device, std::string shaderDir,
                     rhi::Format colorFormat = rhi::Format::RGBA8_SRGB, ErrorSink sink = {})
    : dev_(device), shaderDir_(std::move(shaderDir)), colorFormat_(colorFormat), sink_(std::move(sink)) {}

  ~SpriteUiRhiBackend() override { release(); }

  SpriteUiRhiBackend(const SpriteUiRhiBackend&) = delete;
  SpriteUiRhiBackend& operator=(const SpriteUiRhiBackend&) = delete;

  // Cria pipeline e buffers. Retorna false (com aviso no log) se algum shader ou recurso falhar.
  bool init() {
    if (initialized_) return true;
    const bool gl = dev_.backend() == rhi::Backend::GLES;
    rhi::ShaderId vs = loadShader(gl, rhi::ShaderStage::Vertex, "VSMain", "spriteui.vs");
    rhi::ShaderId ps = loadShader(gl, rhi::ShaderStage::Pixel, "PSMain", "spriteui.ps");
    if (vs.isNull() || ps.isNull()) { fail("shader spriteui indisponivel"); return false; }
    vs_ = vs; ps_ = ps;

    rhi::PipelineDesc pd;
    pd.vs = vs_; pd.ps = ps_; pd.name = "spriteui";
    pd.depthTest = false; pd.depthWrite = false; pd.cull = rhi::CullMode::None;
    pd.blendPremultiplied = true;                 // UI em premultiplicado linear: ONE, INV_SRC_ALPHA
    pd.color = colorFormat_; pd.depth = rhi::Format::D32F;
    // Mesma ordem de Vertex (ATTRIB0..5), em bytes: pos(2) uv(2) tint(4) local(2) half(2) radius(1) = 52.
    pd.layout = {{2, 0}, {2, 8}, {4, 16}, {2, 32}, {2, 40}, {1, 48}};
    pd.vertexStride = sizeof(Vertex);
    pd.uniformNames = {"SpriteUiParams"};
    pd.textureNames = {"atlasTex"};
    pipeline_ = dev_.createPipeline(pd);
    if (pipeline_.isNull()) { fail("pipeline spriteui falhou"); return false; }

    params_ = dev_.createBuffer({sizeof(Params), rhi::BufferUsage::Uniform, true}, nullptr);
    if (params_.isNull()) { fail("buffer de parametros falhou"); return false; }

    initialized_ = true;
    return true;
  }

  // Envia o atlas de cobertura alfa (w x h, uma linha por vez, origem no topo). Substitui o atlas anterior.
  bool uploadAtlas(const std::uint8_t* alpha, std::uint32_t w, std::uint32_t h) {
    if (!alpha || w == 0 || h == 0) { fail("atlas vazio"); return false; }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
      rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i];
    }
    rgba[3] = 255;                                 // pixel (0,0): branco solido para retangulos
    rhi::TextureId tex = dev_.createTexture({w, h, 1, rhi::Format::RGBA8, false}, rgba.data());
    if (tex.isNull()) { fail("textura do atlas falhou"); return false; }
    if (!atlas_.isNull()) dev_.destroy(atlas_);
    atlas_ = tex;
    atlasW_ = w; atlasH_ = h;
    return true;
  }

  // Alvo onde a UI e desenhada. Para o backbuffer da janela a profundidade vem do swapchain; para outras
  // texturas o backend cria a sua profundidade (nao e usada, so o pipeline a exige).
  bool setTarget(rhi::TextureId target, std::uint32_t w, std::uint32_t h) {
    if (target.isNull() || w == 0 || h == 0) { fail("alvo invalido"); return false; }
    target_ = target; targetW_ = w; targetH_ = h;
    targetIsBackbuffer_ = target == dev_.backbuffer();
    if (!targetIsBackbuffer_) {
      if (depth_.isNull() || depthW_ != w || depthH_ != h) {
        if (!depth_.isNull()) dev_.destroy(depth_);
        depth_ = dev_.createTexture({w, h, 1, rhi::Format::D32F, true}, nullptr);
        depthW_ = w; depthH_ = h;
        if (depth_.isNull()) { fail("profundidade do alvo falhou"); return false; }
      }
    }
    return true;
  }

  // IRenderBackend: desenha o pacote no alvo definido por setTarget. Mantem a ordem do pacote.
  bool Submit(const R::FramePacket& packet) override {
    if (!R::IsFramePacketValid(packet)) { fail("FramePacket invalido"); return false; }
    if (!initialized_) { fail("backend nao inicializado (chame init)"); return false; }
    if (target_.isNull()) { fail("sem alvo (chame setTarget)"); return false; }
    if (atlas_.isNull()) { fail("sem atlas (chame uploadAtlas)"); return false; }

    BuildVertices(packet);
    if (vertices_.empty()) { ++stats_.frames; return true; }
    if (!EnsureBuffers(vertices_.size(), quadCount_)) return false;

    Params p{};
    p.viewport[0] = static_cast<float>(targetW_); p.viewport[1] = static_cast<float>(targetH_);
    p.atlas[0] = static_cast<float>(atlasW_); p.atlas[1] = static_cast<float>(atlasH_);
    dev_.updateBuffer(params_, &p, sizeof(p));
    dev_.updateBuffer(vb_, vertices_.data(), vertices_.size() * sizeof(Vertex));

    std::unique_ptr<rhi::ICommandList> cl = dev_.beginCommands();
    cl->beginKeep(target_, targetIsBackbuffer_ ? rhi::TextureId{} : depth_);
    cl->setPipeline(pipeline_);
    cl->setVertexBuffer(vb_);
    cl->setIndexBuffer(ib_);
    cl->setUniform(0, params_);
    cl->setTexture(0, atlas_);

    // Trechos contiguos com o mesmo recorte viram uma chamada de desenho.
    std::uint32_t runStart = 0;
    while (runStart < packet.instances.size()) {
      const std::uint32_t clip = packet.instances[runStart].resourceIds[2];
      std::uint32_t runEnd = runStart + 1;
      while (runEnd < packet.instances.size() && packet.instances[runEnd].resourceIds[2] == clip) ++runEnd;
      if (!ApplyClip(*cl, clip, packet.clipRects)) { ++stats_.emptyClips; runStart = runEnd; continue; }
      cl->drawIndexedRange(runStart * 6u, (runEnd - runStart) * 6u, 1);
      ++stats_.draws;
      runStart = runEnd;
    }
    cl->end();
    dev_.submit(*cl);
    ++stats_.frames;
    stats_.quads += packet.instances.size();
    return true;
  }

  [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

private:
  struct Vertex {
    float pos[2];
    float uv[2];
    float tint[4];
    float local[2];
    float half[2];
    float radius;
  };
  static_assert(sizeof(Vertex) == 52, "Vertex layout must match the PipelineDesc layout");

  struct Params {
    float viewport[2];
    float atlas[2];
  };

  void fail(const std::string& why) {
    lastError_ = why;
    const std::string msg = "SpriteUI RHI: " + why;
    if (sink_) sink_(msg);
    else std::fprintf(stderr, "[ERROR] %s\n", msg.c_str());
  }

  rhi::ShaderId loadShader(bool gl, rhi::ShaderStage stage, const char* entry, const char* name) {
    rhi::ShaderDesc d;
    d.stage = stage; d.entry = entry; d.name = name;
    if (gl) {
      std::ifstream in(shaderDir_ + "/glsl/" + std::string(name) + ".430.glsl", std::ios::binary);
      if (!in) { fail(std::string("nao achei o GLSL de ") + name); return {}; }
      d.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      d.lang = rhi::ShaderLang::GLSL; d.entry = "main";
    } else {
      std::ifstream in(shaderDir_ + "/spriteui.hlsl", std::ios::binary);
      if (!in) { fail("nao achei spriteui.hlsl em " + shaderDir_); return {}; }
      d.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      d.lang = rhi::ShaderLang::HLSL;
    }
    return dev_.createShader(d);
  }

  // Gera quatro vertices por instancia, na ordem do pacote. Mesma geometria do rasterizador de referencia:
  // o canto (u,v) do quad e tx + m00*u + m01*v (x) e ty + m10*u + m11*v (y).
  void BuildVertices(const R::FramePacket& packet) {
    vertices_.clear();
    vertices_.reserve(packet.instances.size() * 4);
    quadCount_ = 0;
    static constexpr float corners[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (const R::SpriteInstanceGPU& inst : packet.instances) {
      const float m00 = inst.transformRow0[0], m01 = inst.transformRow0[1], tx = inst.transformRow0[2];
      const float m10 = inst.transformRow1[0], m11 = inst.transformRow1[1], ty = inst.transformRow1[2];
      const float sizeX = std::sqrt(m00 * m00 + m10 * m10);
      const float sizeY = std::sqrt(m01 * m01 + m11 * m11);
      const bool rounded = (inst.resourceIds[3] & R::FlagRoundedPanel) != 0u;
      const float halfX = sizeX * 0.5f, halfY = sizeY * 0.5f;
      float radius = rounded && std::isfinite(inst.transformRow1[3]) ? inst.transformRow1[3] : 0.0f;
      radius = std::clamp(radius, 0.0f, std::max(0.0f, std::min(halfX, halfY)));
      const bool white = inst.resourceIds[0] == R::WhiteTextureSlice;

      for (const auto& c : corners) {
        const float u = c[0], v = c[1];
        Vertex vx{};
        vx.pos[0] = tx + m00 * u + m01 * v;
        vx.pos[1] = ty + m10 * u + m11 * v;
        if (white) {
          vx.uv[0] = 0.5f; vx.uv[1] = 0.5f;          // centro do pixel branco (0,0)
        } else {
          const float nu = inst.uvRect[0] + u * inst.uvRect[2];
          const float nv = inst.uvRect[1] + v * inst.uvRect[3];
          vx.uv[0] = nu * static_cast<float>(atlasW_);
          vx.uv[1] = nv * static_cast<float>(atlasH_);
        }
        for (int k = 0; k < 4; ++k) vx.tint[k] = inst.tint[k];
        vx.local[0] = (u - 0.5f) * sizeX;
        vx.local[1] = (v - 0.5f) * sizeY;
        vx.half[0] = halfX; vx.half[1] = halfY;
        vx.radius = radius;
        vertices_.push_back(vx);
      }
      ++quadCount_;
    }
  }

  // Recria os buffers quando o quadro cresce. O index buffer tem o padrao de dois triangulos por quad.
  bool EnsureBuffers(std::size_t vertexCount, std::size_t quads) {
    const std::size_t vbBytes = vertexCount * sizeof(Vertex);
    if (vb_.isNull() || vbCapacityBytes_ < vbBytes) {
      if (!vb_.isNull()) dev_.destroy(vb_);
      const std::size_t cap = std::max<std::size_t>(vbBytes, 64 * 1024);
      vb_ = dev_.createBuffer({cap, rhi::BufferUsage::Vertex, true}, nullptr);
      vbCapacityBytes_ = cap;
      if (vb_.isNull()) { fail("vertex buffer falhou"); return false; }
    }
    if (ib_.isNull() || ibCapacityQuads_ < quads) {
      if (!ib_.isNull()) dev_.destroy(ib_);
      std::size_t cap = std::max<std::size_t>(quads, 1024);
      std::vector<std::uint32_t> idx(cap * 6);
      for (std::size_t q = 0; q < cap; ++q) {
        const auto b = static_cast<std::uint32_t>(q * 4);
        const std::size_t o = q * 6;
        idx[o + 0] = b + 0; idx[o + 1] = b + 1; idx[o + 2] = b + 2;
        idx[o + 3] = b + 0; idx[o + 4] = b + 2; idx[o + 5] = b + 3;
      }
      ib_ = dev_.createBuffer({idx.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index, false}, idx.data());
      ibCapacityQuads_ = cap;
      if (ib_.isNull()) { fail("index buffer falhou"); return false; }
    }
    return true;
  }

  // Aplica o recorte do clip. Retorna false se o recorte tem area zero (o trecho nao deve ser desenhado).
  bool ApplyClip(rhi::ICommandList& cl, std::uint32_t clipIndex, const std::vector<R::ClipRectGPU>& clips) {
    const std::uint32_t fullW = targetW_, fullH = targetH_;
    if (clipIndex == R::NoClipIndex || clipIndex >= clips.size()) {
      cl.setScissor(0, 0, fullW, fullH);
      return true;
    }
    const R::ClipRectGPU& c = clips[clipIndex];
    const float x0 = std::clamp(std::floor(c.minX), 0.0f, static_cast<float>(fullW));
    const float y0 = std::clamp(std::floor(c.minY), 0.0f, static_cast<float>(fullH));
    const float x1 = std::clamp(std::ceil(c.maxX), 0.0f, static_cast<float>(fullW));
    const float y1 = std::clamp(std::ceil(c.maxY), 0.0f, static_cast<float>(fullH));
    if (!(x1 > x0) || !(y1 > y0)) return false;
    ++stats_.clippedRuns;
    cl.setScissor(static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0),
                  static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0));
    return true;
  }

  void release() {
    for (tool::Handle* h : {&vb_, &ib_, &params_, &atlas_, &depth_, &pipeline_, &vs_, &ps_}) {
      if (!h->isNull()) dev_.destroy(*h);
      *h = tool::Handle{};
    }
    initialized_ = false;
  }

  rhi::IDevice& dev_;
  std::string shaderDir_;
  rhi::Format colorFormat_;
  ErrorSink sink_;
  bool initialized_ = false;
  std::string lastError_;

  rhi::ShaderId vs_, ps_;
  rhi::PipelineId pipeline_;
  rhi::BufferId vb_, ib_, params_;
  rhi::TextureId atlas_, depth_, target_;
  std::uint32_t atlasW_ = 0, atlasH_ = 0;
  std::uint32_t targetW_ = 0, targetH_ = 0, depthW_ = 0, depthH_ = 0;
  bool targetIsBackbuffer_ = false;
  std::size_t vbCapacityBytes_ = 0, ibCapacityQuads_ = 0, quadCount_ = 0;
  std::vector<Vertex> vertices_;
  Stats stats_;
};

} // namespace ToolEngine::SpriteUi::Adapters
