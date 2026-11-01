#include "tool/rhi/diligent_device.hpp"

#include "RefCntAutoPtr.hpp"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "NativeWindow.h"

#if TOOL_HAS_VULKAN
#include "EngineFactoryVk.h"
#endif
#if TOOL_HAS_OPENGL
#include "EngineFactoryOpenGL.h"
#endif
#if TOOL_HAS_D3D12
#include "EngineFactoryD3D12.h"
#endif

#include <cstring>
#include <unordered_map>

namespace tool::rhi {

namespace dl = Diligent;

namespace {

void messageCallback(dl::DEBUG_MESSAGE_SEVERITY sev, const char* msg, const char* func, const char*, int) {
  const bool err = sev >= dl::DEBUG_MESSAGE_SEVERITY_ERROR;
  const bool warn = sev == dl::DEBUG_MESSAGE_SEVERITY_WARNING;
  if (!err && !warn) return;
  debug().report(err ? "TOOL-E150" : "TOOL-W150", err ? Severity::Error : Severity::Warning,
                 std::string("[gpu] ") + (func ? std::string(func) + ": " : "") + (msg ? msg : ""));
}

dl::TEXTURE_FORMAT toDl(Format f) {
  switch (f) {
  case Format::RGBA8: return dl::TEX_FORMAT_RGBA8_UNORM;
  case Format::RGBA8_SRGB: return dl::TEX_FORMAT_RGBA8_UNORM_SRGB;
  case Format::BGRA8: return dl::TEX_FORMAT_BGRA8_UNORM;
  case Format::RGBA16F: return dl::TEX_FORMAT_RGBA16_FLOAT;
  case Format::R32F: return dl::TEX_FORMAT_R32_FLOAT;
  case Format::D32F: return dl::TEX_FORMAT_D32_FLOAT;
  case Format::D24S8: return dl::TEX_FORMAT_D24_UNORM_S8_UINT;
  }
  return dl::TEX_FORMAT_UNKNOWN;
}

class DiligentCommands;

class DiligentDevice final : public IDevice {
public:
  Backend kind = Backend::Null;
  dl::RefCntAutoPtr<dl::IRenderDevice> device;
  dl::RefCntAutoPtr<dl::IDeviceContext> ctx;
  dl::RefCntAutoPtr<dl::ISwapChain> swapchain;
  dl::RefCntAutoPtr<dl::ITexture> staging_;   // textura de leitura reaproveitada (readTexture)
  dl::Uint32 stagingW_ = 0, stagingH_ = 0; dl::TEXTURE_FORMAT stagingFmt_ = dl::TEX_FORMAT_UNKNOWN;
  std::string adapter;

  Backend backend() const override { return kind; }
  std::string deviceName() const override { return adapter; }

  // ---- recursos ----
  BufferId createBuffer(const BufferDesc& d, const void* data) override {
    if (d.size == 0) { debug().report("TOOL-E130", Severity::Error, "buffer de tamanho 0"); return kNullHandle; }
    dl::BufferDesc bd;
    bd.Name = "tool buffer";
    bd.Size = d.size;
    switch (d.usage) {
    case BufferUsage::Vertex: bd.BindFlags = dl::BIND_VERTEX_BUFFER; break;
    case BufferUsage::Index: bd.BindFlags = dl::BIND_INDEX_BUFFER; break;
    case BufferUsage::Uniform: bd.BindFlags = dl::BIND_UNIFORM_BUFFER; break;
    case BufferUsage::Storage: bd.BindFlags = dl::BIND_SHADER_RESOURCE; bd.Mode = dl::BUFFER_MODE_RAW; break;
    }
    if (d.dynamic || d.usage == BufferUsage::Uniform) { bd.Usage = dl::USAGE_DYNAMIC; bd.CPUAccessFlags = dl::CPU_ACCESS_WRITE; }
    else if (data) bd.Usage = dl::USAGE_IMMUTABLE;
    else bd.Usage = dl::USAGE_DEFAULT;
    dl::BufferData init{data, data ? d.size : 0};
    dl::RefCntAutoPtr<dl::IBuffer> b;
    device->CreateBuffer(bd, (data && bd.Usage != dl::USAGE_DYNAMIC) ? &init : nullptr, &b);
    if (!b) { debug().report("TOOL-E151", Severity::Error, "falha ao criar buffer na GPU"); return kNullHandle; }
    Handle h = ids_.create();
    put(h.index, Kind::Buffer, b, d.size);
    return h;
  }

