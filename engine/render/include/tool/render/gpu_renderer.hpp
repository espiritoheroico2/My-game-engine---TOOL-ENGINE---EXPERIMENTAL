// ToolEngine - GpuRenderer: desenha a cena 3D iluminada (PBR) sobre qualquer backend do RHI.
// Mesmo modelo de camera/luz do SoftRenderer; os shaders sao os HLSL de assets/shaders.
#pragma once
#include "tool/render/shadow.hpp"
#include "tool/render/soft_renderer.hpp"
#include "tool/rhi/rhi.hpp"

#include <iterator>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace tool {

// Le um .hlsl resolvendo `#include "arquivo"` relativo a pasta (sem recursao infinita).
inline bool loadHlsl(const std::string& dir, const std::string& file, std::string& out, int depth = 0) {
  if (depth > 8) return false;
  std::ifstream f(dir + "/" + file, std::ios::binary);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) {
    const size_t p = line.find("#include");
    if (p != std::string::npos && line.find('"', p) != std::string::npos) {
      const size_t a = line.find('"', p) + 1, b = line.find('"', a);
      if (b == std::string::npos) return false;
      std::string inc;
      if (!loadHlsl(dir, line.substr(a, b - a), inc, depth + 1)) return false;
      out += inc + "\n";
    } else out += line + "\n";
  }
  return true;
}

// Layouts identicos aos cbuffers de pbr.hlsl / lighting.hlsli / shadow.hlsl (mudou aqui? mude la, e nos .glsl de assets/shaders/glsl).
struct FrameCB { Mat4 viewProj; f32 camera[3]; f32 pad = 0; };
struct LightCB {
  GpuLight lights[64]; f32 ambient[4]; u32 count; u32 pad[3];
  Mat4 lightViewProj;     // sol: mundo -> clip da luz
  f32 shadowParams[4];    // bias, 1/resolucao, forca (0 = sem sombra), normal bias em metros
  f32 shadowInfo[4];      // resolucao, raio PCF, 0, 0
};
struct ObjectCB { Mat4 world; f32 color[4]; f32 metallic, roughness, receiveShadow, pad; };
struct ShadowCB { Mat4 lightViewProj, world; };
static_assert(sizeof(FrameCB) == 80 && sizeof(LightCB) == 4096 + 32 + 64 + 32 && sizeof(ObjectCB) == 96 && sizeof(ShadowCB) == 128, "layout de cbuffer");

class GpuRenderer {
public:
  ~GpuRenderer() { release(); }
  GpuRenderer() = default;
  GpuRenderer(const GpuRenderer&) = delete;
  GpuRenderer& operator=(const GpuRenderer&) = delete;
  // Libera tudo na GPU. Chamar ANTES de destruir o dispositivo.
  void shutdown() { release(); }

