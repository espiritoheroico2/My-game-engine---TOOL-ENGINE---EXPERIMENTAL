#pragma once

// Interface do editor (SpriteUI) desenhada pelo MESMO dispositivo Diligent da cena (Vulkan, D3D12, D3D11 ou OpenGL),
// apresentada pela swapchain da janela principal (docs/planos/38_PIPELINE_GRAFICA.md, etapa 3).
//
// Como funciona:
//  - Durante o quadro, cada pacote da SpriteUI (UiGpuSink::drawPacket) e cada geometria avulsa (drawGeometry: triangulos do
//    gizmo, imagens do Canvas) so GRAVA vertices e registros de desenho num vetor unico do quadro. Nada vai para a GPU ainda.
//  - No fim do quadro, flush() sobe um vertex buffer e um index buffer, abre UMA lista de comandos no backbuffer e emite um
//    drawIndexedRange por registro (texturas e recortes iguais e vizinhos viram um desenho so).
//  - Aparencia igual a do SDL_Renderer: o backbuffer e UNORM e a mistura e feita em sRGB; a cor do item sai do mesmo
//    sdlColor() (sRGB reto) e e premultiplicada aqui; as texturas sobem como RGBA8 cru; o shader (editor_ui.hlsl) filtra
//    bilinear/vizinho manualmente e premultiplica depois de filtrar.
//  - Coordenadas dos pacotes: pixels logicos da janela. Os vertices saem em pixels do backbuffer (escala = px / logico).
//
// Dono dos recursos: este objeto; precisa ser destruido ANTES do dispositivo (o hook DetachGpu do main faz isso).
#include "spriteui_host.hpp"
#include "tool/rhi/rhi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor::sui {

class UiGpuBackend final : public UiGpuSink {
 public:
  explicit UiGpuBackend(tool::rhi::IDevice& device, std::string shaderDir = "assets/shaders")
      : dev_(device), shaderDir_(std::move(shaderDir)), generation_(++counter()) {}
  ~UiGpuBackend() override { release(); }
  UiGpuBackend(const UiGpuBackend&) = delete;
  UiGpuBackend& operator=(const UiGpuBackend&) = delete;

  // Cria shaders, pipelines, textura branca 1x1 e o buffer de parametros. false (com erro no log) se algo falhar.
  bool init() {
    if (ready_) return true;
    if (!dev_.backbuffer().isNull()) backFormat_ = dev_.backbufferFormat();
    else { fail("o dispositivo nao tem swapchain (backbuffer)"); return false; }
    const bool gl = dev_.backend() == tool::rhi::Backend::GLES;
    vs_ = loadShader(gl, tool::rhi::ShaderStage::Vertex, "VSMain", "editor_ui.vs");
    ps_ = loadShader(gl, tool::rhi::ShaderStage::Pixel, "PSMain", "editor_ui.ps");
    if (vs_.isNull() || ps_.isNull()) { fail("shader editor_ui indisponivel"); return false; }
    pipeBack_ = makePipeline(backFormat_, tool::rhi::Format::D32F, "editor ui (backbuffer)");   // o backbuffer traz a profundidade da swapchain
    pipeOff_ = makePipeline(tool::rhi::Format::RGBA8, tool::rhi::Format::None, "editor ui (offscreen)");
    if (pipeBack_.isNull() || pipeOff_.isNull()) { fail("pipeline editor_ui falhou"); return false; }
    params_ = dev_.createBuffer({sizeof(Params), tool::rhi::BufferUsage::Uniform, true}, nullptr);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    white_ = dev_.createTexture({1, 1, 1, tool::rhi::Format::RGBA8, false}, white);
    if (params_.isNull() || white_.isNull()) { fail("recursos basicos da UI falharam"); return false; }
    ready_ = true;
    return true;
  }

  // Tamanho (px) com que a swapchain foi criada: o primeiro quadro so a redimensiona se a janela mudou desde entao.
  void setInitialSwapchainSize(int w, int h) { swapW_ = std::max(w, 1); swapH_ = std::max(h, 1); }