  TextureId createTexture(const TextureDesc& d, const void* data) override {
    if (d.width == 0 || d.height == 0) { debug().report("TOOL-E131", Severity::Error, "textura com dimensao 0"); return kNullHandle; }
    dl::TextureDesc td;
    td.Name = "tool texture";
    td.Type = dl::RESOURCE_DIM_TEX_2D;
    td.Width = d.width; td.Height = d.height; td.MipLevels = d.mips;
    td.Format = toDl(d.format);
    const bool isDepth = d.format == Format::D32F || d.format == Format::D24S8;
    if (td.Format == dl::TEX_FORMAT_UNKNOWN && !isDepth) { // formato que o Diligent nao conhece: cai para RGBA8 com aviso
      debug().report("TOOL-W200", Severity::Warning, "formato de textura desconhecido; usando RGBA8_UNORM");
      td.Format = dl::TEX_FORMAT_RGBA8_UNORM;
    }
    td.BindFlags = isDepth ? dl::BIND_DEPTH_STENCIL : (d.renderTarget ? (dl::BIND_RENDER_TARGET | dl::BIND_SHADER_RESOURCE) : dl::BIND_SHADER_RESOURCE);
    td.Usage = dl::USAGE_DEFAULT;
    if (d.renderTarget || isDepth) { td.ClearValue.Format = td.Format; if (isDepth) { td.ClearValue.DepthStencil.Depth = 1.f; } }
    dl::RefCntAutoPtr<dl::ITexture> t;
    device->CreateTexture(td, nullptr, &t);
    if (!t) { debug().report("TOOL-E151", Severity::Error, "falha ao criar textura na GPU"); return kNullHandle; }
    if (data && !isDepth && d.format == Format::RGBA8) { // dados iniciais: copia o nivel 0 (linhas de 4 bytes por pixel)
      dl::Box box; box.MinX = 0; box.MaxX = d.width; box.MinY = 0; box.MaxY = d.height; box.MinZ = 0; box.MaxZ = 1;
      dl::TextureSubResData sub; sub.pData = data; sub.Stride = d.width * 4; sub.DepthStride = 0;
      ctx->UpdateTexture(t, 0, 0, box, sub, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    Handle h = ids_.create();
    put(h.index, Kind::Texture, t, 0);
    return h;
  }

  ShaderId createShader(const ShaderDesc& d) override {
    if (d.source.empty()) { debug().report("TOOL-E132", Severity::Error, "shader sem codigo-fonte HLSL (bytecode pre-compilado ainda nao suportado)"); return kNullHandle; }
    dl::ShaderCreateInfo ci;
    ci.Desc.Name = d.name.empty() ? "tool shader" : d.name.c_str();
    ci.Desc.ShaderType = d.stage == ShaderStage::Vertex ? dl::SHADER_TYPE_VERTEX : d.stage == ShaderStage::Pixel ? dl::SHADER_TYPE_PIXEL : dl::SHADER_TYPE_COMPUTE;
    ci.Desc.UseCombinedTextureSamplers = true;
    const bool glsl = d.lang == ShaderLang::GLSL;
    ci.EntryPoint = glsl ? "main" : d.entry.c_str();
    ci.Source = d.source.c_str();
    ci.SourceLength = d.source.size();
    ci.SourceLanguage = glsl ? dl::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM : dl::SHADER_SOURCE_LANGUAGE_HLSL;
    ci.ShaderCompiler = dl::SHADER_COMPILER_DEFAULT;
    dl::RefCntAutoPtr<dl::IShader> s;
    device->CreateShader(ci, &s);
    if (!s) { debug().report("TOOL-E152", Severity::Error, "falha ao compilar shader '" + d.name + "' (" + d.entry + ")"); return kNullHandle; }
    Handle h = ids_.create();
    put(h.index, Kind::Shader, s, 0);
    return h;
  }

  PipelineId createPipeline(const PipelineDesc& d) override {
    auto* vs = obj<dl::IShader>(d.vs, Kind::Shader);
    auto* ps = obj<dl::IShader>(d.ps, Kind::Shader);
    if (!vs || !ps) { debug().report("TOOL-E133", Severity::Error, "pipeline com shader invalido"); return kNullHandle; }
    dl::GraphicsPipelineStateCreateInfo pci;
    pci.PSODesc.Name = d.name.empty() ? "tool pipeline" : d.name.c_str();
    pci.PSODesc.PipelineType = dl::PIPELINE_TYPE_GRAPHICS;
    auto& gp = pci.GraphicsPipeline;
    gp.NumRenderTargets = 1;
    gp.RTVFormats[0] = toDl(d.color);
    gp.DSVFormat = toDl(d.depth);
    gp.PrimitiveTopology = dl::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode = d.cull == CullMode::None ? dl::CULL_MODE_NONE : d.cull == CullMode::Back ? dl::CULL_MODE_BACK : dl::CULL_MODE_FRONT;
    // frente = anti-horario (convencao do Mesh). No OpenGL o GLSL gerado vem de SPIR-V (eixo Y de clip invertido) e o
    // Diligent inverte o Y mais uma vez ao desenhar em textura: o sentido dos triangulos chega trocado, entao la a frente e horaria.
    gp.RasterizerDesc.FrontCounterClockwise = kind != Backend::GLES;
    gp.DepthStencilDesc.DepthEnable = d.depthTest;
    gp.DepthStencilDesc.DepthWriteEnable = d.depthWrite;
    gp.DepthStencilDesc.DepthFunc = dl::COMPARISON_FUNC_LESS_EQUAL;
    if (d.blend) {
      auto& rt = gp.BlendDesc.RenderTargets[0];
      rt.BlendEnable = true; rt.SrcBlend = dl::BLEND_FACTOR_SRC_ALPHA; rt.DestBlend = dl::BLEND_FACTOR_INV_SRC_ALPHA;
    }
    std::vector<dl::LayoutElement> elems;
    for (u32 i = 0; i < d.layout.size(); ++i)
      elems.emplace_back(i, 0u, d.layout[i].components, dl::VT_FLOAT32, false, d.layout[i].offset, d.vertexStride);
    gp.InputLayout.LayoutElements = elems.data();
    gp.InputLayout.NumElements = static_cast<dl::Uint32>(elems.size());
    pci.pVS = vs; pci.pPS = ps;
    pci.PSODesc.ResourceLayout.DefaultVariableType = dl::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    dl::RefCntAutoPtr<dl::IPipelineState> pso;
    device->CreateGraphicsPipelineState(pci, &pso);
    if (!pso) { debug().report("TOOL-E153", Severity::Error, "falha ao criar pipeline '" + d.name + "'"); return kNullHandle; }
    Handle h = ids_.create();
    put(h.index, Kind::Pipeline, pso, 0);
    PipelineData pd;
    pso->CreateShaderResourceBinding(&pd.srb, true);
    pd.uniformNames = d.uniformNames; pd.textureNames = d.textureNames;
    pipelines_[h.index] = std::move(pd);
    return h;
  }

  void destroy(Handle h) override {
    if (!ids_.alive(h)) { debug().report(diag::kStaleHandle, Severity::Warning, "RHI destroy() com handle obsoleto"); return; }
    ctx->WaitForIdle(); // simples e seguro: o objeto pode estar em uso por comandos em voo
    objs_.erase(h.index); pipelines_.erase(h.index); sizes_.erase(h.index); kinds_.erase(h.index);
    ids_.destroy(h);
  }

  void updateBuffer(BufferId b, const void* data, size_t size) override {
    auto* buf = obj<dl::IBuffer>(b, Kind::Buffer);
    if (!buf) { debug().report(diag::kStaleHandle, Severity::Error, "updateBuffer em handle invalido"); return; }
    if (size > sizes_[b.index]) { debug().report("TOOL-E134", Severity::Error, "updateBuffer maior que o buffer"); return; }
    if (buf->GetDesc().Usage == dl::USAGE_DYNAMIC) {
      dl::PVoid mapped = nullptr;
      ctx->MapBuffer(buf, dl::MAP_WRITE, dl::MAP_FLAG_DISCARD, mapped);
      if (mapped) std::memcpy(mapped, data, size);
      ctx->UnmapBuffer(buf, dl::MAP_WRITE);
    } else if (buf->GetDesc().Usage == dl::USAGE_DEFAULT) {
      ctx->UpdateBuffer(buf, 0, static_cast<dl::Uint64>(size), data, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    } else {
      debug().report("TOOL-E134", Severity::Error, "updateBuffer em buffer imutavel");
    }
    ++stats_.bufferUploads;
  }

  std::unique_ptr<ICommandList> beginCommands() override;
  void submit(ICommandList&) override { ctx->Flush(); }

  // Fim de quadro. FinishFrame() e OBRIGATORIO: e ele que devolve a memoria dinamica (buffers uniformes com
  // MAP_FLAG_DISCARD) de quadros ja concluidos. Sem ele o "dynamic heap" enche e o log mostra
  // "Space in dynamic heap is exhausted". Rodamos offscreen (resultado lido de volta), entao no OpenGL nem
  // trocamos buffers da janela oculta (evita travar esperando vsync de uma janela invisivel).
  void present() override {
    if (swapchain && kind != Backend::GLES) swapchain->Present();
    ctx->FinishFrame();
    last_ = stats_; stats_ = {};
  }
  DrawStats frameStats() const override { return last_; }

  TextureId backbuffer() override {
    if (!swapchain) return kNullHandle;
    if (backbufferHandle_.isNull()) { backbufferHandle_ = ids_.create(); kinds_[backbufferHandle_.index] = Kind::Backbuffer; }
    return backbufferHandle_;
  }
  bool resizeSwapchain(u32 w, u32 h) override { if (!swapchain) return false; swapchain->Resize(w, h); return true; }

  bool readTexture(TextureId t, std::vector<u8>& rgba, u32& w, u32& h) override {
    dl::ITexture* tex = nullptr;
    if (auto* t0 = obj<dl::ITexture>(t, Kind::Texture)) tex = t0;
    else if (swapchain && t == backbufferHandle_) tex = swapchain->GetCurrentBackBufferRTV()->GetTexture();
    if (!tex) return false;
    const auto& td = tex->GetDesc();
    if (td.Format != dl::TEX_FORMAT_RGBA8_UNORM && td.Format != dl::TEX_FORMAT_RGBA8_UNORM_SRGB && td.Format != dl::TEX_FORMAT_BGRA8_UNORM && td.Format != dl::TEX_FORMAT_BGRA8_UNORM_SRGB) return false;
    // A textura de leitura e reaproveitada enquanto o tamanho/formato nao mudam (criar uma por quadro era lento).
    if (!staging_ || stagingW_ != td.Width || stagingH_ != td.Height || stagingFmt_ != td.Format) {
      dl::TextureDesc sd = td;
      sd.Name = "tool readback"; sd.Usage = dl::USAGE_STAGING; sd.BindFlags = dl::BIND_NONE; sd.CPUAccessFlags = dl::CPU_ACCESS_READ;
      staging_.Release();
      device->CreateTexture(sd, nullptr, &staging_);
      stagingW_ = td.Width; stagingH_ = td.Height; stagingFmt_ = td.Format;
    }
    if (!staging_) return false;
    dl::ITexture* staging = staging_;
    dl::CopyTextureAttribs ca(tex, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->CopyTexture(ca);
    ctx->WaitForIdle();
    dl::MappedTextureSubresource m;
    ctx->MapTextureSubresource(staging, 0, 0, dl::MAP_READ, kind == Backend::GLES ? dl::MAP_FLAG_NONE : dl::MAP_FLAG_DO_NOT_WAIT, nullptr, m); // Vulkan/D3D12 exigem DO_NOT_WAIT (ja esperamos acima)
    if (!m.pData) return false;
    w = td.Width; h = td.Height;
    rgba.resize(static_cast<size_t>(w) * h * 4);
    const bool bgra = td.Format == dl::TEX_FORMAT_BGRA8_UNORM || td.Format == dl::TEX_FORMAT_BGRA8_UNORM_SRGB;
    for (u32 y = 0; y < h; ++y) {
      const u8* src = static_cast<const u8*>(m.pData) + static_cast<size_t>(y) * m.Stride;
      u8* dst = rgba.data() + static_cast<size_t>(y) * w * 4;
      if (!bgra) std::memcpy(dst, src, static_cast<size_t>(w) * 4);
      else for (u32 x = 0; x < w; ++x) { dst[x * 4] = src[x * 4 + 2]; dst[x * 4 + 1] = src[x * 4 + 1]; dst[x * 4 + 2] = src[x * 4]; dst[x * 4 + 3] = src[x * 4 + 3]; }
    }
    ctx->UnmapTextureSubresource(staging, 0, 0);
    return true;
  }

  // ---- acesso interno aos comandos ----
  enum class Kind : u8 { Buffer, Texture, Shader, Pipeline, Backbuffer };
  template <class T> T* obj(Handle h, Kind k) const {
    if (!ids_.alive(h)) return nullptr;
    auto kk = kinds_.find(h.index);
    if (kk == kinds_.end() || kk->second != k) return nullptr;
    auto it = objs_.find(h.index);
    return it == objs_.end() ? nullptr : static_cast<T*>(it->second.RawPtr());
  }
  struct PipelineData { dl::RefCntAutoPtr<dl::IShaderResourceBinding> srb; std::vector<std::string> uniformNames, textureNames; };
  PipelineData* pipelineData(Handle h) { auto it = pipelines_.find(h.index); return it == pipelines_.end() ? nullptr : &it->second; }
  bool isBackbuffer(Handle h) const { return swapchain && h == backbufferHandle_ && !h.isNull(); }
  void err(const char* m) { debug().report("TOOL-E135", Severity::Error, std::string("RHI: ") + m); }
  DrawStats stats_, last_;
  const HandleAllocator& ids() const { return ids_; }

private:
  void put(u32 index, Kind k, dl::IDeviceObject* o, size_t size) { kinds_[index] = k; objs_[index] = o; sizes_[index] = size; }

  HandleAllocator ids_;
  std::unordered_map<u32, Kind> kinds_;
  std::unordered_map<u32, dl::RefCntAutoPtr<dl::IDeviceObject>> objs_;
  std::unordered_map<u32, size_t> sizes_;
  std::unordered_map<u32, PipelineData> pipelines_;
  Handle backbufferHandle_;
};

class DiligentCommands final : public ICommandList {
public:
  explicit DiligentCommands(DiligentDevice& d) : d_(d) {}
  void begin(TextureId target, TextureId depth, f32 r, f32 g, f32 b, f32 a) override {
    if (open_) d_.err("begin() dentro de begin()");
    open_ = true;
    dl::ITextureView* rtv = nullptr; dl::ITextureView* dsv = nullptr;
    if (d_.isBackbuffer(target)) { rtv = d_.swapchain->GetCurrentBackBufferRTV(); dsv = d_.swapchain->GetDepthBufferDSV(); }
    else if (auto* t = d_.obj<dl::ITexture>(target, DiligentDevice::Kind::Texture)) rtv = t->GetDefaultView(dl::TEXTURE_VIEW_RENDER_TARGET);
    if (!rtv) { d_.err("begin() com alvo invalido"); open_ = false; return; }
    if (!depth.isNull()) { if (auto* t = d_.obj<dl::ITexture>(depth, DiligentDevice::Kind::Texture)) dsv = t->GetDefaultView(dl::TEXTURE_VIEW_DEPTH_STENCIL); }
    dl::ITextureView* rtvs[] = {rtv};
    d_.ctx->SetRenderTargets(1, rtvs, dsv, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // Viewport/scissor = tamanho do alvo. Alvos de tamanhos diferentes no mesmo quadro (mapa de sombra 1024^2 e imagem
    // 320x180) deixavam o viewport do alvo anterior no OpenGL, cortando o desenho; por isso e definido explicitamente.
    {
      const auto& td = rtv->GetTexture()->GetDesc();
      dl::Viewport vp; vp.Width = static_cast<float>(td.Width); vp.Height = static_cast<float>(td.Height);
      d_.ctx->SetViewports(1, &vp, td.Width, td.Height);
    }
    const float clear[4] = {r, g, b, a};
    d_.ctx->ClearRenderTarget(rtv, clear, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (dsv) d_.ctx->ClearDepthStencil(dsv, dl::CLEAR_DEPTH_FLAG, 1.f, 0, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
  void setPipeline(PipelineId p) override {
    if (!check()) return;
    auto* pso = d_.obj<dl::IPipelineState>(p, DiligentDevice::Kind::Pipeline);
    if (!pso) { d_.err("setPipeline com handle invalido"); pipe_ = nullptr; return; }
    d_.ctx->SetPipelineState(pso);
    pipe_ = d_.pipelineData(p);
    ++d_.stats_.pipelineSwitches;
  }
  void setVertexBuffer(BufferId b) override {
    if (!check()) return;
    auto* buf = d_.obj<dl::IBuffer>(b, DiligentDevice::Kind::Buffer);
    if (!buf) { d_.err("vertex buffer invalido"); vb_ = false; return; }
    dl::IBuffer* bufs[] = {buf}; dl::Uint64 off[] = {0};
    d_.ctx->SetVertexBuffers(0, 1, bufs, off, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, dl::SET_VERTEX_BUFFERS_FLAG_RESET);
    vb_ = true;
  }
  void setIndexBuffer(BufferId b) override {
    if (!check()) return;
    auto* buf = d_.obj<dl::IBuffer>(b, DiligentDevice::Kind::Buffer);
    if (!buf) { d_.err("index buffer invalido"); ib_ = false; return; }
    d_.ctx->SetIndexBuffer(buf, 0, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ib_ = true;
  }
  void setUniform(u32 slot, BufferId b) override {
    if (!check()) return;
    auto* buf = d_.obj<dl::IBuffer>(b, DiligentDevice::Kind::Buffer);
    if (!buf) { d_.err("uniform buffer invalido"); return; }
    if (!pipe_ || slot >= pipe_->uniformNames.size()) { d_.err("setUniform: slot sem nome (defina PipelineDesc::uniformNames e chame setPipeline antes)"); return; }
    for (auto st : {dl::SHADER_TYPE_VERTEX, dl::SHADER_TYPE_PIXEL})
      if (auto* var = pipe_->srb->GetVariableByName(st, pipe_->uniformNames[slot].c_str())) var->Set(buf);
  }
  void setTexture(u32 slot, TextureId t) override {
    if (!check()) return;
    auto* tex = d_.obj<dl::ITexture>(t, DiligentDevice::Kind::Texture);
    if (!tex) { d_.err("textura invalida"); return; }
    if (!pipe_ || slot >= pipe_->textureNames.size()) { d_.err("setTexture: slot sem nome (defina PipelineDesc::textureNames e chame setPipeline antes)"); return; }
    if (auto* var = pipe_->srb->GetVariableByName(dl::SHADER_TYPE_PIXEL, pipe_->textureNames[slot].c_str())) var->Set(tex->GetDefaultView(dl::TEXTURE_VIEW_SHADER_RESOURCE));
  }
  void drawIndexed(u32 n, u32 inst) override {
    if (!check()) return;
    if (!pipe_ || !vb_ || !ib_) { d_.err("draw sem pipeline/vertex/index buffer"); return; }
    d_.ctx->CommitShaderResources(pipe_->srb, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    dl::DrawIndexedAttribs da; da.IndexType = dl::VT_UINT32; da.NumIndices = n; da.NumInstances = inst; da.Flags = dl::DRAW_FLAG_VERIFY_ALL;
    d_.ctx->DrawIndexed(da);
    ++d_.stats_.draws; d_.stats_.triangles += (n / 3) * inst;
  }
  void end() override { if (!open_) d_.err("end() sem begin()"); open_ = false; }

private:
  bool check() { if (!open_) d_.err("comando fora de begin()/end()"); return open_; }
  DiligentDevice& d_;
  bool open_ = false, vb_ = false, ib_ = false;
  DiligentDevice::PipelineData* pipe_ = nullptr;
};

std::unique_ptr<ICommandList> DiligentDevice::beginCommands() { return std::make_unique<DiligentCommands>(*this); }

void fillWindow(dl::NativeWindow& w, const DeviceInfo& info) {
#if defined(_WIN32)
  w.hWnd = info.nativeWindow;
  (void)info.nativeDisplay;
#elif defined(__linux__)
  w.WindowId = static_cast<dl::Uint32>(reinterpret_cast<uintptr_t>(info.nativeWindow));
  w.pDisplay = info.nativeDisplay;
#else
  (void)w; (void)info;
#endif
}

} // namespace

std::vector<Backend> compiledBackends() {
  std::vector<Backend> v;
#if TOOL_HAS_D3D12
  v.push_back(Backend::D3D12);
#endif
#if TOOL_HAS_VULKAN
  v.push_back(Backend::Vulkan);
#endif
#if TOOL_HAS_OPENGL
  v.push_back(Backend::GLES); // OpenGL desktop / GLES conforme a plataforma
#endif
  return v;
}

std::unique_ptr<IDevice> createDiligentDevice(const DeviceInfo& info, std::string* error) {
  auto fail = [&](const std::string& m) { if (error) *error = m; debug().report("TOOL-E154", Severity::Error, m); return std::unique_ptr<IDevice>(); };
  dl::SetDebugMessageCallback(messageCallback);
  auto dev = std::make_unique<DiligentDevice>();
  dev->kind = info.backend;
  dl::SwapChainDesc scd;
  scd.Width = info.width; scd.Height = info.height;
  scd.ColorBufferFormat = dl::TEX_FORMAT_RGBA8_UNORM_SRGB;
  scd.DepthBufferFormat = dl::TEX_FORMAT_D32_FLOAT;
  dl::NativeWindow win; fillWindow(win, info);
  const bool wantSwap = info.nativeWindow != nullptr;

  switch (info.backend) {
#if TOOL_HAS_VULKAN
  case Backend::Vulkan: {
    auto* f = dl::GetEngineFactoryVk();
    dl::EngineVkCreateInfo ci;
    ci.DynamicHeapSize = info.dynamicHeapMB << 20; // o padrao do Diligent e 8 MB; "Space in dynamic heap is exhausted" acontecia aqui
    ci.DynamicHeapPageSize = 1u << 20;
    ci.EnableValidation = info.validation;          // se as camadas nao estiverem instaladas, o Diligent avisa e segue sem elas
    if (!info.validation) debug().report("TOOL-W201", Severity::Warning, "Vulkan sem validacao: erros do driver podem passar sem aviso (ligue em Configuracoes > Graficos)");
    dl::IDeviceContext* ctxs[1] = {};
    f->CreateDeviceAndContextsVk(ci, &dev->device, ctxs);
    if (!dev->device || !ctxs[0]) return fail("Vulkan: falha ao criar o dispositivo (driver/GPU Vulkan ausente?)");
    dev->ctx.Attach(ctxs[0]);
    if (wantSwap) { f->CreateSwapChainVk(dev->device, dev->ctx, scd, win, &dev->swapchain); if (!dev->swapchain) return fail("Vulkan: falha ao criar a swapchain"); }
    break;
  }
#endif
#if TOOL_HAS_OPENGL
  case Backend::GLES: {
    if (!wantSwap) return fail("OpenGL: precisa de uma janela nativa (mesmo oculta) para criar o contexto");
    auto* f = dl::GetEngineFactoryOpenGL();
    dl::EngineGLCreateInfo ci;
    ci.Window = win;
    ci.ZeroToOneNDZ = true; // projecoes com z de clip em 0..1, iguais ao D3D/Vulkan
    // O Diligent nao expoe tamanho de dynamic heap no OpenGL (dynamicHeapMB vale so para Vulkan).
    ci.EnableValidation = info.validation; // depuracao do OpenGL (glDebugMessageCallback) quando solicitado
    if (!info.validation) debug().report("TOOL-W202", Severity::Warning, "OpenGL sem validacao: erros do driver podem passar sem aviso");
    dl::IDeviceContext* ctx = nullptr;
    f->CreateDeviceAndSwapChainGL(ci, &dev->device, &ctx, scd, &dev->swapchain);
    if (!dev->device || !ctx) return fail("OpenGL: falha ao criar o contexto (driver sem GL 4.3+?)");
    dev->ctx.Attach(ctx);
    break;
  }
#endif
#if TOOL_HAS_D3D12
  case Backend::D3D12: {
    auto* f = dl::GetEngineFactoryD3D12();
    dl::EngineD3D12CreateInfo ci;
    ci.EnableValidation = info.validation;   // o Diligent nao expoe tamanho total do heap dinamico no D3D12 (so o de pagina)
    if (!info.validation) debug().report("TOOL-W203", Severity::Warning, "D3D12 sem validacao: erros do driver podem passar sem aviso");
    dl::IDeviceContext* ctxs[1] = {};
    f->CreateDeviceAndContextsD3D12(ci, &dev->device, ctxs);
    if (!dev->device || !ctxs[0]) return fail("D3D12: falha ao criar o dispositivo");
    dev->ctx.Attach(ctxs[0]);
    if (wantSwap) { f->CreateSwapChainD3D12(dev->device, dev->ctx, scd, dl::FullScreenModeDesc{}, win, &dev->swapchain); if (!dev->swapchain) return fail("D3D12: falha ao criar a swapchain"); }
    break;
  }
#endif
  default: return fail(std::string("backend nao compilado neste binario: ") + toString(info.backend));
  }
  dev->adapter = dev->device->GetAdapterInfo().Description;
  return dev;
}

} // namespace tool::rhi