  // shaderDir = pasta com pbr.hlsl e lighting.hlsli
  bool init(rhi::IDevice& dev, const std::string& shaderDir, u32 w, u32 h) {
    release();
    dev_ = &dev;
    std::string src;
    rhi::ShaderDesc vs; vs.stage = rhi::ShaderStage::Vertex; vs.entry = "VSMain"; vs.name = "pbr.vs";
    rhi::ShaderDesc ps; ps.stage = rhi::ShaderStage::Pixel; ps.entry = "PSMain"; ps.name = "pbr.ps";
    std::string shadowVsSrc, shadowPsSrc;
    if (dev.backend() == rhi::Backend::GLES) {
      // OpenGL: GLSL 430 em assets/shaders/glsl (pbr.* gerado do HLSL por tools/compile_shaders.py e depois ajustado
      // a mao; shadow.* escrito a mao), pois o conversor HLSL->GLSL embutido nao aceita estes shaders.
      // A fonte da verdade continua sendo o HLSL: mantenha os dois em sincronia.
      auto slurp = [&](const std::string& f, std::string& out) { std::ifstream a(shaderDir + "/glsl/" + f, std::ios::binary); if (!a) return false; out.assign(std::istreambuf_iterator<char>(a), {}); return true; };
      if (!slurp("pbr.vs.430.glsl", vs.source) || !slurp("pbr.ps.430.glsl", ps.source) || !slurp("shadow.vs.430.glsl", shadowVsSrc) || !slurp("shadow.ps.430.glsl", shadowPsSrc)) {
        debug().report("TOOL-E140", Severity::Error, "nao achei os shaders GLSL em " + shaderDir + "/glsl"); return false;
      }
      vs.lang = ps.lang = rhi::ShaderLang::GLSL;
    } else {
      if (!loadHlsl(shaderDir, "pbr.hlsl", src)) { debug().report("TOOL-E140", Severity::Error, "nao achei os shaders HLSL em " + shaderDir); return false; }
      vs.source = ps.source = src;
      std::string sh; if (!loadHlsl(shaderDir, "shadow.hlsl", sh)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow.hlsl em " + shaderDir); return false; }
      shadowVsSrc = shadowPsSrc = sh;
    }
    vs_ = dev.createShader(vs); ps_ = dev.createShader(ps);
    rhi::PipelineDesc pd; pd.vs = vs_; pd.ps = ps_; pd.name = "pbr";
    pd.layout = {{3, 0}, {3, 12}, {2, 24}}; pd.vertexStride = 32;
    pd.uniformNames = {"FrameBuffer", "LightBuffer", "ObjectBuffer"};
    pd.textureNames = {"ShadowMap"};
    // Cor: Vulkan/D3D12 usam alvo sRGB (o hardware converte). No OpenGL o Diligent liga GL_FRAMEBUFFER_SRGB conforme o
    // driver, entao usamos alvo UNORM e o proprio GLSL aplica o gamma: resultado igual em qualquer driver.
    colorFmt_ = dev.backend() == rhi::Backend::GLES ? rhi::Format::RGBA8 : rhi::Format::RGBA8_SRGB;
    pd.color = colorFmt_;
    pipe_ = dev.createPipeline(pd);
    // passe de sombra: so posicao, grava z da luz num alvo R32F, sem culling (as duas faces projetam sombra)
    rhi::ShaderDesc svs = vs, sps = ps;
    svs.name = "shadow.vs"; sps.name = "shadow.ps"; svs.source = shadowVsSrc; sps.source = shadowPsSrc;
    svs.entry = "VSMain"; sps.entry = "PSMain";
    shVs_ = dev.createShader(svs); shPs_ = dev.createShader(sps);
    rhi::PipelineDesc sd; sd.vs = shVs_; sd.ps = shPs_; sd.name = "shadow"; sd.color = rhi::Format::R32F; sd.cull = rhi::CullMode::None;
    sd.layout = {{3, 0}}; sd.vertexStride = 32; sd.uniformNames = {"ShadowBuffer"};
    shPipe_ = dev.createPipeline(sd);
    frameCb_ = dev.createBuffer({sizeof(FrameCB), rhi::BufferUsage::Uniform, true}, nullptr);
    lightCb_ = dev.createBuffer({sizeof(LightCB), rhi::BufferUsage::Uniform, true}, nullptr);
    objCb_ = dev.createBuffer({sizeof(ObjectCB), rhi::BufferUsage::Uniform, true}, nullptr);
    shCb_ = dev.createBuffer({sizeof(ShadowCB), rhi::BufferUsage::Uniform, true}, nullptr);
    if (pipe_.isNull() || shPipe_.isNull() || frameCb_.isNull() || lightCb_.isNull() || objCb_.isNull() || shCb_.isNull()) { release(); return false; }
    ensureShadowTarget(1024); // alvo "vazio" para o shader sempre ter uma textura ligada
    resize(w, h);
    return !color_.isNull();
  }

  // Escolhe o alvo de desenho (cor+profundidade) para w*h. As janelas Scene e Game desenham com tamanhos
  // diferentes no mesmo quadro; por isso guardamos um alvo por tamanho (antes, recriavamos as texturas a cada
  // troca de janela, duas vezes por quadro). Alvos sem uso por 120 quadros sao liberados.
  void resize(u32 w, u32 h) {
    if (!dev_) return;
    w_ = std::max(1u, w); h_ = std::max(1u, h);
    const u64 key = (static_cast<u64>(w_) << 32) | h_;
    auto it = targets_.find(key);
    if (it == targets_.end()) {
      Target t;
      t.color = dev_->createTexture({w_, h_, 1, colorFmt_, true}, nullptr);
      t.depth = dev_->createTexture({w_, h_, 1, rhi::Format::D32F, true}, nullptr);
      it = targets_.emplace(key, t).first;
    }
    it->second.lastUse = frame_;
    color_ = it->second.color; depth_ = it->second.depth;
    for (auto i = targets_.begin(); i != targets_.end();) {
      if (frame_ - i->second.lastUse > 120) { dev_->destroy(i->second.color); dev_->destroy(i->second.depth); i = targets_.erase(i); }
      else ++i;
    }
  }

  // Liga as sombras do sol neste quadro (chamar entre begin() e end()). fr = makeSunShadow(...).
  // reuse = true: nada que projeta sombra nem a luz mudou ("sleeper"): reaproveita o shadow map do quadro anterior (pula o passe de sombra).
  void setShadow(const ShadowSettings& st, const ShadowFrustum& fr, bool reuse = false) {
    shadowSet_ = st; shadowFr_ = fr; shadowOn_ = st.enabled && fr.valid;
    shadowSkip_ = shadowOn_ && reuse && shadowValid_ && shW_ == static_cast<u32>(std::clamp(st.resolution, 128, 4096));
  }
  bool shadowPassSkipped() const { return shadowSkip_; }
  bool shadowsActive() const { return shadowOn_; }

  // begin/draw/end: begin() so guarda os parametros e draw() so ANOTA os objetos; tudo e enviado a GPU em end(),
  // na ordem certa: 1) passe de sombra (so objetos que projetam), 2) passe principal (agrupado por malha, o que
  // reduz trocas de buffer). Assim o chamador nao precisa saber da ordem dos passes.
  void begin(const Camera& cam, const std::vector<Light>& lights, Vec3 ambient = {0.03f, 0.03f, 0.04f}, Vec3 clear = {0.02f, 0.025f, 0.035f}) {
    if (!dev_) return;
    ++frame_;
    cam_ = cam; lights_ = lights; ambient_ = ambient; clear_ = clear;
    draws_.clear(); shadowOn_ = false; shadowSkip_ = false; open_ = true;
  }

  void draw(const Mesh& m, const Mat4& world, const Material& mat) {
    if (!open_) return;
    GpuMesh& g = upload(m);
    if (g.index.isNull()) return;
    draws_.push_back({&g, world, mat});
  }

  void end() {
    if (!open_ || !dev_) return;
    open_ = false;
    // ---- 1) sombras ----
    const bool sh = shadowOn_;
    if (sh) ensureShadowTarget(static_cast<u32>(std::clamp(shadowSet_.resolution, 128, 4096)));
    if (sh && !shadowSkip_) {
      auto cl = dev_->beginCommands();
      cl->begin(shColor_, shDepth_, 1.f, 1.f, 1.f, 1.f); // limpa para "longe" (z = 1)
      cl->setPipeline(shPipe_);
      for (const Draw& d : draws_) {
        if (!d.mat.castShadow) continue;
        ShadowCB sc{shadowFr_.viewProj, d.world};
        dev_->updateBuffer(shCb_, &sc, sizeof sc);
        cl->setUniform(0, shCb_);
        cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
        cl->drawIndexed(d.mesh->count, 1);
      }
      cl->end(); dev_->submit(*cl); shadowValid_ = true;
    }
    // ---- 2) passe principal ----
    const Mat4 vp = Mat4::perspective(cam_.fovY, static_cast<f32>(w_) / static_cast<f32>(h_), cam_.nearZ, cam_.farZ) * Mat4::lookAt(cam_.eye, cam_.target, cam_.up);
    FrameCB fc; fc.viewProj = vp; fc.camera[0] = cam_.eye.x; fc.camera[1] = cam_.eye.y; fc.camera[2] = cam_.eye.z;
    dev_->updateBuffer(frameCb_, &fc, sizeof fc);
    LightCB lc{}; lc.count = static_cast<u32>(std::min<size_t>(lights_.size(), 64));
    for (u32 i = 0; i < lc.count; ++i) lc.lights[i] = toGpu(lights_[i], (sh && lights_[i].castShadow) ? 0u : 0xFFFFFFFFu); // shadowIndex 0 = recebe a sombra do sol
    lc.ambient[0] = ambient_.x; lc.ambient[1] = ambient_.y; lc.ambient[2] = ambient_.z;
    if (sh) {
      lc.lightViewProj = shadowFr_.viewProj;
      lc.shadowParams[0] = shadowSet_.bias; lc.shadowParams[1] = 1.f / static_cast<f32>(shW_); lc.shadowParams[2] = shadowSet_.strength; lc.shadowParams[3] = shadowSet_.normalBias * shadowFr_.texelWorld;
      lc.shadowInfo[0] = static_cast<f32>(shW_); lc.shadowInfo[1] = static_cast<f32>(std::clamp(shadowSet_.pcf, 0, 2));
    }
    dev_->updateBuffer(lightCb_, &lc, sizeof lc);
    std::stable_sort(draws_.begin(), draws_.end(), [](const Draw& a, const Draw& b) { return a.mesh->vertex.index < b.mesh->vertex.index; });
    cl_ = dev_->beginCommands();
    // Alvo UNORM (OpenGL): o clear nao passa pelo hardware sRGB, entao convertemos aqui para bater com Vulkan/D3D12.
    const bool lin2srgb = colorFmt_ == rhi::Format::RGBA8;
    auto cv = [&](f32 v) { return lin2srgb ? std::pow(std::max(v, 0.f), 1.f / 2.2f) : v; };
    cl_->begin(color_, depth_, cv(clear_.x), cv(clear_.y), cv(clear_.z), 1.f);
    cl_->setPipeline(pipe_);
    cl_->setUniform(0, frameCb_); cl_->setUniform(1, lightCb_);
    cl_->setTexture(0, shColor_);
    const GpuMesh* last = nullptr;
    for (const Draw& d : draws_) {
      ObjectCB oc{}; oc.world = d.world; oc.color[0] = d.mat.albedo.x; oc.color[1] = d.mat.albedo.y; oc.color[2] = d.mat.albedo.z; oc.color[3] = 1.f;
      oc.metallic = d.mat.metallic; oc.roughness = d.mat.roughness; oc.receiveShadow = d.mat.receiveShadow ? 1.f : 0.f;
      dev_->updateBuffer(objCb_, &oc, sizeof oc);
      cl_->setUniform(2, objCb_);
      if (d.mesh != last) { cl_->setVertexBuffer(d.mesh->vertex); cl_->setIndexBuffer(d.mesh->index); last = d.mesh; }
      cl_->drawIndexed(d.mesh->count, 1);
    }
    cl_->end(); dev_->submit(*cl_); cl_.reset();
    drawCount_ = static_cast<u32>(draws_.size());
  }

  bool readback(std::vector<u8>& rgba, u32& w, u32& h) { return dev_ && dev_->readTexture(color_, rgba, w, h); }
  rhi::TextureId colorTarget() const { return color_; }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  u32 draws() const { return drawCount_; }
  size_t cachedMeshes() const { return meshes_.size(); }

  // Libera a copia na GPU de uma malha (chamar se a malha for destruida/alterada).
  void forget(const Mesh& m) {
    auto it = meshes_.find(&m);
    if (it == meshes_.end()) return;
    dev_->destroy(it->second.vertex); dev_->destroy(it->second.index); meshes_.erase(it);
  }

private:
  struct GpuMesh { rhi::BufferId vertex, index; u32 count = 0; };
  struct Draw { const GpuMesh* mesh; Mat4 world; Material mat; };
  struct Target { rhi::TextureId color, depth; u64 lastUse = 0; };

  GpuMesh& upload(const Mesh& m) {
    auto it = meshes_.find(&m);
    if (it != meshes_.end()) return it->second;
    GpuMesh g;
    std::vector<f32> v; v.reserve(m.pos.size() * 8);
    // Layout de vertice fixo (stride 32): POSITION f3 @0, NORMAL f3 @12, TEXCOORD0 f2 @24 (0 se a malha nao tem UV).
    const bool hasUv = m.uv.size() == m.pos.size() * 2;
    for (size_t i = 0; i < m.pos.size(); ++i) { v.insert(v.end(), {m.pos[i].x, m.pos[i].y, m.pos[i].z, m.normal[i].x, m.normal[i].y, m.normal[i].z, hasUv ? m.uv[2 * i] : 0.f, hasUv ? m.uv[2 * i + 1] : 0.f}); }
    g.vertex = dev_->createBuffer({v.size() * sizeof(f32), rhi::BufferUsage::Vertex, false}, v.data());
    g.index = dev_->createBuffer({m.idx.size() * sizeof(u32), rhi::BufferUsage::Index, false}, m.idx.data());
    g.count = static_cast<u32>(m.idx.size());
    return meshes_[&m] = g;
  }

  void ensureShadowTarget(u32 res) {
    if (shW_ == res && !shColor_.isNull()) return;
    if (!shColor_.isNull()) dev_->destroy(shColor_);
    if (!shDepth_.isNull()) dev_->destroy(shDepth_);
    shColor_ = dev_->createTexture({res, res, 1, rhi::Format::R32F, true}, nullptr);
    shDepth_ = dev_->createTexture({res, res, 1, rhi::Format::D32F, true}, nullptr);
    shW_ = res; shadowValid_ = false;
  }

  void release() {
    if (!dev_) return;
    for (auto& [k, g] : meshes_) { dev_->destroy(g.vertex); dev_->destroy(g.index); }
    meshes_.clear();
    for (auto& [k, t] : targets_) { dev_->destroy(t.color); dev_->destroy(t.depth); }
    targets_.clear(); color_ = depth_ = kNullHandle;
    for (auto* h : {&frameCb_, &lightCb_, &objCb_, &shCb_, &shColor_, &shDepth_, &pipe_, &shPipe_, &vs_, &ps_, &shVs_, &shPs_}) if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    shW_ = 0; draws_.clear(); dev_ = nullptr;
  }

  rhi::IDevice* dev_ = nullptr;
  rhi::ShaderId vs_, ps_, shVs_, shPs_;
  rhi::PipelineId pipe_, shPipe_;
  rhi::BufferId frameCb_, lightCb_, objCb_, shCb_;
  rhi::TextureId shColor_, shDepth_; u32 shW_ = 0;   // mapa de sombra (R32F) + profundidade para o teste do passe de sombra
  ShadowSettings shadowSet_; ShadowFrustum shadowFr_; bool shadowOn_ = false, open_ = false, shadowSkip_ = false, shadowValid_ = false;
  Camera cam_; std::vector<Light> lights_; Vec3 ambient_, clear_;
  std::vector<Draw> draws_;
  rhi::TextureId color_, depth_;
  rhi::Format colorFmt_ = rhi::Format::RGBA8_SRGB;
  std::unordered_map<u64, Target> targets_;
  u64 frame_ = 0;
  std::unique_ptr<rhi::ICommandList> cl_;
  std::unordered_map<const Mesh*, GpuMesh> meshes_;
  u32 w_ = 0, h_ = 0, drawCount_ = 0;
};

} // namespace tool