  // ---- UiGpuSink ----
  [[nodiscard]] std::uint32_t generation() const override { return generation_; }

  bool setSlice(std::uint32_t slice, int w, int h, const std::uint8_t* rgba, bool linear) override {
    if (!ready_ || w <= 0 || h <= 0 || !rgba) return false;
    auto it = slices_.find(slice);
    if (it != slices_.end() && it->second.w == w && it->second.h == h) {   // mesmo tamanho: troca so o conteudo
      dev_.updateTexture(it->second.id, rgba, static_cast<tool::u32>(w), static_cast<tool::u32>(h));
      it->second.linear = linear;
      return true;
    }
    const tool::rhi::TextureId t = dev_.createTexture({static_cast<tool::u32>(w), static_cast<tool::u32>(h), 1, tool::rhi::Format::RGBA8, false}, rgba);
    if (t.isNull()) return false;
    if (it != slices_.end()) pendingDestroy_.push_back(it->second.id);   // o antigo pode estar nos desenhos gravados deste quadro
    slices_[slice] = Tex{t, w, h, linear};
    return true;
  }
  [[nodiscard]] bool hasSlice(std::uint32_t slice) const override { return slices_.count(slice) != 0; }

  std::uint32_t createDynamic(int w, int h, const std::uint8_t* rgba) override {
    if (!ready_ || w <= 0 || h <= 0) return 0;
    const tool::rhi::TextureId t = dev_.createTexture({static_cast<tool::u32>(w), static_cast<tool::u32>(h), 1, tool::rhi::Format::RGBA8, false}, rgba);
    if (t.isNull()) return 0;
    const std::uint32_t id = nextDynamic_++;
    dynamic_[id] = Tex{t, w, h, true};
    return id;
  }
  void updateDynamic(std::uint32_t id, const std::uint8_t* rgba) override {
    const auto it = dynamic_.find(id);
    if (it == dynamic_.end() || !rgba) return;
    dev_.updateTexture(it->second.id, rgba, static_cast<tool::u32>(it->second.w), static_cast<tool::u32>(it->second.h));
  }
  void destroyDynamic(std::uint32_t id) override {
    const auto it = dynamic_.find(id);
    if (it == dynamic_.end()) return;
    pendingDestroy_.push_back(it->second.id);   // so depois do flush: os desenhos gravados ainda podem referenciar a textura
    dynamic_.erase(it);
    for (View& v : views_) if (v.dyn == id) v.dyn = 0;
  }
  bool dynamicSize(std::uint32_t id, int& w, int& h) const override {
    const auto it = dynamic_.find(id);
    if (it == dynamic_.end()) return false;
    w = it->second.w; h = it->second.h;
    return true;
  }

  void bindView(std::uint32_t slot, std::uint32_t id, bool linear) override {
    if (slot < kViewSlots) views_[slot] = View{id, linear};
  }

  // ---- Quadro ----
  // Comeco do quadro: tamanho do backbuffer em pixels e da janela em unidades logicas (a escala dos vertices). Zera o quadro.
  void beginFrame(int pxW, int pxH, int logW, int logH) {
    vertices_.clear();
    indices_.clear();
    draws_.clear();
    pxW_ = std::max(pxW, 1); pxH_ = std::max(pxH, 1);
    scaleX_ = logW > 0 ? static_cast<float>(pxW_) / static_cast<float>(logW) : 1.f;
    scaleY_ = logH > 0 ? static_cast<float>(pxH_) / static_cast<float>(logH) : 1.f;
    if (pxW_ != swapW_ || pxH_ != swapH_) {   // janela redimensionada: a swapchain acompanha
      dev_.resizeSwapchain(static_cast<tool::u32>(pxW_), static_cast<tool::u32>(pxH_));
      swapW_ = pxW_; swapH_ = pxH_;
    }
  }

