// ToolEngine - GpuRenderer: desenha a cena 3D iluminada (PBR) sobre o RHI (Diligent: Vulkan / D3D12).
#pragma once
#include "tool/core/parallel.hpp"
#include "tool/render/lighting.hpp"
#include "tool/render/instance_batch.hpp"
#include "tool/render/indirect_plan.hpp"
#include "tool/render/render_queue.hpp"
#include "tool/render/frustum_cull.hpp"
#include "tool/render/fsr1.hpp"
#include "tool/render/render_stats.hpp"
#include "tool/render/soft_renderer.hpp"
#include "tool/rhi/rhi.hpp"

#include "LightPrism/Backends/DiligentDevice.hpp"
#include "LightPrism/Cluster/ClusterSystem.hpp"

#include <cstring>
#include <filesystem>
#include <set>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace tool {

using render::BatchItem;
using render::BatchPlan;
using render::InstanceBatch;
using render::InstanceGPU;
using render::planInstanceBatches;

inline bool loadHlslRec(const std::string& dir, const std::string& file, std::string& out, std::set<std::string>& seen, int depth) {
  if (depth > 12) return false;
  std::error_code ec;
  const std::filesystem::path full = std::filesystem::path(dir) / file;
  const std::string key = std::filesystem::weakly_canonical(full, ec).string();
  if (!ec && !seen.insert(key).second) return true;
  std::ifstream f(full, std::ios::binary);
  if (!f) return false;
  const size_t slash = file.find_last_of('/');
  const std::string sub = slash == std::string::npos ? std::string() : file.substr(0, slash + 1);
  std::string line;
  while (std::getline(f, line)) {
    const size_t p = line.find("#include");
    if (p != std::string::npos && line.find('"', p) != std::string::npos) {
      const size_t a = line.find('"', p) + 1, b = line.find('"', a);
      if (b == std::string::npos) return false;
      if (!loadHlslRec(dir, sub + line.substr(a, b - a), out, seen, depth + 1)) return false;
    } else out += line + "\n";
  }
  return true;
}

inline bool loadHlsl(const std::string& dir, const std::string& file, std::string& out) {
  std::set<std::string> seen;
  return loadHlslRec(dir, file, out, seen, 0);
}

struct FrameCB { Mat4 viewProj; Mat4 view; f32 camera[3]; f32 pad = 0; };
struct ObjectCB { Mat4 world; f32 color[4]; f32 metallic, roughness, receiveShadow, pad; f32 texInfo[4]; f32 emissive[4]; f32 fogColor[4]; f32 fogParams[4]; };
struct ShadowCB { Mat4 lightViewProj; };
struct ShadowInstanceGPU { Mat4 world; };
struct BatchCB { u32 first; u32 pad[3]; };
struct BloomCB { f32 srcSize[2]; f32 haloSize[2]; f32 dir[2]; f32 threshold, strength; };
// Culling GPU (frustum_cull.hlsl): planos do frustum e descritor de lote (32 B, igual a CullBatch do HLSL).
struct CullCB { f32 planes[6][4]; };
struct CullBatchGPU { u32 first, count, indexCount, argsIndex; f32 sphere[4]; };
static_assert(sizeof(CullCB) == 96 && sizeof(CullBatchGPU) == 32 && sizeof(rhi::DrawIndexedIndirectArgs) == 20, "layout do culling GPU");
static_assert(sizeof(FrameCB) == 144 && sizeof(ObjectCB) == 160 && sizeof(ShadowCB) == 64 && sizeof(ShadowInstanceGPU) == 64 && sizeof(BatchCB) == 16 && sizeof(BloomCB) == 32, "layout de cbuffer");
static_assert(sizeof(InstanceGPU) == sizeof(ObjectCB), "layout de instancia deve bater com ObjectCB");

struct PointShadowLightGPU { f32 posType[4]; f32 params[4]; Mat4 vp[6]; };
struct PointShadowGPU { u32 count; u32 pad[3]; PointShadowLightGPU lights[kMaxPointShadowLights]; };
static_assert(sizeof(PointShadowLightGPU) == 416 && sizeof(PointShadowGPU) == 16 + 4 * 416, "layout de PointShadowCB");
static_assert(sizeof(LightPrism::CsmGPU) == 352 && sizeof(LightPrism::Ibl::IblGPU) == 160, "layout dos cbuffers da Light Prism");

inline const std::vector<std::string>& pbrUniformNames() {
  static const std::vector<std::string> n = {"FrameBuffer", "BatchBuffer", "GlobalLightCB", "SunLightCB", "CsmCB", "IblCB", "ClusterGridCB", "PointShadowCB"};
  return n;
}
// Slot 11 ("NormalMap", normal map do material) so existe nos backends que compilam o HLSL; o GLSL 430 pre-gerado (OpenGL)
// ainda nao tem esse mapa, entao la o nome fica de fora e o material ignora normal map/tiling/cutoff.
inline constexpr u32 kNormalMapSlot = 11;
inline const std::vector<std::string>& pbrTextureNames(bool withNormalMap = false) {
  static const std::vector<std::string> n = {"AlbedoMap", "CsmMap0", "CsmMap1", "CsmMap2", "CsmMap3", "PointShadowMap0", "PointShadowMap1", "PointShadowMap2", "PointShadowMap3", "AoMap", "IblBrdfLut"};
  static const std::vector<std::string> nn = [] { auto v = n; v.push_back("NormalMap"); return v; }();
  return withNormalMap ? nn : n;
}

// Empacota forca do normal map (0..2) e alpha cutoff (0..1) em UM float exato (24 bits): forcaQ*1024 + cutoffQ, cada metade
// 0..1023. Vai em InstanceGPU.fogParams[3] (sobra do layout de 160 B); o VS do pbr.hlsl desfaz (UnpackNormalCutoff).
inline f32 packMaterialExtra(f32 normalStrength, f32 alphaCutoff) {
  auto q = [](f32 v01) { return static_cast<f32>(static_cast<int>(std::clamp(v01, 0.f, 1.f) * 1023.f + 0.5f)); };
  return q(normalStrength * 0.5f) * 1024.f + q(alphaCutoff);
}
inline const std::vector<std::string>& pbrStorageNames() {
  static const std::vector<std::string> n = {"PointLightsBuffer", "SpotLightsBuffer", "LightGrid", "LightIndexList", "Instances"};
  return n;
}

class GpuRenderer {
public:
  ~GpuRenderer() { release(); }
  GpuRenderer() = default;
  GpuRenderer(const GpuRenderer&) = delete;
  GpuRenderer& operator=(const GpuRenderer&) = delete;
  void shutdown() { release(); }