  bool drawPacket(const SR::FramePacket& packet) override {
    if (!ready_) return false;
    for (const auto& batch : packet.batches) {
      Tex tex;
      const bool textured = batch.textureSlice != SR::WhiteTextureSlice && resolveSlice(batch.textureSlice, tex);
      if (!textured) tex = Tex{white_, 1, 1, false};
      Scissor sc;
      if (!scissorFor(packet, batch.clipIndex, sc)) continue;   // recorte de area zero: nada a desenhar
      const std::uint32_t first = static_cast<std::uint32_t>(indices_.size());
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& inst = packet.instances[batch.firstInstance + i];
        if (inst.tint[3] <= 0.f) continue;   // alfa zero e invisivel (igual ao caminho SDL)
        appendInstance(inst, textured, tex);
      }
      addDraw(first, tex.id, sc);
    }
    return true;
  }

  bool drawGeometry(const UiVertex* v, int vertexCount, const int* idx, int indexCount, std::uint32_t texture, bool linear,
                    const ToolEngine::SpriteUi::Rendering::ClipRectGPU* clip) override {
    if (!ready_ || !v || vertexCount <= 0) return false;
    Tex tex{white_, 1, 1, false};
    bool textured = false;
    if (texture != 0) {
      const auto it = dynamic_.find(texture);
      if (it != dynamic_.end()) { tex = it->second; tex.linear = linear; textured = true; }
    }
    Scissor sc;
    if (!scissorForClip(clip, sc)) return true;
    const std::uint32_t first = static_cast<std::uint32_t>(indices_.size());
    const std::uint32_t base = static_cast<std::uint32_t>(vertices_.size());
    for (int i = 0; i < vertexCount; ++i) {
      const UiVertex& s = v[i];
      Vtx o{};
      o.pos[0] = s.x * scaleX_; o.pos[1] = s.y * scaleY_;
      o.uv[0] = textured ? s.u * static_cast<float>(tex.w) : 0.5f;
      o.uv[1] = textured ? s.v * static_cast<float>(tex.h) : 0.5f;
      o.tint[0] = s.r * s.a; o.tint[1] = s.g * s.a; o.tint[2] = s.b * s.a; o.tint[3] = s.a;   // reto -> premultiplicado
      o.mode[0] = 0.f;
      o.mode[1] = textured && tex.linear ? 1.f : 0.f;
      vertices_.push_back(o);
    }
    if (idx && indexCount > 0) for (int i = 0; i < indexCount; ++i) indices_.push_back(base + static_cast<std::uint32_t>(idx[i]));
    else for (int i = 0; i + 2 < vertexCount; i += 3) for (int k = 0; k < 3; ++k) indices_.push_back(base + static_cast<std::uint32_t>(i + k));
    addDraw(first, tex.id, sc);
    return true;
  }

  // Envia o quadro gravado: limpa o backbuffer com (r,g,b) e desenha tudo. Depois libera o que ficou pendente.
  bool flush(float r, float g, float b) {
    if (!ready_) return false;
    if (!uploadBuffers()) return false;
    recordPass(dev_.backbuffer(), true, r, g, b);
    return true;
  }

  // Captura de tela: repete o quadro gravado num alvo RGBA8 fora da tela e le de volta (espera a GPU). Chamar depois de flush().
  bool captureOffscreen(float r, float g, float b, std::vector<tool::u8>& rgba, tool::u32& w, tool::u32& h) {
    if (!ready_) return false;
    if (off_.isNull() || offW_ != pxW_ || offH_ != pxH_) {
      if (!off_.isNull()) dev_.destroy(off_);
      off_ = dev_.createTexture({static_cast<tool::u32>(pxW_), static_cast<tool::u32>(pxH_), 1, tool::rhi::Format::RGBA8, true}, nullptr);
      offW_ = pxW_; offH_ = pxH_;
      if (off_.isNull()) return false;
    }
    recordPass(off_, false, r, g, b);
    return dev_.readTexture(off_, rgba, w, h);
  }

  // Libera as texturas que o quadro anterior ainda podia usar. Chamado no fim do quadro, depois do flush.
  void endFrame() {
    for (const tool::rhi::TextureId& t : pendingDestroy_) if (!t.isNull()) dev_.destroy(t);
    pendingDestroy_.clear();
  }

  [[nodiscard]] std::size_t drawCount() const { return draws_.size(); }
  [[nodiscard]] std::size_t vertexCount() const { return vertices_.size(); }

 private:
  struct Vtx {   // 56 bytes: ver o layout do pipeline e editor_ui.hlsl
    float pos[2];
    float uv[2];
    float tint[4];
    float shape[4];   // xy = posicao local centrada, zw = meia dimensao (px)
    float mode[2];    // x = raio do canto, y = filtro linear
  };
  static_assert(sizeof(Vtx) == 56, "Vtx deve bater com o layout do pipeline");
  struct Params { float viewport[2]; float padding[2]; };
  struct Tex { tool::rhi::TextureId id; int w = 0, h = 0; bool linear = true; };
  struct View { std::uint32_t dyn = 0; bool linear = true; };
  struct Scissor {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Scissor&) const = default;
  };
  struct Draw { std::uint32_t first = 0, count = 0; tool::rhi::TextureId tex; Scissor sc; };

  static std::uint32_t& counter() { static std::uint32_t c = 0; return c; }

  void fail(const std::string& why) const { tool::debug().report("TOOL-E160", tool::Severity::Error, "UI GPU: " + why); }

  tool::rhi::ShaderId loadShader(bool gl, tool::rhi::ShaderStage stage, const char* entry, const char* name) {
    tool::rhi::ShaderDesc d;
    d.stage = stage; d.entry = entry; d.name = name;
    const std::string path = gl ? shaderDir_ + "/glsl/" + name + ".430.glsl" : shaderDir_ + "/hlsl/editor_ui.hlsl";
    std::ifstream in(path, std::ios::binary);
    if (!in) { fail("nao achei " + path); return {}; }
    d.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    d.lang = gl ? tool::rhi::ShaderLang::GLSL : tool::rhi::ShaderLang::HLSL;
    if (gl) d.entry = "main";
    return dev_.createShader(d);
  }

  tool::rhi::PipelineId makePipeline(tool::rhi::Format color, tool::rhi::Format depth, const char* name) {
    tool::rhi::PipelineDesc pd;
    pd.vs = vs_; pd.ps = ps_; pd.name = name;
    pd.depthTest = false; pd.depthWrite = false; pd.cull = tool::rhi::CullMode::None;
    pd.blendPremultiplied = true;   // ONE, INV_SRC_ALPHA: o tint ja vem multiplicado pelo alfa
    pd.color = color; pd.depth = depth;
    pd.layout = {{2, 0}, {2, 8}, {4, 16}, {4, 32}, {2, 48}};
    pd.vertexStride = sizeof(Vtx);
    pd.uniformNames = {"EditorUiParams"};
    pd.textureNames = {"uiTex"};
    return dev_.createPipeline(pd);
  }

  // Textura (e filtro) de um slice da SpriteUI. false se ainda nao existe (o desenho cai no branco).
  bool resolveSlice(std::uint32_t slice, Tex& out) const {
    if (slice >= kViewSliceBase && slice - kViewSliceBase < kViewSlots) {
      const View& v = views_[slice - kViewSliceBase];
      const auto it = dynamic_.find(v.dyn);
      if (v.dyn == 0 || it == dynamic_.end()) return false;
      out = it->second;
      out.linear = v.linear;
      return true;
    }
    const auto it = slices_.find(slice);
    if (it == slices_.end()) return false;
    out = it->second;
    return true;
  }

  // Recorte do lote em pixels do backbuffer (mesma conta do SDL: floor/ceil no espaco logico). false = area zero.
  bool scissorFor(const SR::FramePacket& packet, std::uint32_t clipIndex, Scissor& out) const {
    if (clipIndex == SR::NoClipIndex || clipIndex >= packet.clipRects.size()) return scissorForClip(nullptr, out);
    return scissorForClip(&packet.clipRects[clipIndex], out);
  }
  bool scissorForClip(const SR::ClipRectGPU* clip, Scissor& out) const {
    if (!clip) { out = Scissor{0, 0, static_cast<std::uint32_t>(pxW_), static_cast<std::uint32_t>(pxH_)}; return true; }
    const float x0 = std::clamp(std::floor(std::floor(clip->minX) * scaleX_), 0.f, static_cast<float>(pxW_));
    const float y0 = std::clamp(std::floor(std::floor(clip->minY) * scaleY_), 0.f, static_cast<float>(pxH_));
    const float x1 = std::clamp(std::ceil(std::ceil(clip->maxX) * scaleX_), 0.f, static_cast<float>(pxW_));
    const float y1 = std::clamp(std::ceil(std::ceil(clip->maxY) * scaleY_), 0.f, static_cast<float>(pxH_));
    if (!(x1 > x0) || !(y1 > y0)) return false;
    out = Scissor{static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    return true;
  }

  // Um item da SpriteUI vira um quad (4 vertices, 6 indices). Cantos arredondados: o SDF do shader (radius > 0).
  void appendInstance(const SR::SpriteInstanceGPU& inst, bool textured, const Tex& tex) {
    const float m00 = inst.transformRow0[0], m01 = inst.transformRow0[1], tx = inst.transformRow0[2];
    const float m10 = inst.transformRow1[0], m11 = inst.transformRow1[1], ty = inst.transformRow1[2];
    const float sizeX = std::hypot(m00, m10), sizeY = std::hypot(m01, m11);
    const bool rounded = (inst.resourceIds[3] & SR::FlagRoundedPanel) != 0u && inst.transformRow1[3] > 0.f;
    float radius = rounded ? inst.transformRow1[3] : 0.f;
    radius = std::clamp(radius, 0.f, std::max(0.f, std::min(sizeX, sizeY) * 0.5f));
    const SDL_FColor c = SpriteUiSdlRenderer::sdlColor(inst);   // sRGB reto: a mesma cor que o SDL usa
    const float tint[4] = {c.r * c.a, c.g * c.a, c.b * c.a, c.a};
    const float s = 0.5f * (scaleX_ + scaleY_);
    static constexpr float corners[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    const std::uint32_t base = static_cast<std::uint32_t>(vertices_.size());
    for (const auto& corner : corners) {
      const float u = corner[0], v = corner[1];
      Vtx o{};
      o.pos[0] = (tx + m00 * u + m01 * v) * scaleX_;
      o.pos[1] = (ty + m10 * u + m11 * v) * scaleY_;
      if (textured) {
        o.uv[0] = (inst.uvRect[0] + u * inst.uvRect[2]) * static_cast<float>(tex.w);
        o.uv[1] = (inst.uvRect[1] + v * inst.uvRect[3]) * static_cast<float>(tex.h);
      } else {
        o.uv[0] = o.uv[1] = 0.5f;   // o unico texel da textura branca
      }
      for (int k = 0; k < 4; ++k) o.tint[k] = tint[k];
      o.shape[0] = (u - 0.5f) * sizeX * s; o.shape[1] = (v - 0.5f) * sizeY * s;
      o.shape[2] = sizeX * 0.5f * s;       o.shape[3] = sizeY * 0.5f * s;
      o.mode[0] = radius * s;
      o.mode[1] = textured && tex.linear ? 1.f : 0.f;
      vertices_.push_back(o);
    }
    indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  // Registra um desenho [first, fim dos indices atuais). Funde com o anterior se textura e recorte forem iguais.
  void addDraw(std::uint32_t first, const tool::rhi::TextureId& tex, const Scissor& sc) {
    const std::uint32_t count = static_cast<std::uint32_t>(indices_.size()) - first;
    if (count == 0) return;
    if (!draws_.empty()) {
      Draw& last = draws_.back();
      if (last.tex == tex && last.sc == sc && last.first + last.count == first) { last.count += count; return; }
    }
    draws_.push_back(Draw{first, count, tex, sc});
  }

  // Sobe vertices e indices do quadro (um buffer cada, com folga que cresce) e o tamanho do alvo.
  bool uploadBuffers() {
    Params p{};
    p.viewport[0] = static_cast<float>(pxW_); p.viewport[1] = static_cast<float>(pxH_);
    dev_.updateBuffer(params_, &p, sizeof(p));
    if (vertices_.empty() || indices_.empty()) return true;
    const std::size_t vbBytes = vertices_.size() * sizeof(Vtx), ibBytes = indices_.size() * sizeof(std::uint32_t);
    if (vb_.isNull() || vbCap_ < vbBytes) {
      if (!vb_.isNull()) dev_.destroy(vb_);
      vbCap_ = std::max<std::size_t>(vbBytes + vbBytes / 2, 256u * 1024u);
      vb_ = dev_.createBuffer({vbCap_, tool::rhi::BufferUsage::Vertex, true}, nullptr);
      if (vb_.isNull()) { fail("vertex buffer falhou"); return false; }
    }
    if (ib_.isNull() || ibCap_ < ibBytes) {
      if (!ib_.isNull()) dev_.destroy(ib_);
      ibCap_ = std::max<std::size_t>(ibBytes + ibBytes / 2, 256u * 1024u);
      ib_ = dev_.createBuffer({ibCap_, tool::rhi::BufferUsage::Index, true}, nullptr);
      if (ib_.isNull()) { fail("index buffer falhou"); return false; }
    }
    dev_.updateBuffer(vb_, vertices_.data(), vbBytes);
    dev_.updateBuffer(ib_, indices_.data(), ibBytes);
    return true;
  }

  // Grava o quadro num alvo: limpa com a cor e emite os desenhos na ordem.
  void recordPass(tool::rhi::TextureId target, bool backbuffer, float r, float g, float b) {
    std::unique_ptr<tool::rhi::ICommandList> cl = dev_.beginCommands();
    cl->begin(target, tool::rhi::TextureId{}, r, g, b, 1.f);
    if (!draws_.empty() && !vb_.isNull() && !ib_.isNull()) {
      cl->setPipeline(backbuffer ? pipeBack_ : pipeOff_);
      cl->setVertexBuffer(vb_);
      cl->setIndexBuffer(ib_);
      cl->setUniform(0, params_);
      for (const Draw& d : draws_) {
        cl->setScissor(d.sc.x, d.sc.y, d.sc.w, d.sc.h);
        cl->setTexture(0, d.tex);
        cl->drawIndexedRange(d.first, d.count, 1);
      }
    }
    cl->end();
    dev_.submit(*cl);
  }

  void release() {
    endFrame();
    for (auto& [k, t] : slices_) if (!t.id.isNull()) dev_.destroy(t.id);
    for (auto& [k, t] : dynamic_) if (!t.id.isNull()) dev_.destroy(t.id);
    slices_.clear();
    dynamic_.clear();
    for (tool::Handle* h : {&vb_, &ib_, &params_, &white_, &off_, &pipeBack_, &pipeOff_, &vs_, &ps_}) {
      if (!h->isNull()) dev_.destroy(*h);
      *h = tool::Handle{};
    }
    ready_ = false;
  }

  tool::rhi::IDevice& dev_;
  std::string shaderDir_;
  std::uint32_t generation_ = 0;
  bool ready_ = false;
  tool::rhi::Format backFormat_ = tool::rhi::Format::RGBA8;
  tool::rhi::ShaderId vs_, ps_;
  tool::rhi::PipelineId pipeBack_, pipeOff_;
  tool::rhi::BufferId vb_, ib_, params_;
  tool::rhi::TextureId white_, off_;
  std::size_t vbCap_ = 0, ibCap_ = 0;
  int offW_ = 0, offH_ = 0;
  std::unordered_map<std::uint32_t, Tex> slices_;
  std::unordered_map<std::uint32_t, Tex> dynamic_;
  std::uint32_t nextDynamic_ = 1;
  View views_[kViewSlots];
  std::vector<tool::rhi::TextureId> pendingDestroy_;
  // quadro
  std::vector<Vtx> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<Draw> draws_;
  int pxW_ = 1, pxH_ = 1, swapW_ = 0, swapH_ = 0;
  float scaleX_ = 1.f, scaleY_ = 1.f;
};

}  // namespace editor::sui