  bool init(rhi::IDevice& dev, std::unique_ptr<LightPrism::IGraphicsDevice> lp, const std::string& shaderDir, u32 w, u32 h) {
    release();
    if (!lp) { debug().report("TOOL-E142", Severity::Error, "dispositivo Light Prism indisponivel (backend sem Diligent)"); return false; }
    dev_ = &dev; lp_ = std::move(lp);
    const std::string hlslDir = shaderDir + "/hlsl";
    LightPrism::ClusterSystemDesc cd;
    cd.shaderDirectory = shaderDir + "/lightprism";
    clusters_ = std::make_unique<LightPrism::ClusterSystem>(cd);

    std::string src;
    if (!loadHlsl(hlslDir, "pbr.hlsl", src)) { debug().report("TOOL-E140", Severity::Error, "nao achei pbr.hlsl em " + hlslDir); release(); return false; }
    std::string legacySrc;
    if (!loadHlsl(hlslDir, "pbr_object.hlsl", legacySrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei pbr_object.hlsl em " + hlslDir); release(); return false; }
    std::string sh;
    if (!loadHlsl(hlslDir, "shadow.hlsl", sh)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow.hlsl em " + hlslDir); release(); return false; }
    std::string ssaoSrc, prepassSrc;
    if (!loadHlsl(hlslDir, "ssao.hlsl", ssaoSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei ssao.hlsl em " + hlslDir); release(); return false; }
    if (!loadHlsl(hlslDir, "prepass.hlsl", prepassSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei prepass.hlsl em " + hlslDir); release(); return false; }
    std::string shp, shClear;
    if (!loadHlsl(hlslDir, "shadow_point.hlsl", shp)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow_point.hlsl em " + hlslDir); release(); return false; }
    if (!loadHlsl(hlslDir, "shadow_clear.hlsl", shClear)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow_clear.hlsl em " + hlslDir); release(); return false; }

    const bool gl = dev.backend() == rhi::Backend::GLES;
    if (gl) debug().report("TOOL-W142", Severity::Warning, "OpenGL: pipeline em GLSL 430 (gerado de HLSL). Pendente: bloom e sombra pontual ainda diferem do Vulkan; use Vulkan ou D3D12 para a versao final.");
    auto mk = [&](rhi::ShaderDesc d) -> rhi::ShaderId {
      if (!gl) return dev.createShader(d);
      std::ifstream in(shaderDir + "/glsl/" + d.name + ".430.glsl", std::ios::binary);
      if (!in) { debug().report("TOOL-E140", Severity::Error, "nao achei o GLSL '" + d.name + ".430.glsl' em " + shaderDir + "/glsl"); }
      d.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      d.lang = rhi::ShaderLang::GLSL; d.entry = "main";
      return dev.createShader(d);
    };

    rhi::ShaderDesc vs; vs.stage = rhi::ShaderStage::Vertex; vs.entry = "VSMain"; vs.name = "pbr.vs"; vs.source = src;
    rhi::ShaderDesc ps; ps.stage = rhi::ShaderStage::Pixel; ps.entry = "PSMain"; ps.name = "pbr.ps"; ps.source = src;
    vs_ = mk(vs); ps_ = mk(ps);
    rhi::ShaderDesc legacyVs; legacyVs.stage = rhi::ShaderStage::Vertex; legacyVs.entry = "VSMain"; legacyVs.name = "pbr_object.vs"; legacyVs.source = legacySrc;
    rhi::ShaderDesc legacyPs; legacyPs.stage = rhi::ShaderStage::Pixel; legacyPs.entry = "PSMain"; legacyPs.name = "pbr_object.ps"; legacyPs.source = legacySrc;
    legacyVs_ = mk(legacyVs); legacyPs_ = mk(legacyPs);
    rhi::PipelineDesc pd; pd.vs = vs_; pd.ps = ps_; pd.name = "pbr";
    pd.layout = {{3, 0}, {3, 12}, {2, 24}}; pd.vertexStride = 32;
    normalSlot_ = !gl;   // normal map do material so no HLSL (ver pbrTextureNames)
    pd.uniformNames = pbrUniformNames(); pd.textureNames = pbrTextureNames(normalSlot_); pd.storageNames = pbrStorageNames();
    colorFmt_ = rhi::Format::RGBA8_SRGB;
    pd.color = colorFmt_;
    pipe_ = dev.createPipeline(pd);
    {   // variante dupla face (material.doubleSided): mesmo pipeline sem cull de faces
      rhi::PipelineDesc dd = pd; dd.name = "pbr_ds"; dd.cull = rhi::CullMode::None;
      pipeDs_ = dev.createPipeline(dd);
    }

    rhi::ShaderDesc emi = ps; emi.entry = "EmissionPS"; emi.name = "pbr.emi";
    emiPs_ = mk(emi);
    rhi::PipelineDesc ed; ed.vs = vs_; ed.ps = emiPs_; ed.name = "emission"; ed.depthWrite = false; ed.depthTest = true;
    ed.layout = pd.layout; ed.vertexStride = pd.vertexStride; ed.color = rhi::Format::RGBA16F;
    ed.uniformNames = {"FrameBuffer", "BatchBuffer"}; ed.textureNames.clear(); ed.storageNames = {"Instances"};
    emiPipe_ = dev.createPipeline(ed);

    rhi::PipelineDesc td = pd; td.name = "transparent"; td.blend = true; td.depthWrite = false;
    transPipe_ = dev.createPipeline(td);
    { rhi::PipelineDesc dd = td; dd.name = "transparent_ds"; dd.cull = rhi::CullMode::None; transPipeDs_ = dev.createPipeline(dd); }

    rhi::ShaderDesc svs = vs, sps = ps;
    svs.name = "shadow.vs"; sps.name = "shadow.ps"; svs.source = sh; sps.source = sh;
    svs.entry = "VSMain"; sps.entry = "PSMain";
    shVs_ = mk(svs); shPs_ = mk(sps);
    rhi::PipelineDesc sd; sd.vs = shVs_; sd.ps = shPs_; sd.name = "shadow"; sd.color = rhi::Format::R32F; sd.cull = rhi::CullMode::None;
    sd.layout = {{3, 0}}; sd.vertexStride = 32; sd.uniformNames = {"ShadowBuffer", "BatchBuffer"}; sd.storageNames = {"Instances"};
    shPipe_ = dev.createPipeline(sd);

    rhi::ShaderDesc pvs = svs, pps = sps;
    pvs.name = "shadowpoint.vs"; pps.name = "shadowpoint.ps"; pvs.source = shp; pps.source = shp;
    ptVs_ = mk(pvs); ptPs_ = mk(pps);
    rhi::PipelineDesc pd2 = sd; pd2.vs = ptVs_; pd2.ps = ptPs_; pd2.name = "shadowpoint";
    ptPipe_ = dev.createPipeline(pd2);
    rhi::ShaderDesc clearPs; clearPs.stage = rhi::ShaderStage::Pixel; clearPs.entry = "PSMain";
    clearPs.name = "shadowclear.ps"; clearPs.source = shClear; pointClearPs_ = mk(clearPs);

    rhi::ShaderDesc pre; pre.stage = rhi::ShaderStage::Vertex; pre.entry = "VSMain"; pre.name = "prepass.vs"; pre.source = prepassSrc;
    rhi::ShaderDesc prePs = pre; prePs.stage = rhi::ShaderStage::Pixel; prePs.entry = "PSMain"; prePs.name = "prepass.ps";
    preVs_ = mk(pre); prePs_ = mk(prePs);
    rhi::PipelineDesc pp; pp.vs = preVs_; pp.ps = prePs_; pp.name = "prepass";
    pp.layout = {{3, 0}, {3, 12}, {2, 24}}; pp.vertexStride = 32; pp.uniformNames = {"FrameBuffer", "BatchBuffer"}; pp.storageNames = {"Instances"};
    pp.color = rhi::Format::RGBA16F; pp.cull = rhi::CullMode::Back;
    prePipe_ = dev.createPipeline(pp);

    rhi::ShaderDesc fsv; fsv.stage = rhi::ShaderStage::Vertex; fsv.entry = "VSMain"; fsv.name = "ssao.vs"; fsv.source = ssaoSrc;
    rhi::ShaderDesc ssP = fsv; ssP.stage = rhi::ShaderStage::Pixel; ssP.entry = "SsaoMain"; ssP.name = "ssao.ps";
    rhi::ShaderDesc blP = fsv; blP.stage = rhi::ShaderStage::Pixel; blP.entry = "SsaoBlur"; blP.name = "ssaoblur.ps";
    fsVs_ = mk(fsv); ssaoPs_ = mk(ssP); blurPs_ = mk(blP);
    rhi::PipelineDesc clearDesc; clearDesc.vs = fsVs_; clearDesc.ps = pointClearPs_;
    clearDesc.name = "shadowclear"; clearDesc.layout = {{3, 0}}; clearDesc.vertexStride = 12;
    clearDesc.color = rhi::Format::R32F; clearDesc.depthTest = false; clearDesc.depthWrite = false; clearDesc.cull = rhi::CullMode::None;
    pointClearPipe_ = dev.createPipeline(clearDesc);

    rhi::PipelineDesc sp; sp.vs = fsVs_; sp.ps = ssaoPs_; sp.name = "ssao"; sp.layout = {{3, 0}}; sp.vertexStride = 12;
    sp.uniformNames = {"SsaoCB"}; sp.textureNames = {"GBuf"}; sp.color = rhi::Format::R32F; sp.cull = rhi::CullMode::None;
    ssaoPipe_ = dev.createPipeline(sp);
    rhi::PipelineDesc bp = sp; bp.ps = blurPs_; bp.name = "ssaoblur"; bp.textureNames = {"GBuf", "RawAo"};
    blurPipe_ = dev.createPipeline(bp);

    {
      std::string bloomSrc;
      if (!loadHlsl(hlslDir, "bloom.hlsl", bloomSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei bloom.hlsl em " + hlslDir); release(); return false; }
      rhi::ShaderDesc bs; bs.stage = rhi::ShaderStage::Pixel; bs.source = bloomSrc;
      bs.name = "bloom.thr"; bs.entry = "BloomThreshold"; bloomThrPs_ = mk(bs);
      bs.name = "bloom.blur"; bs.entry = "BloomBlur"; bloomBlurPs_ = mk(bs);
      bs.name = "bloom.comp"; bs.entry = "BloomComposite"; bloomCompPs_ = mk(bs);
      rhi::PipelineDesc tp; tp.vs = fsVs_; tp.ps = bloomThrPs_; tp.name = "bloomthr"; tp.layout = {{3, 0}}; tp.vertexStride = 12;
      tp.uniformNames = {"BloomCB"}; tp.textureNames = {"Src"}; tp.color = rhi::Format::RGBA16F; tp.cull = rhi::CullMode::None;
      bloomThrPipe_ = dev.createPipeline(tp);
      rhi::PipelineDesc bb = tp; bb.ps = bloomBlurPs_; bb.name = "bloomblur"; bb.textureNames = {"Halo"};
      bloomBlurPipe_ = dev.createPipeline(bb);
      rhi::PipelineDesc cp = tp; cp.ps = bloomCompPs_; cp.name = "bloomcomp"; cp.textureNames = {"Src", "Halo"}; cp.color = colorFmt_;
      bloomCompPipe_ = dev.createPipeline(cp);
      bloomCbThr_ = dev.createBuffer({sizeof(BloomCB), rhi::BufferUsage::Uniform, true}, nullptr);
      bloomCbH_ = dev.createBuffer({sizeof(BloomCB), rhi::BufferUsage::Uniform, true}, nullptr);
      bloomCbV_ = dev.createBuffer({sizeof(BloomCB), rhi::BufferUsage::Uniform, true}, nullptr);
      bloomCbComp_ = dev.createBuffer({sizeof(BloomCB), rhi::BufferUsage::Uniform, true}, nullptr);
    }

    // Culling de frustum por compute (fase 1, so o passe opaco). Opcional: se o backend nao tem compute/indirect ou o
    // shader falhar, o renderer segue no caminho CPU de sempre.
    if (dev.supportsCompute() && dev.supportsIndirect() && !gl) {
      std::string cullSrc;
      if (loadHlsl(hlslDir, "frustum_cull.hlsl", cullSrc)) {
        rhi::ShaderDesc cs; cs.stage = rhi::ShaderStage::Compute; cs.entry = "CullInstancesCS"; cs.name = "frustum_cull.cs"; cs.source = cullSrc;
        cullCs_ = dev.createShader(cs);
        if (!cullCs_.isNull()) {
          rhi::PipelineDesc cd2; cd2.cs = cullCs_; cd2.name = "frustum_cull";
          cd2.uniformNames = {"CullCB"}; cd2.storageNames = {"CullBatches", "SrcInstances"}; cd2.storageRWNames = {"DstInstances", "DrawArgs"};
          cullPipe_ = dev.createPipeline(cd2);
        }
        cullCb_ = dev.createBuffer({sizeof(CullCB), rhi::BufferUsage::Uniform, true}, nullptr);
      }
      if (cullPipe_.isNull() || cullCb_.isNull()) debug().report("TOOL-W143", Severity::Warning, "culling de frustum na GPU indisponivel; usando o caminho CPU");
    }

    // FSR 1 (EASU + RCAS em pixel shader). Opcional: em OpenGL (GLSL pre-gerado) ou se o shader/pipeline falhar, fica
    // indisponivel e o chamador cai no upscale de sempre (fsr1Available() == false).
    fsrOk_ = false; fsrWarned_ = false; fsrWhy_.clear();
    if (!gl) {
      fsrOk_ = fsr_.init(dev, hlslDir, colorFmt_);
      if (!fsrOk_) fsrWhy_ = fsr_.whyNot();
    } else fsrWhy_ = "OpenGL nao tem o FSR 1";

    static const float fsTri[9] = {-1.f, -1.f, 0.f, 3.f, -1.f, 0.f, -1.f, 3.f, 0.f};
    static const u32 fsIdx[3] = {0, 1, 2};
    fsVb_ = dev.createBuffer({sizeof fsTri, rhi::BufferUsage::Vertex, false}, fsTri);
    fsIb_ = dev.createBuffer({sizeof fsIdx, rhi::BufferUsage::Index, false}, fsIdx);
    ssaoCb_ = dev.createBuffer({sizeof(LightPrism::Ssao::SsaoGPU), rhi::BufferUsage::Uniform, true}, nullptr);
    frameCb_ = dev.createBuffer({sizeof(FrameCB), rhi::BufferUsage::Uniform, true}, nullptr);
    objCb_ = dev.createBuffer({sizeof(ObjectCB), rhi::BufferUsage::Uniform, true}, nullptr);
    batchCb_ = dev.createBuffer({sizeof(BatchCB), rhi::BufferUsage::Uniform, true}, nullptr);
    shCb_ = dev.createBuffer({sizeof(ShadowCB), rhi::BufferUsage::Uniform, true}, nullptr);
    csmCb_ = dev.createBuffer({sizeof(LightPrism::CsmGPU), rhi::BufferUsage::Uniform, true}, nullptr);
    iblCb_ = dev.createBuffer({sizeof(LightPrism::Ibl::IblGPU), rhi::BufferUsage::Uniform, true}, nullptr);

    {
      const LightPrism::Ibl::BrdfLut& lut = sharedBrdfLut();
      std::vector<u8> px(size_t(lut.size) * lut.size * 4);
      for (size_t t = 0; t < size_t(lut.size) * lut.size; ++t) {
        auto q = [](float v) { return static_cast<u8>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        px[t * 4 + 0] = q(lut.ab[t * 2 + 0]); px[t * 4 + 1] = q(lut.ab[t * 2 + 1]); px[t * 4 + 2] = 0; px[t * 4 + 3] = 255;
      }
      brdfTex_ = dev.createTexture({lut.size, lut.size, 1, rhi::Format::RGBA8, false}, px.data());
    }

    pointCb_ = dev.createBuffer({sizeof(PointShadowGPU), rhi::BufferUsage::Uniform, true}, nullptr);
    if (prePipe_.isNull() || ssaoPipe_.isNull() || blurPipe_.isNull() || pointClearPipe_.isNull() || fsVb_.isNull() || fsIb_.isNull() || ssaoCb_.isNull() || pipe_.isNull() || transPipe_.isNull() || legacyVs_.isNull() || legacyPs_.isNull() || shPipe_.isNull() || ptPipe_.isNull() || frameCb_.isNull() || objCb_.isNull() || batchCb_.isNull() || shCb_.isNull() || csmCb_.isNull() || iblCb_.isNull() || pointCb_.isNull()) { release(); return false; }
    ensureShadowTargets(1024);
    ensurePointTargets(64);
    resize(w, h);
    return !color_.isNull();
  }

  void resize(u32 w, u32 h) {
    if (!dev_) return;
    w_ = std::max(1u, w); h_ = std::max(1u, h);
    const u64 key = (static_cast<u64>(w_) << 32) | h_;
    auto it = targets_.find(key);
    if (it == targets_.end()) {
      Target t;
      t.color = dev_->createTexture({w_, h_, 1, colorFmt_, true}, nullptr);
      t.depth = dev_->createTexture({w_, h_, 1, rhi::Format::D32F, true}, nullptr);
      t.gbuf = dev_->createTexture({w_, h_, 1, rhi::Format::RGBA16F, true}, nullptr);
      t.aoRaw = dev_->createTexture({w_, h_, 1, rhi::Format::R32F, true}, nullptr);
      t.ao = dev_->createTexture({w_, h_, 1, rhi::Format::R32F, true}, nullptr);
      const u32 hw = std::max(1u, w_ / 2), hh = std::max(1u, h_ / 2);
      t.bloomA = dev_->createTexture({hw, hh, 1, rhi::Format::RGBA16F, true}, nullptr);
      t.bloomB = dev_->createTexture({hw, hh, 1, rhi::Format::RGBA16F, true}, nullptr);
      t.out = dev_->createTexture({w_, h_, 1, colorFmt_, true}, nullptr);
      t.glow = dev_->createTexture({w_, h_, 1, rhi::Format::RGBA16F, true}, nullptr);
      it = targets_.emplace(key, t).first;
    }
    it->second.lastUse = frame_;
    color_ = it->second.color; depth_ = it->second.depth; gbuf_ = it->second.gbuf; aoRaw_ = it->second.aoRaw; ao_ = it->second.ao;
    bloomA_ = it->second.bloomA; bloomB_ = it->second.bloomB; outFull_ = it->second.out; glow_ = it->second.glow;
    for (auto i = targets_.begin(); i != targets_.end();) {
      if (frame_ - i->second.lastUse > 120) { destroyTarget(i->second); i = targets_.erase(i); }
      else ++i;
    }
  }

  bool setLighting(LightRig& rig, const Mat4& view, const Mat4& proj, bool reuse = false) {
    if (!dev_ || !lp_) return false;
    const LightFrame& fr = rig.frame();
    LightPrism::LightingSystem& sys = rig.system();
    if (!sys.UploadToGPU(*lp_)) { lastError_ = "Light Prism: falha ao enviar as luzes para a GPU"; return false; }
    LightPrism::ClusterCamera cam = LightPrism::MakeClusterCamera(LightRig::toLp(view), LightRig::toLp(proj), w_, h_, cam_zNear_, cam_zFar_);
    // Passa a textura de profundidade da cena para permitir compactacao de clusters ativos via Z-Prepass quando disponivel
    if (!clusters_->Execute(*lp_, sys, rig.lastView(), cam)) { lastError_ = "Light Prism clusters: " + clusters_->LastError(); return false; }

    releaseImports();
    auto imp = [&](LightPrism::IGpuBuffer* b) -> rhi::BufferId {
      if (!b || !b->NativeHandle()) return kNullHandle;
      const rhi::BufferId h = dev_->importBuffer(b->NativeHandle());
      if (!h.isNull()) imports_.push_back(h);
      return h;
    };
    imp_.points = imp(sys.PointLightBuffer());
    imp_.spots = imp(sys.SpotLightBuffer());
    imp_.globals = imp(sys.GlobalsBuffer());
    imp_.sun = imp(sys.SunBuffer());
    imp_.grid = imp(clusters_->GridBuffer());
    imp_.cells = imp(clusters_->LightGridBuffer());
    imp_.indices = imp(clusters_->LightIndexBuffer());
    if (imp_.points.isNull() || imp_.spots.isNull() || imp_.globals.isNull() || imp_.sun.isNull() || imp_.grid.isNull() || imp_.cells.isNull() || imp_.indices.isNull()) {
      lastError_ = "Light Prism: buffers de luz indisponiveis"; return false;
    }

    const bool on = fr.shadows();
    const u32 count = on ? std::min<u32>(fr.csm.gpu.cascadeCount, LightPrism::kMaxCsmCascades) : 0u;
    const int res = on ? std::clamp(static_cast<int>(fr.csm.gpu.shadowMapSize), 128, 4096) : shRes_;
    const bool sameLayout = on && res == static_cast<int>(shW_) && count == shadowCount_;
    shadowOn_ = on; shadowCount_ = count;
    shadowSkip_ = on && reuse && shadowValid_ && sameLayout;
    if (on) ensureShadowTargets(static_cast<u32>(res));
    for (u32 k = 0; k < count; ++k) cascadeVp_[k] = toTool(fr.csm.gpu.cascades[k].viewProj);
    dev_->updateBuffer(csmCb_, &fr.csm.gpu, sizeof(LightPrism::CsmGPU));

    pointCount_ = std::min<u32>(static_cast<u32>(fr.shadowLights.size()), kMaxPointShadowLights);
    pointOn_ = pointCount_ > 0;
    pointRes_ = std::clamp<u32>(fr.pointShadowResolution, 64u, 1024u);
    if (pointOn_) ensurePointTargets(pointRes_);
    PointShadowGPU pg{};
    pg.count = pointCount_;
    for (u32 i = 0; i < pointCount_; ++i) {
      const ShadowLight& sl = fr.shadowLights[i];
      pointLights_[i] = sl;
      PointShadowLightGPU& L = pg.lights[i];
      L.posType[0] = sl.position.x; L.posType[1] = sl.position.y; L.posType[2] = sl.position.z;
      L.posType[3] = sl.type == LightType::Spot ? 1.f : 0.f;
      pointFaces_[i] = std::min<u32>(sl.faces, kMaxPointShadowFaces);
      L.params[0] = sl.nearZ; L.params[1] = static_cast<f32>(pointFaces_[i]); L.params[2] = static_cast<f32>(pointRes_); L.params[3] = 0.f;
      for (u32 f = 0; f < kMaxPointShadowFaces; ++f) { pointVp_[i][f] = sl.vp[f]; L.vp[f] = sl.vp[f]; }
    }
    dev_->updateBuffer(pointCb_, &pg, sizeof pg);
    const LightPrism::Ibl::IblGPU ibl = LightPrism::Ibl::MakeIblGPU(fr.ambient, 1.0f, 1);
    dev_->updateBuffer(iblCb_, &ibl, sizeof(ibl));

    ssaoOn_ = fr.ssaoOn;
    if (ssaoOn_) {
      float proj4[4][4];
      for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) proj4[r][c] = proj.m[c][r];
      const LightPrism::Ssao::SsaoGPU sg = LightPrism::Ssao::MakeSsaoGPU(fr.ssao, w_, h_, proj4);
      dev_->updateBuffer(ssaoCb_, &sg, sizeof sg);
    }
    lastError_.clear();
    return true;
  }

  const std::string& lastError() const { return lastError_; }
  bool shadowPassSkipped() const { return shadowSkip_; }
  bool shadowsActive() const { return shadowOn_; }

  void begin(const Camera& cam, Vec3 clear = {0.02f, 0.025f, 0.035f}, f32 clearAlpha = 1.f) {
    if (!dev_) return;
    ++frame_;
    cam_ = cam; clear_ = clear; clearAlpha_ = clearAlpha;
    cam_zNear_ = cam.nearZ; cam_zFar_ = cam.farZ;
    draws_.clear(); tdraws_.clear(); shadowOn_ = false; shadowSkip_ = false; open_ = true;
    stats_ = {};
    stats_.valid = true;
    if ((frame_ & 63) == 0) for (auto it = meshes_.begin(); it != meshes_.end();) {
      if (frame_ - it->second.lastUse > 240) { freeMesh(it->second); it = meshes_.erase(it); } else ++it;
    }
  }

  void drawDynamic(u32 slot, const Mesh& m, const Mat4& world, const Material& mat) {
    if (!open_ || m.idx.empty()) return;
    GpuMesh& g = dynamicSlot(slot, m);
    if (g.index.isNull()) return;
    addDraw(makeDraw(&g, world, mat));
  }

  void draw(const Mesh& m, const Mat4& world, const Material& mat) {
    if (!open_) return;
    GpuMesh& g = upload(m);
    if (g.index.isNull()) return;
    addDraw(makeDraw(&g, world, mat));
  }

  rhi::TextureId textureFor(const Texture* t, f32& tw, f32& th) {
    tw = th = 0.f;
    if (!dev_ || !t || !t->valid() || t->id == 0) return white_;
    tw = static_cast<f32>(t->w); th = static_cast<f32>(t->h);
    if (auto it = texs_.find(t->id); it != texs_.end()) return it->second;
    const rhi::TextureId id = dev_->createTexture({t->w, t->h, 1, rhi::Format::RGBA8, false}, t->rgba.data());
    if (id.isNull()) { tw = th = 0.f; return white_; }
    texs_[t->id] = id;
    return id;
  }

  void end() {
    if (!open_ || !dev_) return;
    open_ = false;
    std::stable_sort(draws_.begin(), draws_.end(), [](const Draw& a, const Draw& b) { return a.mesh->vertex.index < b.mesh->vertex.index; });
    const Mat4 view = Mat4::lookAt(cam_.eye, cam_.target, cam_.up);
    const Mat4 vp = cam_.projection(static_cast<f32>(w_), static_cast<f32>(h_)) * view;
    const render::Frustum cameraFrustum = render::frustumFromViewProj(vp.m, dev_->backend() != rhi::Backend::GLES);

    // Avaliacao paralela de frustum culling e flags de fila para milhares de objetos
    // Culling GPU ligado: a fila opaca principal recebe TODOS os opacos (o compute descarta os fora do frustum). O frustum
    // da CPU continua so para os passes que ainda nao usam o culling GPU (prepasso e emissao, fase 1): `prepass` = visivel.
    const bool gpuCull = gpuCullActive();
    const size_t drawTotal = draws_.size();
    queueItems_.resize(drawTotal);
    if (drawTotal > 0) {
      tool::parallelFor(drawTotal, 256, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
          const Draw& d = draws_[i];
          const render::SphereBounds worldBounds = render::transformSphere(d.world.m, d.mesh->bounds);
          const bool cameraVisible = render::sphereInFrustum(cameraFrustum, worldBounds);
          const bool emissive = !(d.mat.emissive.x <= 0.f && d.mat.emissive.y <= 0.f && d.mat.emissive.z <= 0.f);
          queueItems_[i] = {gpuCull || cameraVisible, false, cameraVisible && emissive, !gpuCull || cameraVisible, d.mat.castShadow && d.mesh->count > 0};
        }
      });
    }
    render::buildRenderQueues(queueItems_, renderQueues_);
    cpuVisibleOpaque_ = static_cast<u32>(renderQueues_.prepass.size());   // visiveis pelo frustum da CPU (referencia p/ comparar com a GPU)

    tdraws_.erase(std::remove_if(tdraws_.begin(), tdraws_.end(), [&](const Draw& d) {
      const render::SphereBounds worldBounds = render::transformSphere(d.world.m, d.mesh->bounds);
      return !render::sphereInFrustum(cameraFrustum, worldBounds);
    }), tdraws_.end());

    if (!ensureBuffer(instBuf_, instCap_, std::max(renderQueues_.opaque.size(), tdraws_.size()), sizeof(InstanceGPU))) {
      lastError_ = "storage buffer de instancias indisponivel";
      releaseImports();
      return;
    }

    shadowCasters_.clear();
    shadowInstances_.clear();
    shadowCasters_.reserve(draws_.size());
    for (const Draw& d : draws_) {
      if (d.mat.castShadow && d.mesh->count > 0) shadowCasters_.push_back(&d);
    }

    std::array<std::array<bool, kMaxPointShadowFaces>, kMaxPointShadowLights> pointFaceDirty{};
    std::array<std::array<PointFaceSignature, kMaxPointShadowFaces>, kMaxPointShadowLights> pointFaceSignatures{};
    for (u32 k = 0; k < shadowCount_ && shadowOn_ && !shadowSkip_; ++k) {
      const render::Frustum frustum = render::frustumFromViewProj(cascadeVp_[k].m, dev_->backend() != rhi::Backend::GLES);
      buildShadowQueue(csmQueues_[k], frustum, true, shadowCasters_);
    }

    for (u32 i = 0; i < pointCount_ && pointOn_; ++i) {
      for (u32 f = 0; f < pointFaces_[i]; ++f) {
        const render::Frustum frustum = render::frustumFromViewProj(pointVp_[i][f].m, dev_->backend() != rhi::Backend::GLES);
        ShadowQueue& queue = pointQueues_[i * kMaxPointShadowFaces + f];
        buildShadowQueue(queue, frustum, false, shadowCasters_, false);
        const PointFaceSignature signature = pointShadowFaceSignature(pointLights_[i], f, queue);
        pointFaceSignatures[i][f] = signature;
        PointFaceCache& cache = pointFaceCache_[i][f];
        pointFaceDirty[i][f] = !cache.valid || !(cache.signature == signature);
        if (pointFaceDirty[i][f]) {
          if (!cache.valid) {
            if (cache.targetResetPending) {
              ++stats_.shadowCache.pointFacesTargetReset;
              cache.targetResetPending = false;
            } else if (!cache.renderFailed) {
              ++stats_.shadowCache.pointFacesFirstUse;
            }
          } else if (cache.signature.light != signature.light) {
            ++stats_.shadowCache.pointFacesLightChanged;
          } else {
            ++stats_.shadowCache.pointFacesCastersChanged;
          }
          cache.valid = false;
          appendShadowInstances(queue);
        } else {
          ++stats_.shadowCache.pointFacesReused;
        }
      }
    }

    if (!shadowInstances_.empty()) {
      if (ensureBuffer(shadowInstBuf_, shadowInstCap_, shadowInstances_.size(), sizeof(ShadowInstanceGPU))) {
        dev_->updateBuffer(shadowInstBuf_, shadowInstances_.data(), shadowInstances_.size() * sizeof(ShadowInstanceGPU));
      }
    }

    // ---- 1) sombras CSM ----
    if (shadowOn_ && !shadowSkip_) {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::CsmShadow)]};
      for (u32 k = 0; k < shadowCount_; ++k) {
        auto cl = dev_->beginCommands();
        cl->begin(shColor_[k], shDepth_, 1.f, 1.f, 1.f, 1.f);
        cl->setPipeline(shPipe_);
        ShadowCB sc{};
        sc.lightViewProj = cascadeVp_[k];
        dev_->updateBuffer(shCb_, &sc, sizeof sc);
        cl->setUniform(0, shCb_);
        if (!shadowInstances_.empty()) cl->setStorage(0, shadowInstBuf_);
        const ShadowQueue& queue = csmQueues_[k];
        for (const InstanceBatch& b : queue.plan.batches) {
          BatchCB bc{};
          bc.first = static_cast<u32>(queue.instanceBase + b.first);
          uploadBatchCb(bc);
          cl->setUniform(1, batchCb_);
          const Draw& d = *queue.orderedDraws[b.first];
          cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
          recordDraw(*cl, render::RenderPassId::CsmShadow, d.mesh->count, b.count);
        }
        cl->end(); dev_->submit(*cl);
      }
      shadowValid_ = true;
    }

    // ---- 1b) sombras Point/Spot ----
    bool anyPointFaceDirty = false;
    for (u32 i = 0; i < pointCount_; ++i)
      for (u32 f = 0; f < pointFaces_[i]; ++f) anyPointFaceDirty = anyPointFaceDirty || pointFaceDirty[i][f];
    if (pointOn_ && anyPointFaceDirty) {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::PointShadow)]};
      for (u32 i = 0; i < pointCount_; ++i) {
        for (u32 f = 0; f < pointFaces_[i]; ++f) {
          if (!pointFaceDirty[i][f]) continue;
          const std::size_t errorsBefore = debug().count(Severity::Error);
          auto cl = dev_->beginCommands();
          if (!cl) {
            pointFaceCache_[i][f].renderFailed = true;
            ++stats_.shadowCache.pointFaceRenderFailures;
            continue;
          }
          cl->beginKeep(pointStrip_[i], pointDepth_);
          cl->clearDepth();
          cl->setViewport(0, f * pointRes_, pointRes_, pointRes_);
          cl->setScissor(0, f * pointRes_, pointRes_, pointRes_);
          cl->setPipeline(pointClearPipe_);
          cl->setVertexBuffer(fsVb_); cl->setIndexBuffer(fsIb_);
          recordDraw(*cl, render::RenderPassId::PointShadow, 3, 1);
          cl->setPipeline(ptPipe_);
          ShadowCB sc{};
          sc.lightViewProj = pointVp_[i][f];
          dev_->updateBuffer(shCb_, &sc, sizeof sc);
          cl->setUniform(0, shCb_);
          if (!pointQueues_[i * kMaxPointShadowFaces + f].plan.batches.empty()) cl->setStorage(0, shadowInstBuf_);
          const ShadowQueue& queue = pointQueues_[i * kMaxPointShadowFaces + f];
          for (const InstanceBatch& b : queue.plan.batches) {
            BatchCB bc{};
            bc.first = static_cast<u32>(queue.instanceBase + b.first);
            uploadBatchCb(bc);
            cl->setUniform(1, batchCb_);
            const Draw& d = *queue.orderedDraws[b.first];
            cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
            recordDraw(*cl, render::RenderPassId::PointShadow, d.mesh->count, b.count);
          }
          cl->end();
          dev_->submit(*cl);
          if (debug().count(Severity::Error) == errorsBefore) {
            PointFaceCache& cache = pointFaceCache_[i][f];
            cache.signature = pointFaceSignatures[i][f];
            cache.valid = true;
            cache.renderFailed = false;
            ++stats_.shadowCache.pointFacesRendered;
          } else {
            pointFaceCache_[i][f].renderFailed = true;
            ++stats_.shadowCache.pointFaceRenderFailures;
          }
        }
      }
    }

    // ---- 2) passe principal ----
    FrameCB fc; fc.viewProj = vp; fc.view = view; fc.camera[0] = cam_.eye.x; fc.camera[1] = cam_.eye.y; fc.camera[2] = cam_.eye.z;
    dev_->updateBuffer(frameCb_, &fc, sizeof fc);

    // ---- 1c) SSAO ----
    if (ssaoOn_) {
      planQueuedBatches(renderQueues_.prepass, prepassItemsScratch_, prepassPlan_, [&](u32 index) {
        return BatchItem{reinterpret_cast<u64>(draws_[index].mesh), 0u};
      }, 4096);
      prepassCpu_.resize(prepassPlan_.order.size());
      if (!prepassCpu_.empty()) {
        tool::parallelFor(prepassCpu_.size(), 256, [&](size_t b, size_t e) {
          for (size_t i = b; i < e; ++i) {
            prepassCpu_[i].world = draws_[renderQueues_.prepass[prepassPlan_.order[i]]].world;
          }
        });
        if (ensureBuffer(prepassInstBuf_, prepassInstCap_, prepassCpu_.size(), sizeof(ShadowInstanceGPU))) {
          dev_->updateBuffer(prepassInstBuf_, prepassCpu_.data(), prepassCpu_.size() * sizeof(ShadowInstanceGPU));
        }
      }
      render::PassTimer prepassTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Prepass)]};
      auto pl = dev_->beginCommands();
      pl->begin(gbuf_, depth_, 0.f, 0.f, 0.f, 0.f);
      pl->setPipeline(prePipe_);
      pl->setUniform(0, frameCb_);
      if (!prepassCpu_.empty()) pl->setStorage(0, prepassInstBuf_);
      for (const InstanceBatch& b : prepassPlan_.batches) {
        BatchCB bc{};
        bc.first = b.first;
        uploadBatchCb(bc);
        pl->setUniform(1, batchCb_);
        const Draw& d = draws_[renderQueues_.prepass[prepassPlan_.order[b.first]]];
        pl->setVertexBuffer(d.mesh->vertex); pl->setIndexBuffer(d.mesh->index);
        recordDraw(*pl, render::RenderPassId::Prepass, d.mesh->count, b.count);
      }
      pl->end(); dev_->submit(*pl);
      prepassTimer.stop();

      render::PassTimer ssaoTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Ssao)]};
      auto sc = dev_->beginCommands();
      sc->begin(aoRaw_, kNullHandle, 1.f, 1.f, 1.f, 1.f);
      sc->setPipeline(ssaoPipe_);
      sc->setUniform(0, ssaoCb_);
      sc->setTexture(0, gbuf_);
      sc->setVertexBuffer(fsVb_); sc->setIndexBuffer(fsIb_);
      recordDraw(*sc, render::RenderPassId::Ssao, 3, 1);
      sc->end(); dev_->submit(*sc);
      ssaoTimer.stop();

      render::PassTimer ssaoBlurTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::SsaoBlur)]};
      auto bl = dev_->beginCommands();
      bl->begin(ao_, kNullHandle, 1.f, 1.f, 1.f, 1.f);
      bl->setPipeline(blurPipe_);
      bl->setUniform(0, ssaoCb_);
      bl->setTexture(0, gbuf_);
      bl->setTexture(1, aoRaw_);
      bl->setVertexBuffer(fsVb_); bl->setIndexBuffer(fsIb_);
      recordDraw(*bl, render::RenderPassId::SsaoBlur, 3, 1);
      bl->end(); dev_->submit(*bl);
    } else {
      auto cl = dev_->beginCommands();
      cl->begin(ao_, kNullHandle, 1.f, 1.f, 1.f, 1.f);
      cl->end(); dev_->submit(*cl);
    }

    auto bindScene = [&](rhi::ICommandList* c, rhi::PipelineId p) {
      c->setPipeline(p);
      c->setUniform(0, frameCb_);
      c->setUniform(2, imp_.globals);
      c->setUniform(3, imp_.sun);
      c->setUniform(4, csmCb_);
      c->setUniform(5, iblCb_);
      c->setUniform(6, imp_.grid);
      c->setUniform(7, pointCb_);
      c->setStorage(0, imp_.points);
      c->setStorage(1, imp_.spots);
      c->setStorage(2, imp_.cells);
      c->setStorage(3, imp_.indices);
      for (u32 k = 0; k < 4; ++k) c->setTexture(1 + k, shColor_[k < shadowCount_ ? k : 0]);
      for (u32 i = 0; i < 4; ++i) c->setTexture(5 + i, pointStrip_[i < pointCount_ ? i : 0]);
      c->setTexture(9, ao_);
      c->setTexture(10, brdfTex_);
    };

    render::PassTimer opaqueTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Opaque)]};
    // A lista do passe opaco so abre (begin) depois do compute de culling: dispatch nao pode ficar dentro de um render pass.
    planQueuedBatches(renderQueues_.opaque, opaqueItemsScratch_, opaquePlan_, [&](u32 index) {
      const Draw& d = draws_[index];
      const u64 meshId = reinterpret_cast<u64>(d.mesh);
      return BatchItem{meshId, textureKey(d)};
    }, 4096);

    // Empacotamento paralelo das instancias opacas no SSBO
    const size_t opaqueCount = opaquePlan_.order.size();
    instCpu_.resize(opaqueCount);
    if (opaqueCount > 0) {
      tool::parallelFor(opaqueCount, 256, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
          const Draw& d = draws_[renderQueues_.opaque[opaquePlan_.order[i]]];
          instCpu_[i] = makeInstanceGPU(d, 1.f);
        }
      });
      if (!ensureBuffer(instBuf_, instCap_, instCpu_.size(), sizeof(InstanceGPU))) {
        lastError_ = "storage buffer de instancias indisponivel";
        releaseImports();
        return;
      }
      dev_->updateBuffer(instBuf_, instCpu_.data(), instCpu_.size() * sizeof(InstanceGPU));
    }

    // Culling de frustum na GPU (fase 1: so este passe; sombras, prepasso, emissao e transparentes seguem no caminho CPU).
    // Fica ativo no quadro se o compute foi criado, ha lotes e o dispatch cabe (gy <= 65535 grupos); senao o quadro usa o
    // caminho CPU (a fila opaca pode ter instancias fora da tela, que a GPU recorta no rasterizador: so perde desempenho).
    gpuCullFrame_ = gpuCullActive() && opaqueCount > 0 && !opaquePlan_.batches.empty() && opaquePlan_.batches.size() <= 65535u;
    if (gpuCullFrame_) gpuCullFrame_ = runGpuCull(opaqueCount, cameraFrustum);

    cl_ = dev_->beginCommands();
    cl_->begin(color_, depth_, clear_.x, clear_.y, clear_.z, clearAlpha_);
    bindScene(cl_.get(), pipe_);
    // Caminho GPU: o VS le do buffer compactado (mesma faixa de cada lote, entao BatchCB.first continua valendo).
    if (opaqueCount > 0) cl_->setStorage(4, gpuCullFrame_ ? compactBuf_ : instBuf_);

    u32 batchIndex = 0;
    rhi::PipelineId curPipe = pipe_;   // pipeline ligado agora (pipe_ ou a variante dupla face)
    for (const InstanceBatch& b : opaquePlan_.batches) {
      BatchCB bc{};
      bc.first = b.first;
      uploadBatchCb(bc);
      cl_->setUniform(1, batchCb_);
      const Draw& d = draws_[renderQueues_.opaque[opaquePlan_.order[b.first]]];
      // Dupla face: troca para o pipeline sem cull (rebind da cena) quando o lote pede; volta ao normal no lote seguinte.
      {
        const rhi::PipelineId want = (d.mat.doubleSided && !pipeDs_.isNull()) ? pipeDs_ : pipe_;
        if (want != curPipe) {
          curPipe = want;
          bindScene(cl_.get(), curPipe);
          cl_->setStorage(4, gpuCullFrame_ ? compactBuf_ : instBuf_);
          cl_->setUniform(1, batchCb_);   // o rebind zera os slots: religa o BatchCB deste lote (ja gravado acima)
        }
      }
      cl_->setTexture(0, d.tex.isNull() ? white_ : d.tex);
      if (normalSlot_) cl_->setTexture(kNormalMapSlot, d.ntex.isNull() ? white_ : d.ntex);
      cl_->setVertexBuffer(d.mesh->vertex); cl_->setIndexBuffer(d.mesh->index);
      if (gpuCullFrame_) {
        // instanceCount vem da GPU; nas estatisticas entra o total do lote (limite superior, so a GPU sabe o visivel).
        cl_->drawIndexedIndirect(cullArgsBuf_, batchIndex * static_cast<u32>(sizeof(rhi::DrawIndexedIndirectArgs)));
        stats_.passes[static_cast<std::size_t>(render::RenderPassId::Opaque)].recordDraw(d.mesh->count, b.count);
      } else {
        recordDraw(*cl_, render::RenderPassId::Opaque, d.mesh->count, b.count);
      }
      ++batchIndex;
    }
    cl_->end(); dev_->submit(*cl_); cl_.reset();
    opaqueTimer.stop();

    // Passe de emissao: usa buffer dedicado (emiInstBuf_) para nao sobrescrever instBuf_ do passe opaco no mesmo frame
    if (bloomOn_ && !glow_.isNull()) {
      render::PassTimer emissionTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Emission)]};
      auto gl = dev_->beginCommands();
      gl->begin(glow_, kNullHandle, 0.f, 0.f, 0.f, 0.f);
      if (!emiPipe_.isNull()) {
        gl->setTargets(glow_, depth_);
        gl->setPipeline(emiPipe_);
        gl->setUniform(0, frameCb_);
        planQueuedBatches(renderQueues_.emissive, emissionItemsScratch_, emissionPlan_, [&](u32 index) {
          const Draw& d = draws_[index];
          return BatchItem{reinterpret_cast<u64>(d.mesh), 0};
        }, 4096);
        emiCpu_.resize(emissionPlan_.order.size());
        for (size_t i = 0; i < emissionPlan_.order.size(); ++i) {
          const Draw& d = draws_[renderQueues_.emissive[emissionPlan_.order[i]]];
          InstanceGPU inst{};
          std::memcpy(inst.world, &d.world, sizeof d.world);
          inst.emissive[0] = d.mat.emissive.x;
          inst.emissive[1] = d.mat.emissive.y;
          inst.emissive[2] = d.mat.emissive.z;
          emiCpu_[i] = inst;
        }
        if (!emiCpu_.empty() && ensureBuffer(emiInstBuf_, emiInstCap_, emiCpu_.size(), sizeof(InstanceGPU))) {
          dev_->updateBuffer(emiInstBuf_, emiCpu_.data(), emiCpu_.size() * sizeof(InstanceGPU));
          gl->setStorage(0, emiInstBuf_);
          for (const InstanceBatch& b : emissionPlan_.batches) {
            BatchCB bc{};
            bc.first = b.first;
            uploadBatchCb(bc);
            gl->setUniform(1, batchCb_);
            const Draw& d = draws_[renderQueues_.emissive[emissionPlan_.order[b.first]]];
            gl->setVertexBuffer(d.mesh->vertex);
            gl->setIndexBuffer(d.mesh->index);
            recordDraw(*gl, render::RenderPassId::Emission, d.mesh->count, b.count);
          }
        }
      }
      gl->end(); dev_->submit(*gl);
    }

    // Passe transparente: usa buffer dedicado (transInstBuf_) para preservar integridade dos passes anteriores
    if (!tdraws_.empty() && !transPipe_.isNull()) {
      render::PassTimer transparentTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Transparent)]};
      const Vec3 eye = cam_.eye;
      auto dist2 = [&](const Draw& d) { const Vec3 p = d.world.transformPoint({0, 0, 0}); const Vec3 q = p - eye; return dot(q, q); };
      std::sort(tdraws_.begin(), tdraws_.end(), [&](const Draw& a, const Draw& b) { return dist2(a) > dist2(b); });
      transparentItemsScratch_.clear();
      transparentItemsScratch_.reserve(tdraws_.size());
      for (const Draw& d : tdraws_) {
        const u64 meshId = reinterpret_cast<u64>(d.mesh);
        transparentItemsScratch_.push_back({meshId, textureKey(d)});
      }
      planAdjacentInstanceBatches(transparentItemsScratch_, transparentPlan_, 4096);
      transCpu_.resize(transparentPlan_.order.size());
      for (size_t i = 0; i < transparentPlan_.order.size(); ++i) {
        const Draw& d = tdraws_[transparentPlan_.order[i]];
        transCpu_[i] = makeInstanceGPU(d, d.mat.opacity);
      }
      if (!transCpu_.empty()) {
        if (!ensureBuffer(transInstBuf_, transInstCap_, transCpu_.size(), sizeof(InstanceGPU))) {
          lastError_ = "storage buffer de instancias transparentes indisponivel";
          transparentTimer.stop();
          releaseImports();
          return;
        }
        dev_->updateBuffer(transInstBuf_, transCpu_.data(), transCpu_.size() * sizeof(InstanceGPU));
      }
      auto tl = dev_->beginCommands();
      tl->beginKeep(color_, depth_);
      bindScene(tl.get(), transPipe_);
      if (!transCpu_.empty()) tl->setStorage(4, transInstBuf_);
      rhi::PipelineId curTrans = transPipe_;
      for (const InstanceBatch& b : transparentPlan_.batches) {
        BatchCB bc{};
        bc.first = b.first;
        uploadBatchCb(bc);
        tl->setUniform(1, batchCb_);
        const Draw& d = tdraws_[transparentPlan_.order[b.first]];
        {   // dupla face: pipeline transparente sem cull (rebind da cena quando o lote muda)
          const rhi::PipelineId want = (d.mat.doubleSided && !transPipeDs_.isNull()) ? transPipeDs_ : transPipe_;
          if (want != curTrans) {
            curTrans = want;
            bindScene(tl.get(), curTrans);
            if (!transCpu_.empty()) tl->setStorage(4, transInstBuf_);
            tl->setUniform(1, batchCb_);
          }
        }
        tl->setTexture(0, d.tex.isNull() ? white_ : d.tex);
        if (normalSlot_) tl->setTexture(kNormalMapSlot, d.ntex.isNull() ? white_ : d.ntex);
        tl->setVertexBuffer(d.mesh->vertex);
        tl->setIndexBuffer(d.mesh->index);
        recordDraw(*tl, render::RenderPassId::Transparent, d.mesh->count, b.count);
      }
      tl->end(); dev_->submit(*tl);
    }

    out_ = color_;
    if (bloomOn_ && !bloomA_.isNull() && !bloomThrPipe_.isNull()) { runBloom(); out_ = outFull_; }
    // FSR 1: amplia a imagem renderizada (w_ x h_) para o tamanho de saida pedido; a leitura (readback) passa a vir dela.
    fsrActive_ = false;
    if (fsrWant_ && (fsrOutW_ != w_ || fsrOutH_ != h_)) {
      rhi::TextureId up;
      if (fsrOk_) up = fsr_.run(out_, w_, h_, fsrOutW_, fsrOutH_, fsrSharp_);
      if (!up.isNull()) { out_ = up; fsrActive_ = true; }
      else {   // falhou: desliga o FSR 1 e avisa UMA vez (o chamador amplia pelo caminho normal)
        fsrOk_ = false;
        if (fsrWhy_.empty()) fsrWhy_ = "falha ao rodar o FSR 1";
        if (!fsrWarned_) { fsrWarned_ = true; debug().report("TOOL-W144", Severity::Warning, "FSR 1 desligado: " + fsrWhy_); }
      }
    }
    releaseImports();
    drawCount_ = static_cast<u32>(opaquePlan_.batches.size());
  }

  bool readback(std::vector<u8>& rgba, u32& w, u32& h) { return dev_ && dev_->readTexture(out_, rgba, w, h); }
  int readbackAsync(int slot, std::vector<u8>& rgba, u32& w, u32& h) { return dev_ ? dev_->readTextureAsync(out_, slot, rgba, w, h) : -1; }
  void setBloom(bool on, f32 threshold, f32 strength) { bloomOn_ = on; bloomThr_ = threshold; bloomStr_ = strength; }
  // ---- FSR 1 (upscale na GPU) ----
  // Pede (on) que end() amplie a cena renderizada em w x h para outW x outH com EASU + RCAS (sharpness 0..1).
  // So vale se fsr1Available(); se o chamador pedir sem suporte, avisa UMA vez e a imagem sai no tamanho de render.
  void setFsr1(bool on, f32 sharpness, u32 outW, u32 outH) {
    fsrWant_ = on; fsrSharp_ = sharpness; fsrOutW_ = outW; fsrOutH_ = outH;
    if (on && !fsrOk_ && !fsrWarned_) { fsrWarned_ = true; debug().report("TOOL-W144", Severity::Warning, "FSR 1 indisponivel neste backend: " + (fsrWhy_.empty() ? std::string("sem suporte") : fsrWhy_)); }
  }
  // O backend e os shaders suportam o FSR 1.
  bool fsr1Available() const { return fsrOk_; }
  // O ultimo end() entregou a imagem ampliada pelo FSR 1 (o readback tem outW x outH).
  bool fsr1Active() const { return fsrActive_; }
  const std::string& fsr1Why() const { return fsrWhy_; }
  rhi::TextureId colorTarget() const { return color_; }
  rhi::TextureId depthTarget() const { return depth_; }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  u32 draws() const { return drawCount_; }
  // ---- Culling de frustum na GPU (compute + draw indireto), fase 1: so o passe opaco principal ----
  // Liga/desliga. Mesmo ligado, so vale se o backend tem compute+indirect e o shader foi criado (gpuCullActive()).
  void setGpuCulling(bool on) { gpuCull_ = on; }
  bool gpuCullActive() const { return gpuCull_ && dev_ && !cullPipe_.isNull() && !cullCb_.isNull(); }
  // O ultimo quadro usou o caminho GPU no passe opaco.
  bool gpuCullUsedLastFrame() const { return gpuCullFrame_; }
  // Opacos visiveis pelo frustum da CPU no ultimo quadro (referencia para comparar com a GPU).
  u32 cpuVisibleOpaque() const { return cpuVisibleOpaque_; }
  // Le da GPU (espera o fim do quadro: so para teste/bench) a soma de instanceCount dos draws indiretos do ultimo quadro.
  bool readGpuVisibleCount(u32& out) {
    out = 0;
    if (!gpuCullFrame_ || !dev_) return false;
    std::vector<u8> raw;
    const size_t nb = opaquePlan_.batches.size();
    if (!dev_->readBuffer(cullArgsBuf_, raw) || raw.size() < nb * sizeof(rhi::DrawIndexedIndirectArgs)) return false;
    for (size_t i = 0; i < nb; ++i) {
      rhi::DrawIndexedIndirectArgs a{};
      std::memcpy(&a, raw.data() + i * sizeof a, sizeof a);
      out += a.instanceCount;
    }
    return true;
  }
  size_t cachedMeshes() const { return meshes_.size(); }
  const render::RenderViewStats& renderStats() const { return stats_; }
  const LightPrism::ClusterSystem* clusters() const { return clusters_.get(); }
  // Total de uploads do BatchCB desde a criacao (um por lote desenhado). Diferenca entre dois quadros = uploads por quadro.
  u64 batchCbUploads() const { return batchCbUploads_; }

  void forget(const Mesh& m) {
    auto it = meshes_.find(&m);
    if (it == meshes_.end()) return;
    dev_->destroy(it->second.vertex); dev_->destroy(it->second.index); meshes_.erase(it);
  }

private:
  // Envia o BatchCB de um lote e conta o upload (medido pelo bench).
  void uploadBatchCb(const BatchCB& bc) { dev_->updateBuffer(batchCb_, &bc, sizeof bc); ++batchCbUploads_; }
  void recordDraw(rhi::ICommandList& commands, render::RenderPassId pass, u32 indexCount, u32 instances) {
    commands.drawIndexed(indexCount, instances);
    stats_.passes[static_cast<std::size_t>(pass)].recordDraw(indexCount, instances);
  }

  struct GpuMesh { rhi::BufferId vertex, index; u32 count = 0; const void* posKey = nullptr; size_t nVert = 0, nIdx = 0, vCap = 0, iCap = 0; u64 lastUse = 0; u32 rev = 0; bool dynamic = false; render::SphereBounds bounds; };
  struct Draw { const GpuMesh* mesh; Mat4 world; Material mat; rhi::TextureId tex; f32 tw, th; rhi::TextureId ntex; };
  // Chave de lote de textura: albedo (32 bits) + normal map (31 bits) + dupla face (1 bit). Instancias com chaves diferentes
  // nunca entram no mesmo lote (a textura/pipeline mudam por lote).
  static u64 textureKey(const Draw& d) {
    auto pack = [](const rhi::TextureId& t) -> u64 { return t.isNull() ? 0u : (static_cast<u64>(t.generation & 0x7FFFu) << 16) | (t.index & 0xFFFFu); };
    return (pack(d.tex) << 32) | (pack(d.ntex) << 1) | (d.mat.doubleSided ? 1u : 0u);
  }
  struct ShadowQueue { BatchPlan plan; std::vector<const Draw*> orderedDraws; size_t instanceBase = 0; };
  struct PointFaceSignature { u64 light = 0, casters = 0; bool operator==(const PointFaceSignature&) const = default; };
  struct PointFaceCache { PointFaceSignature signature{}; bool valid = false, targetResetPending = false, renderFailed = false; };
  struct Target { rhi::TextureId color, depth, gbuf, aoRaw, ao, bloomA, bloomB, out, glow; u64 lastUse = 0; };
  struct Imports { rhi::BufferId points, spots, globals, sun, grid, cells, indices; };

  static Mat4 toTool(const LightPrism::Mat4& m) { Mat4 r; for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r.m[c][k] = m.m[k][c]; return r; }

  static render::SphereBounds meshSphere(const Mesh& m) {
    if (m.pos.empty()) return {};
    Vec3 lo = m.pos.front(), hi = m.pos.front();
    auto finite = [](Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if (!finite(lo)) return {};
    for (Vec3 p : m.pos) {
      if (!finite(p)) return {};
      lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
      hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const Vec3 center{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    f32 radius2 = 0.f;
    for (Vec3 p : m.pos) {
      const Vec3 d = p - center;
      radius2 = std::max(radius2, dot(d, d));
    }
    const f32 radius = std::sqrt(radius2);
    if (!finite(center) || !std::isfinite(radius)) return {};
    return {{center.x, center.y, center.z}, radius, true};
  }

  void buildShadowQueue(ShadowQueue& queue, const render::Frustum& frustum, bool csm,
                       const std::vector<const Draw*>& casters, bool appendInstances = true) {
    (void)csm;
    shadowItemsScratch_.clear();
    shadowDrawsScratch_.clear();
    queue.orderedDraws.clear();
    queue.plan.order.clear();
    queue.plan.batches.clear();
    queue.instanceBase = 0;
    for (const Draw* d : casters) {
      const render::SphereBounds worldBounds = render::transformSphere(d->world.m, d->mesh->bounds);
      if (!render::sphereInFrustumSides(frustum, worldBounds)) continue;
      shadowDrawsScratch_.push_back(d);
    }
    planQueuedBatches(shadowDrawsScratch_, shadowItemsScratch_, queue.plan, [](const Draw* d) {
      return BatchItem{reinterpret_cast<u64>(d->mesh), 0u};
    }, 4096);
    queue.orderedDraws.reserve(queue.plan.order.size());
    for (u32 index : queue.plan.order) {
      queue.orderedDraws.push_back(shadowDrawsScratch_[index]);
    }
    if (appendInstances) appendShadowInstances(queue);
  }

  void appendShadowInstances(ShadowQueue& queue) {
    queue.instanceBase = shadowInstances_.size();
    for (const Draw* d : queue.orderedDraws) shadowInstances_.push_back({d->world});
  }

  PointFaceSignature pointShadowFaceSignature(const ShadowLight& light, u32 face, const ShadowQueue& queue) const {
    std::uint64_t lightHash = 1469598103934665603ull;
    std::uint64_t casterHash = 1469598103934665603ull;
    auto mixLightU = [&](std::uint64_t value) { lightHash ^= value; lightHash *= 1099511628211ull; };
    auto mixLightF = [&](float value) { std::uint32_t bits; std::memcpy(&bits, &value, sizeof bits); mixLightU(bits); };
    auto mixCasterU = [&](std::uint64_t value) { casterHash ^= value; casterHash *= 1099511628211ull; };
    auto mixCasterF = [&](float value) { std::uint32_t bits; std::memcpy(&bits, &value, sizeof bits); mixCasterU(bits); };
    mixLightU(pointRes_); mixLightU(light.faces); mixLightU(static_cast<u64>(light.type)); mixLightU(face);
    mixLightF(light.position.x); mixLightF(light.position.y); mixLightF(light.position.z);
    mixLightF(light.range); mixLightF(light.nearZ);
    const Mat4& vp = light.vp[face];
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixLightF(vp.m[c][r]);
    for (const Draw* d : queue.orderedDraws) {
      mixCasterU(reinterpret_cast<std::uintptr_t>(d->mesh->posKey));
      mixCasterU(d->mesh->nVert); mixCasterU(d->mesh->nIdx); mixCasterU(d->mesh->rev); mixCasterU(d->mesh->count);
      if (d->mesh->dynamic) mixCasterU(frame_);
      for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixCasterF(d->world.m[c][r]);
    }
    return {lightHash, casterHash};
  }

  static void packVertices(const Mesh& m, std::vector<f32>& v) {
    v.clear(); v.reserve(m.pos.size() * 8);
    const bool hasUv = m.uv.size() == m.pos.size() * 2;
    for (size_t i = 0; i < m.pos.size(); ++i) { v.insert(v.end(), {m.pos[i].x, m.pos[i].y, m.pos[i].z, m.normal[i].x, m.normal[i].y, m.normal[i].z, hasUv ? m.uv[2 * i] : 0.f, hasUv ? m.uv[2 * i + 1] : 0.f}); }
  }

  void freeMesh(GpuMesh& g) {
    if (!g.vertex.isNull()) dev_->destroy(g.vertex);
    if (!g.index.isNull()) dev_->destroy(g.index);
    const u64 lu = g.lastUse; g = GpuMesh{}; g.lastUse = lu;
  }

  void runBloom() {
    BloomCB base{};
    base.srcSize[0] = static_cast<f32>(w_); base.srcSize[1] = static_cast<f32>(h_);
    base.haloSize[0] = static_cast<f32>(std::max(1u, w_ / 2)); base.haloSize[1] = static_cast<f32>(std::max(1u, h_ / 2));
    base.threshold = 0.f; base.strength = bloomStr_;
    BloomCB hz = base; hz.dir[0] = 1.f; hz.dir[1] = 0.f;
    BloomCB vt = base; vt.dir[0] = 0.f; vt.dir[1] = 1.f;
    dev_->updateBuffer(bloomCbThr_, &base, sizeof base);
    dev_->updateBuffer(bloomCbH_, &hz, sizeof hz);
    dev_->updateBuffer(bloomCbV_, &vt, sizeof vt);
    dev_->updateBuffer(bloomCbComp_, &base, sizeof base);
    auto fullscreen = [&](rhi::ICommandList& c, render::RenderPassId pass) {
      c.setVertexBuffer(fsVb_); c.setIndexBuffer(fsIb_); recordDraw(c, pass, 3, 1);
    };
    {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomExtract)]};
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomThrPipe_); c->setUniform(0, bloomCbThr_); c->setTexture(0, glow_);
      fullscreen(*c, render::RenderPassId::BloomExtract); c->end(); dev_->submit(*c);
    }
    {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomBlurH)]};
      auto c = dev_->beginCommands();
      c->begin(bloomB_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbH_); c->setTexture(0, bloomA_);
      fullscreen(*c, render::RenderPassId::BloomBlurH); c->end(); dev_->submit(*c);
    }
    {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomBlurV)]};
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbV_); c->setTexture(0, bloomB_);
      fullscreen(*c, render::RenderPassId::BloomBlurV); c->end(); dev_->submit(*c);
    }
    {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomComposite)]};
      auto c = dev_->beginCommands();
      c->begin(outFull_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomCompPipe_); c->setUniform(0, bloomCbComp_); c->setTexture(0, color_); c->setTexture(1, bloomA_);
      fullscreen(*c, render::RenderPassId::BloomComposite); c->end(); dev_->submit(*c);
    }
  }

  void releaseImports() {
    for (auto h : imports_) if (dev_) dev_->destroy(h);
    imports_.clear(); imp_ = Imports{};
  }

  GpuMesh& upload(const Mesh& m) {
    GpuMesh& g = meshes_[&m];
    g.lastUse = frame_;
    if (!g.vertex.isNull() && g.posKey == m.pos.data() && g.nVert == m.pos.size() && g.nIdx == m.idx.size() && g.rev == m.revision) return g;
    freeMesh(g);
    packVertices(m, packBuf_);
    g.vertex = dev_->createBuffer({packBuf_.size() * sizeof(f32), rhi::BufferUsage::Vertex, false}, packBuf_.data());
    g.index = dev_->createBuffer({m.idx.size() * sizeof(u32), rhi::BufferUsage::Index, false}, m.idx.data());
    g.count = static_cast<u32>(m.idx.size());
    g.posKey = m.pos.data(); g.nVert = m.pos.size(); g.nIdx = m.idx.size(); g.rev = m.revision;
    g.vCap = packBuf_.size() * sizeof(f32); g.iCap = m.idx.size() * sizeof(u32);
    g.bounds = meshSphere(m);
    return g;
  }

  GpuMesh& dynamicSlot(u32 slot, const Mesh& m) {
    GpuMesh& g = dyn_[slot];
    g.lastUse = frame_;
    packVertices(m, packBuf_);
    const size_t vb = packBuf_.size() * sizeof(f32), ib = m.idx.size() * sizeof(u32);
    if (g.vertex.isNull() || vb > g.vCap) {
      if (!g.vertex.isNull()) dev_->destroy(g.vertex);
      g.vCap = std::max<size_t>(vb, g.vCap + g.vCap / 2);
      g.vertex = dev_->createBuffer({g.vCap, rhi::BufferUsage::Vertex, true}, nullptr);
    }
    if (g.index.isNull() || ib > g.iCap) {
      if (!g.index.isNull()) dev_->destroy(g.index);
      g.iCap = std::max<size_t>(ib, g.iCap + g.iCap / 2);
      g.index = dev_->createBuffer({g.iCap, rhi::BufferUsage::Index, true}, nullptr);
    }
    if (vb) dev_->updateBuffer(g.vertex, packBuf_.data(), vb);
    if (ib) dev_->updateBuffer(g.index, m.idx.data(), ib);
    g.count = static_cast<u32>(m.idx.size());
    g.posKey = m.pos.data(); g.nVert = m.pos.size(); g.nIdx = m.idx.size(); g.rev = m.revision; g.dynamic = true;
    g.bounds = meshSphere(m);
    return g;
  }

  void ensureShadowTargets(u32 res) {
    if (shW_ == res && !shColor_[0].isNull()) return;
    for (auto& t : shColor_) if (!t.isNull()) dev_->destroy(t);
    if (!shDepth_.isNull()) dev_->destroy(shDepth_);
    for (u32 k = 0; k < LightPrism::kMaxCsmCascades; ++k) shColor_[k] = dev_->createTexture({res, res, 1, rhi::Format::R32F, true}, nullptr);
    shDepth_ = dev_->createTexture({res, res, 1, rhi::Format::D32F, true}, nullptr);
    shW_ = res; shRes_ = static_cast<int>(res); shadowValid_ = false;
  }

  void ensurePointTargets(u32 res) {
    bool ready = pointW_ == res && !pointDepth_.isNull();
    for (const auto& t : pointStrip_) ready = ready && !t.isNull();
    if (ready) return;
    for (auto& light : pointFaceCache_) {
      for (PointFaceCache& face : light) {
        const bool hadCachedFace = face.valid;
        face = {};
        face.targetResetPending = hadCachedFace;
      }
    }
    for (auto& t : pointStrip_) if (!t.isNull()) dev_->destroy(t);
    if (!pointDepth_.isNull()) dev_->destroy(pointDepth_);
    for (auto& t : pointStrip_) t = dev_->createTexture({res, 6 * res, 1, rhi::Format::R32F, true}, nullptr);
    pointDepth_ = dev_->createTexture({res, 6 * res, 1, rhi::Format::D32F, true}, nullptr);
    pointW_ = res;
  }

  void release() {
    if (!dev_) return;
    fsr_.release(); fsrOk_ = false; fsrActive_ = false; fsrWant_ = false;
    releaseImports();
    for (auto& [k, g] : meshes_) freeMesh(g);
    meshes_.clear();
    for (auto& [k, g] : dyn_) freeMesh(g);
    dyn_.clear();
    for (auto& [k, t] : targets_) destroyTarget(t);
    targets_.clear(); color_ = depth_ = bloomA_ = bloomB_ = outFull_ = out_ = glow_ = kNullHandle;
    for (auto& [k, t] : texs_) dev_->destroy(t);
    texs_.clear();
    if (!white_.isNull()) { dev_->destroy(white_); white_ = kNullHandle; }
    for (auto* h : {&cullPipe_, &cullCs_, &cullCb_, &cullBatchBuf_, &compactBuf_, &cullArgsBuf_}) if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    cullBatchCap_ = compactCap_ = cullArgsCap_ = 0; gpuCullFrame_ = false;
    for (auto* h : {&frameCb_, &objCb_, &batchCb_, &instBuf_, &emiInstBuf_, &transInstBuf_, &shadowInstBuf_, &prepassInstBuf_, &shCb_, &csmCb_, &iblCb_, &pointCb_, &brdfTex_, &shDepth_, &pointDepth_, &pipe_, &shPipe_, &ptPipe_, &pointClearPipe_, &vs_, &ps_, &legacyVs_, &legacyPs_, &shVs_, &shPs_, &ptVs_, &ptPs_, &pointClearPs_,
                    &prePipe_, &ssaoPipe_, &blurPipe_, &fsVb_, &fsIb_, &ssaoCb_, &preVs_, &prePs_, &fsVs_, &ssaoPs_, &blurPs_,
                    &emiPipe_, &emiPs_, &transPipe_, &pipeDs_, &transPipeDs_, &bloomThrPipe_, &bloomBlurPipe_, &bloomCompPipe_, &bloomCbThr_, &bloomCbH_, &bloomCbV_, &bloomCbComp_, &bloomThrPs_, &bloomBlurPs_, &bloomCompPs_}) if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    for (auto& t : shColor_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    for (auto& t : pointStrip_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    pointW_ = 0; pointOn_ = false; pointCount_ = 0; pointFaceCache_ = {};
    instCap_ = 0; emiInstCap_ = 0; transInstCap_ = 0; shadowInstCap_ = 0; prepassInstCap_ = 0;
    instCpu_.clear(); emiCpu_.clear(); transCpu_.clear();
    clusters_.reset(); lp_.reset();
    shW_ = 0; shRes_ = 0; shadowCount_ = 0; shadowValid_ = false; draws_.clear(); dev_ = nullptr;
  }

  rhi::IDevice* dev_ = nullptr;
  // FSR 1: passe, pedido do chamador e estado (disponivel / ativo no ultimo quadro / aviso ja dado).
  render::Fsr1Pass fsr_;
  bool fsrOk_ = false, fsrWant_ = false, fsrActive_ = false, fsrWarned_ = false;
  f32 fsrSharp_ = 0.2f; u32 fsrOutW_ = 0, fsrOutH_ = 0; std::string fsrWhy_;
  std::unique_ptr<LightPrism::IGraphicsDevice> lp_;
  std::unique_ptr<LightPrism::ClusterSystem> clusters_;
  rhi::ShaderId vs_, ps_, legacyVs_, legacyPs_, shVs_, shPs_, ptVs_, ptPs_, pointClearPs_, preVs_, prePs_, fsVs_, ssaoPs_, blurPs_;
  rhi::PipelineId prePipe_, ssaoPipe_, blurPipe_;
  rhi::BufferId fsVb_, fsIb_, ssaoCb_;
  rhi::PipelineId pipe_, shPipe_, ptPipe_, pointClearPipe_;
  u64 batchCbUploads_ = 0;
  rhi::BufferId frameCb_, objCb_, batchCb_, instBuf_, emiInstBuf_, transInstBuf_, shadowInstBuf_, prepassInstBuf_, shCb_, csmCb_, iblCb_, pointCb_;
  u32 instCap_ = 0, emiInstCap_ = 0, transInstCap_ = 0, shadowInstCap_ = 0, prepassInstCap_ = 0;
  // Culling GPU: pipeline de compute, cbuffer de planos, descritores de lote, buffer compactado (UAV+SRV) e args indiretos.
  rhi::ShaderId cullCs_; rhi::PipelineId cullPipe_;
  rhi::BufferId cullCb_, cullBatchBuf_, compactBuf_, cullArgsBuf_;
  u32 cullBatchCap_ = 0, compactCap_ = 0, cullArgsCap_ = 0;
  std::vector<CullBatchGPU> cullBatchCpu_;
  std::vector<rhi::DrawIndexedIndirectArgs> cullArgsCpu_;
  bool gpuCull_ = true, gpuCullFrame_ = false;
  u32 cpuVisibleOpaque_ = 0;
  std::vector<InstanceGPU> instCpu_, emiCpu_, transCpu_;
  std::vector<ShadowInstanceGPU> prepassCpu_;
  std::array<ShadowQueue, LightPrism::kMaxCsmCascades> csmQueues_;
  std::array<ShadowQueue, kMaxPointShadowLights * kMaxPointShadowFaces> pointQueues_;
  std::vector<const Draw*> shadowCasters_, shadowDrawsScratch_;
  std::vector<render::RenderQueueItem> queueItems_;
  render::RenderQueues renderQueues_;
  std::vector<BatchItem> opaqueItemsScratch_, prepassItemsScratch_, emissionItemsScratch_, transparentItemsScratch_, shadowItemsScratch_;
  BatchPlan opaquePlan_, prepassPlan_, emissionPlan_, transparentPlan_;
  std::vector<ShadowInstanceGPU> shadowInstances_;
  rhi::TextureId brdfTex_;
  rhi::TextureId pointStrip_[kMaxPointShadowLights], pointDepth_;
  u32 pointW_ = 0, pointCount_ = 0, pointRes_ = 512;
  bool pointOn_ = false, ssaoOn_ = false;
  std::array<std::array<PointFaceCache, kMaxPointShadowFaces>, kMaxPointShadowLights> pointFaceCache_{};
  u32 pointFaces_[kMaxPointShadowLights] = {};
  ShadowLight pointLights_[kMaxPointShadowLights];
  Mat4 pointVp_[kMaxPointShadowLights][kMaxPointShadowFaces];
  std::vector<rhi::BufferId> imports_;
  Imports imp_{};
  std::unordered_map<u64, rhi::TextureId> texs_;
  rhi::TextureId white_;
  rhi::TextureId makeWhite() { const u8 px[4] = {255, 255, 255, 255}; return dev_->createTexture({1, 1, 1, rhi::Format::RGBA8, false}, px); }
  Draw makeDraw(const GpuMesh* g, const Mat4& world, const Material& mat) {
    f32 tw = 0, th = 0;
    if (white_.isNull() && dev_) white_ = makeWhite();
    const rhi::TextureId tex = textureFor(mat.albedoMap, tw, th);
    // Normal map do material: so cria/usa textura se o pipeline tem o slot; sem mapa fica a textura branca (nao e lida: forca 0).
    f32 nw = 0, nh = 0;
    rhi::TextureId ntex = normalSlot_ && mat.normalMap ? textureFor(mat.normalMap, nw, nh) : white_;
    if (nw <= 0.f) ntex = white_;   // textura invalida: sem normal map
    Draw d{g, world, mat, tex, tw, th, ntex};
    if (nw <= 0.f) d.mat.normalMap = nullptr;   // marca "sem mapa" (makeInstanceGPU zera a forca)
    return d;
  }
  rhi::TextureId shColor_[LightPrism::kMaxCsmCascades], shDepth_; u32 shW_ = 0; int shRes_ = 0;
  u32 shadowCount_ = 0;
  bool shadowOn_ = false, open_ = false, shadowSkip_ = false, shadowValid_ = false;
  Mat4 cascadeVp_[LightPrism::kMaxCsmCascades];
  std::string lastError_;
  Camera cam_; Vec3 clear_; f32 clearAlpha_ = 1.f; f32 cam_zNear_ = 0.1f, cam_zFar_ = 100.f;
  std::vector<Draw> draws_;
  std::vector<Draw> tdraws_;

  static void fillFog(ObjectCB& oc, const Material& m) {
    oc.fogColor[0] = m.fogColor.x; oc.fogColor[1] = m.fogColor.y; oc.fogColor[2] = m.fogColor.z; oc.fogColor[3] = 0.f;
    oc.fogParams[0] = m.fogStart; oc.fogParams[1] = m.fogEnd; oc.fogParams[2] = m.fogAmount; oc.fogParams[3] = 0.f;
  }

  static InstanceGPU makeInstanceGPU(const Draw& d, f32 alpha) {
    ObjectCB oc{};
    oc.world = d.world;
    oc.color[0] = d.mat.albedo.x; oc.color[1] = d.mat.albedo.y; oc.color[2] = d.mat.albedo.z; oc.color[3] = alpha;
    oc.metallic = d.mat.metallic; oc.roughness = d.mat.roughness; oc.receiveShadow = d.mat.receiveShadow ? 1.f : 0.f;
    oc.texInfo[0] = d.tw; oc.texInfo[1] = d.th; oc.texInfo[2] = d.tw > 0.f ? 1.f : 0.f;
    // Material avancado nas sobras do layout de 160 B: tiling xy em pad/texInfo.w, offset xy em emissive.w/fogColor.w,
    // forca do normal map + alpha cutoff empacotados em fogParams.w (fillFog zera, entao grava depois dele).
    oc.pad = d.mat.uvTiling[0]; oc.texInfo[3] = d.mat.uvTiling[1];
    oc.emissive[3] = d.mat.uvOffset[0];
    oc.emissive[0] = d.mat.emissive.x; oc.emissive[1] = d.mat.emissive.y; oc.emissive[2] = d.mat.emissive.z;
    fillFog(oc, d.mat);
    oc.fogColor[3] = d.mat.uvOffset[1];
    oc.fogParams[3] = packMaterialExtra(d.mat.normalMap ? d.mat.normalStrength : 0.f, d.mat.alphaCutoff);
    InstanceGPU inst{};
    std::memcpy(inst.world, &oc.world, sizeof oc.world);
    std::memcpy(inst.color, oc.color, sizeof oc.color);
    inst.metallic = oc.metallic; inst.roughness = oc.roughness; inst.receiveShadow = oc.receiveShadow; inst.pad = oc.pad;
    std::memcpy(inst.texInfo, oc.texInfo, sizeof oc.texInfo);
    std::memcpy(inst.emissive, oc.emissive, sizeof oc.emissive);
    std::memcpy(inst.fogColor, oc.fogColor, sizeof oc.fogColor);
    std::memcpy(inst.fogParams, oc.fogParams, sizeof oc.fogParams);
    return inst;
  }

  bool ensureBuffer(rhi::BufferId& buf, u32& cap, size_t required, size_t stride,
                    rhi::BufferUsage usage = rhi::BufferUsage::Storage, bool dynamic = true) {
    if (required == 0) return true;
    if (required <= cap && !buf.isNull()) return true;
    u32 newCap = cap ? cap : 64u;
    while (newCap < required) newCap *= 2u;
    if (!buf.isNull()) dev_->destroy(buf);
    buf = dev_->createBuffer({static_cast<size_t>(newCap) * stride, usage, dynamic, usage == rhi::BufferUsage::Indirect ? 0u : static_cast<u32>(stride)}, nullptr);
    cap = newCap;
    return !buf.isNull();
  }

  // Roda o compute de culling dos lotes opacos (opaquePlan_) sobre instBuf_ -> compactBuf_ + args indiretos (cullArgsBuf_).
  // Chamado fora de qualquer render pass. Devolve false se algum recurso faltar (o quadro cai no caminho CPU).
  bool runGpuCull(size_t opaqueCount, const render::Frustum& fr) {
    const size_t nb = opaquePlan_.batches.size();
    if (!ensureBuffer(compactBuf_, compactCap_, opaqueCount, sizeof(InstanceGPU), rhi::BufferUsage::StorageRW, false) ||
        !ensureBuffer(cullBatchBuf_, cullBatchCap_, nb, sizeof(CullBatchGPU)) ||
        !ensureBuffer(cullArgsBuf_, cullArgsCap_, nb, sizeof(rhi::DrawIndexedIndirectArgs), rhi::BufferUsage::Indirect, false)) return false;
    cullBatchCpu_.resize(nb);
    cullArgsCpu_.assign(nb, rhi::DrawIndexedIndirectArgs{});
    u32 maxCount = 0;
    for (size_t i = 0; i < nb; ++i) {
      const InstanceBatch& b = opaquePlan_.batches[i];
      const Draw& d = draws_[renderQueues_.opaque[opaquePlan_.order[b.first]]];
      CullBatchGPU& cb = cullBatchCpu_[i];
      cb.first = b.first; cb.count = b.count; cb.indexCount = d.mesh->count; cb.argsIndex = static_cast<u32>(i);
      const render::SphereBounds& sb = d.mesh->bounds;
      cb.sphere[0] = sb.center[0]; cb.sphere[1] = sb.center[1]; cb.sphere[2] = sb.center[2];
      cb.sphere[3] = sb.valid ? sb.radius : -1.f;   // raio < 0 = bounds invalidos: nunca culla
      cullArgsCpu_[i].indexCount = d.mesh->count;   // instanceCount = 0: o compute acumula (atomico) so os visiveis
      maxCount = std::max(maxCount, b.count);
    }
    CullCB cc{};
    for (int p = 0; p < 6; ++p) { cc.planes[p][0] = fr.p[p].n[0]; cc.planes[p][1] = fr.p[p].n[1]; cc.planes[p][2] = fr.p[p].n[2]; cc.planes[p][3] = fr.p[p].d; }
    dev_->updateBuffer(cullCb_, &cc, sizeof cc);
    dev_->updateBuffer(cullBatchBuf_, cullBatchCpu_.data(), nb * sizeof(CullBatchGPU));
    dev_->updateBuffer(cullArgsBuf_, cullArgsCpu_.data(), nb * sizeof(rhi::DrawIndexedIndirectArgs));
    auto cs = dev_->beginCommands();
    cs->setPipeline(cullPipe_);
    cs->setUniform(0, cullCb_);
    cs->setStorage(0, cullBatchBuf_);
    cs->setStorage(1, instBuf_);
    cs->setStorageRW(0, compactBuf_);
    cs->setStorageRW(1, cullArgsBuf_);
    cs->dispatch((maxCount + 63u) / 64u, static_cast<u32>(nb), 1);   // x = instancias do lote (64 por grupo), y = lote
    dev_->submit(*cs);
    return true;
  }

  void addDraw(Draw&& d) { (d.mat.opacity < 1.f ? tdraws_ : draws_).push_back(std::move(d)); }
  rhi::PipelineId transPipe_;
  rhi::PipelineId pipeDs_, transPipeDs_;   // variantes sem cull (material dupla face); nulas = cai no pipeline normal
  bool normalSlot_ = false;                // pipeline tem o slot kNormalMapSlot (normal map)
  rhi::TextureId color_, depth_, gbuf_, aoRaw_, ao_;
  rhi::TextureId glow_;
  rhi::ShaderId emiPs_; rhi::PipelineId emiPipe_;
  rhi::TextureId bloomA_, bloomB_, outFull_;
  rhi::TextureId out_;
  bool bloomOn_ = false; f32 bloomThr_ = 0.75f, bloomStr_ = 0.8f;
  render::RenderViewStats stats_{};
  rhi::ShaderId bloomThrPs_, bloomBlurPs_, bloomCompPs_;
  rhi::PipelineId bloomThrPipe_, bloomBlurPipe_, bloomCompPipe_;
  rhi::BufferId bloomCbThr_, bloomCbH_, bloomCbV_, bloomCbComp_;
  void destroyTarget(Target& t) { dev_->destroy(t.glow); dev_->destroy(t.color); dev_->destroy(t.depth); dev_->destroy(t.gbuf); dev_->destroy(t.aoRaw); dev_->destroy(t.ao); dev_->destroy(t.bloomA); dev_->destroy(t.bloomB); dev_->destroy(t.out); }
  rhi::Format colorFmt_ = rhi::Format::RGBA8_SRGB;
  std::unordered_map<u64, Target> targets_;
  u64 frame_ = 0;
  std::unique_ptr<rhi::ICommandList> cl_;
  std::unordered_map<const Mesh*, GpuMesh> meshes_;
  std::unordered_map<u32, GpuMesh> dyn_;
  std::vector<f32> packBuf_;
  u32 w_ = 0, h_ = 0, drawCount_ = 0;
};

} // namespace tool