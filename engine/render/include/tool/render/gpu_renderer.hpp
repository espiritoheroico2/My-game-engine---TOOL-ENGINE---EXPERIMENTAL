// ToolEngine - GpuRenderer: desenha a cena 3D iluminada (PBR) sobre o RHI (Diligent: Vulkan / D3D12).
// A iluminacao vem da Light Prism: luzes e clusters sao calculados pelo backend Diligent da propria biblioteca
// (ClusterSystem), no mesmo dispositivo e contexto do RHI. As cascatas do sol sao calculadas em CPU (ComputeCsm) e
// cada uma tem o seu mapa de profundidade. Shaders: assets/shaders/pbr.hlsl + assets/shaders/lightprism/.
#pragma once
#include "tool/render/lighting.hpp"
#include "tool/render/instance_batch.hpp"
#include "tool/render/render_queue.hpp"
#include "tool/render/frustum_cull.hpp"
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

// Le um .hlsl resolvendo `#include "arquivo"`. Como o compilador HLSL, o include e relativo ao ARQUIVO que o contem
// (ex.: Common/ClusterLighting.hlsli inclui "PBR.hlsli" e acha Common/PBR.hlsli). `file` e relativo a `dir`.
// Cada arquivo entra uma vez so (como #pragma once): os headers da Light Prism se incluem entre si por caminhos diferentes.
inline bool loadHlslRec(const std::string& dir, const std::string& file, std::string& out, std::set<std::string>& seen, int depth) {
  if (depth > 12) return false;
  std::error_code ec;
  const std::filesystem::path full = std::filesystem::path(dir) / file;
  const std::string key = std::filesystem::weakly_canonical(full, ec).string();
  if (!ec && !seen.insert(key).second) return true;   // ja incluido: pula
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

// Layouts identicos aos cbuffers de pbr.hlsl e shadow.hlsl. Os da Light Prism (Globals, sol, CSM, IBL, clusters) sao
// os proprios structs da biblioteca (LightPrism::GlobalLightingGPU, CsmGPU, IblGPU, ClusterGridGPU), que ja batem com o HLSL.
struct FrameCB { Mat4 viewProj; Mat4 view; f32 camera[3]; f32 pad = 0; };
struct ObjectCB { Mat4 world; f32 color[4]; f32 metallic, roughness, receiveShadow, pad; f32 texInfo[4]; f32 emissive[4]; f32 fogColor[4]; f32 fogParams[4]; }; // texInfo: largura, altura, 1 = tem textura; emissive: rgb = cor x intensidade
struct ShadowCB { Mat4 lightViewProj; };
struct ShadowInstanceGPU { Mat4 world; };
struct BatchCB { u32 first; u32 pad[3]; };
// Parametros do bloom (espelho de bloom.hlsl): tamanhos em pixels, direcao do blur, limiar e forca.
struct BloomCB { f32 srcSize[2]; f32 haloSize[2]; f32 dir[2]; f32 threshold, strength; };
static_assert(sizeof(FrameCB) == 144 && sizeof(ObjectCB) == 160 && sizeof(ShadowCB) == 64 && sizeof(ShadowInstanceGPU) == 64 && sizeof(BatchCB) == 16 && sizeof(BloomCB) == 32, "layout de cbuffer");
static_assert(sizeof(InstanceGPU) == sizeof(ObjectCB), "layout de instancia deve bater com ObjectCB");
// Sombras de luzes pontuais/spot (espelho de PointShadowCB em lightprism/Common/PointShadow.hlsli).
struct PointShadowLightGPU { f32 posType[4]; f32 params[4]; Mat4 vp[6]; };
struct PointShadowGPU { u32 count; u32 pad[3]; PointShadowLightGPU lights[kMaxPointShadowLights]; };
static_assert(sizeof(PointShadowLightGPU) == 416 && sizeof(PointShadowGPU) == 16 + 4 * 416, "layout de PointShadowCB");
static_assert(sizeof(LightPrism::CsmGPU) == 352 && sizeof(LightPrism::Ibl::IblGPU) == 160, "layout dos cbuffers da Light Prism");

// Nomes dos recursos no pbr.hlsl (ordem = slots do pipeline). Mudou no HLSL? Mude aqui tambem.
inline const std::vector<std::string>& pbrUniformNames() {
  static const std::vector<std::string> n = {"FrameBuffer", "BatchBuffer", "GlobalLightCB", "SunLightCB", "CsmCB", "IblCB", "ClusterGridCB", "PointShadowCB"};
  return n;
}
inline const std::vector<std::string>& pbrTextureNames() {
  static const std::vector<std::string> n = {"AlbedoMap", "CsmMap0", "CsmMap1", "CsmMap2", "CsmMap3", "PointShadowMap0", "PointShadowMap1", "PointShadowMap2", "PointShadowMap3", "AoMap", "IblBrdfLut"};
  return n;
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
  // Libera tudo na GPU. Chamar ANTES de destruir o dispositivo.
  void shutdown() { release(); }

  // shaderDir = pasta "assets/shaders" (pbr.hlsl + lightprism/). `lp` = dispositivo da Light Prism sobre o mesmo RHI
  // (rhi::createLightPrismDevice). Sem ele (backend sem Diligent), o init falha e o editor volta para o renderizador
  // em CPU. No OpenGL os shaders PBR usam o GLSL 430 de glsl/ (mk abaixo); o init nao falha por isso.
  bool init(rhi::IDevice& dev, std::unique_ptr<LightPrism::IGraphicsDevice> lp, const std::string& shaderDir, u32 w, u32 h) {
    release();
    if (!lp) { debug().report("TOOL-E142", Severity::Error, "dispositivo Light Prism indisponivel (backend sem Diligent)"); return false; }
    dev_ = &dev; lp_ = std::move(lp);
    LightPrism::ClusterSystemDesc cd;
    cd.shaderDirectory = shaderDir + "/lightprism";
    clusters_ = std::make_unique<LightPrism::ClusterSystem>(cd);

    std::string src;
    if (!loadHlsl(shaderDir, "pbr.hlsl", src)) { debug().report("TOOL-E140", Severity::Error, "nao achei pbr.hlsl em " + shaderDir); release(); return false; }
    std::string legacySrc;
    if (!loadHlsl(shaderDir, "pbr_object.hlsl", legacySrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei pbr_object.hlsl em " + shaderDir); release(); return false; }
    std::string sh;
    if (!loadHlsl(shaderDir, "shadow.hlsl", sh)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow.hlsl em " + shaderDir); release(); return false; }
    std::string ssaoSrc, prepassSrc;
    if (!loadHlsl(shaderDir, "ssao.hlsl", ssaoSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei ssao.hlsl em " + shaderDir); release(); return false; }
    if (!loadHlsl(shaderDir, "prepass.hlsl", prepassSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei prepass.hlsl em " + shaderDir); release(); return false; }
    std::string shp, shClear;
    if (!loadHlsl(shaderDir, "shadow_point.hlsl", shp)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow_point.hlsl em " + shaderDir); release(); return false; }
    if (!loadHlsl(shaderDir, "shadow_clear.hlsl", shClear)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow_clear.hlsl em " + shaderDir); release(); return false; }

    // OpenGL: cada shader usa o GLSL 430 gerado offline (tools/shaders/build_glsl.py -> glsl/<nome>.430.glsl).
    // Vulkan/D3D12 seguem com o HLSL. Um ponto unico de criacao: mk(desc).
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
    pd.uniformNames = pbrUniformNames(); pd.textureNames = pbrTextureNames(); pd.storageNames = pbrStorageNames();
    // Cor: Vulkan/D3D12 usam alvo sRGB (o hardware converte a saida linear do ACES).
    colorFmt_ = rhi::Format::RGBA8_SRGB;
    pd.color = colorFmt_;
    pipe_ = dev.createPipeline(pd);
    // Passe de emissao usa o VS instanciado do PBR; o pixel shader so consome a radiancia emissiva.
    rhi::ShaderDesc emi = ps; emi.entry = "EmissionPS"; emi.name = "pbr.emi";
    emiPs_ = mk(emi);
    rhi::PipelineDesc ed; ed.vs = vs_; ed.ps = emiPs_; ed.name = "emission"; ed.depthWrite = false; ed.depthTest = true;
    ed.layout = pd.layout; ed.vertexStride = pd.vertexStride; ed.color = rhi::Format::RGBA16F;
    ed.uniformNames = {"FrameBuffer", "BatchBuffer"}; ed.textureNames.clear(); ed.storageNames = {"Instances"};
    emiPipe_ = dev.createPipeline(ed);
    // Transparente usa o PBR instanciado; somente lotes adjacentes sao agrupados para preservar a ordem alfa.
    rhi::PipelineDesc td = pd; td.name = "transparent"; td.blend = true; td.depthWrite = false;
    transPipe_ = dev.createPipeline(td);

    // passe de sombra: so posicao, grava z da luz num alvo R32F, sem culling (as duas faces projetam sombra)
    rhi::ShaderDesc svs = vs, sps = ps;
    svs.name = "shadow.vs"; sps.name = "shadow.ps"; svs.source = sh; sps.source = sh;
    svs.entry = "VSMain"; sps.entry = "PSMain";
    shVs_ = mk(svs); shPs_ = mk(sps);
    rhi::PipelineDesc sd; sd.vs = shVs_; sd.ps = shPs_; sd.name = "shadow"; sd.color = rhi::Format::R32F; sd.cull = rhi::CullMode::None;
    sd.layout = {{3, 0}}; sd.vertexStride = 32; sd.uniformNames = {"ShadowBuffer", "BatchBuffer"}; sd.storageNames = {"Instances"};
    shPipe_ = dev.createPipeline(sd);
    // passe de sombra de pontuais/spot: grava a distancia (clip.w) numa faixa R x 6R por luz
    rhi::ShaderDesc pvs = svs, pps = sps;
    pvs.name = "shadowpoint.vs"; pps.name = "shadowpoint.ps"; pvs.source = shp; pps.source = shp;
    ptVs_ = mk(pvs); ptPs_ = mk(pps);
    rhi::PipelineDesc pd2 = sd; pd2.vs = ptVs_; pd2.ps = ptPs_; pd2.name = "shadowpoint";
    ptPipe_ = dev.createPipeline(pd2);
    rhi::ShaderDesc clearPs; clearPs.stage = rhi::ShaderStage::Pixel; clearPs.entry = "PSMain";
    clearPs.name = "shadowclear.ps"; clearPs.source = shClear; pointClearPs_ = mk(clearPs);
    // SSAO: prepasso (normal + profundidade RGBA16F), AO bruto e blur (triangulo de tela cheia, ver fsVb_)
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
    {   // Bloom (bloom.hlsl): limiar, blur e composicao, todos em tela cheia com o mesmo triangulo do SSAO (fsVs_)
      std::string bloomSrc;
      if (!loadHlsl(shaderDir, "bloom.hlsl", bloomSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei bloom.hlsl em " + shaderDir); release(); return false; }
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
    {   // LUT da BRDF (split-sum) da LightPrism como textura RGBA8: A em R, B em G (valores em [0,1]).
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
    ensureShadowTargets(1024); // alvos "vazios" para o shader sempre ter as 4 texturas ligadas
    ensurePointTargets(64);    // idem para as sombras de pontuais/spot (4 faixas pequenas ate uma luz pedir a resolucao)
    resize(w, h);
    return !color_.isNull();
  }

  // Escolhe o alvo de desenho (cor+profundidade) para w*h. As janelas Scene e Game desenham com tamanhos
  // diferentes no mesmo quadro; por isso guardamos um alvo por tamanho. Alvos sem uso por 120 quadros sao liberados.
  void resize(u32 w, u32 h) {
    if (!dev_) return;
    w_ = std::max(1u, w); h_ = std::max(1u, h);
    const u64 key = (static_cast<u64>(w_) << 32) | h_;
    auto it = targets_.find(key);
    if (it == targets_.end()) {
      Target t;
      t.color = dev_->createTexture({w_, h_, 1, colorFmt_, true}, nullptr);
      t.depth = dev_->createTexture({w_, h_, 1, rhi::Format::D32F, true}, nullptr);
      t.gbuf = dev_->createTexture({w_, h_, 1, rhi::Format::RGBA16F, true}, nullptr);  // SSAO: normal de view + profundidade
      t.aoRaw = dev_->createTexture({w_, h_, 1, rhi::Format::R32F, true}, nullptr);
      t.ao = dev_->createTexture({w_, h_, 1, rhi::Format::R32F, true}, nullptr);
      // Bloom: dois alvos de meia resolucao (limiar e blur) e a saida final (cor + halo), na resolucao cheia.
      const u32 hw = std::max(1u, w_ / 2), hh = std::max(1u, h_ / 2);
      t.bloomA = dev_->createTexture({hw, hh, 1, rhi::Format::RGBA16F, true}, nullptr);
      t.bloomB = dev_->createTexture({hw, hh, 1, rhi::Format::RGBA16F, true}, nullptr);
      t.out = dev_->createTexture({w_, h_, 1, colorFmt_, true}, nullptr);
      t.glow = dev_->createTexture({w_, h_, 1, rhi::Format::RGBA16F, true}, nullptr); // brilho: so a emissao (bloom)
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

  // Iluminacao do quadro (chamar entre begin() e draw()). `rig` ja fez LightRig::build() com as MESMAS view/proj desta
  // camera. Sobe as luzes e os clusters para a GPU (LightPrism), monta o ambiente e prepara as cascatas do sol.
  // reuse = true: nada que projeta sombra nem a luz mudou: reaproveita os mapas do quadro anterior (sem passe de sombra).
  // Retorna false se a Light Prism falhar (LastError() diz o motivo).
  // Point/Spot mantem uma assinatura independente por face; o cache nao depende da camera.
  bool setLighting(LightRig& rig, const Mat4& view, const Mat4& proj, bool reuse = false) {
    if (!dev_ || !lp_) return false;
    const LightFrame& fr = rig.frame();
    LightPrism::LightingSystem& sys = rig.system();
    // 1) luzes na GPU (buffers da LightPrism) e 2) clusters (compute da LightPrism)
    if (!sys.UploadToGPU(*lp_)) { lastError_ = "Light Prism: falha ao enviar as luzes para a GPU"; return false; }
    LightPrism::ClusterCamera cam = LightPrism::MakeClusterCamera(LightRig::toLp(view), LightRig::toLp(proj), w_, h_, cam_zNear_, cam_zFar_);
    if (!clusters_->Execute(*lp_, sys, rig.lastView(), cam)) { lastError_ = "Light Prism clusters: " + clusters_->LastError(); return false; }
    // 3) importa os buffers da LightPrism para o RHI (nao sao donos: a LightPrism recria quando precisa)
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
    // 4) cascatas e ambiente (CPU -> uniformes da engine)
    const bool on = fr.shadows();
    const u32 count = on ? std::min<u32>(fr.csm.gpu.cascadeCount, LightPrism::kMaxCsmCascades) : 0u;
    const int res = on ? std::clamp(static_cast<int>(fr.csm.gpu.shadowMapSize), 128, 4096) : shRes_;
    const bool sameLayout = on && res == static_cast<int>(shW_) && count == shadowCount_;
    shadowOn_ = on; shadowCount_ = count;
    shadowSkip_ = on && reuse && shadowValid_ && sameLayout;
    if (on) ensureShadowTargets(static_cast<u32>(res));
    for (u32 k = 0; k < count; ++k) cascadeVp_[k] = toTool(fr.csm.gpu.cascades[k].viewProj);
    dev_->updateBuffer(csmCb_, &fr.csm.gpu, sizeof(LightPrism::CsmGPU));
    // Sombras de pontuais/spot (engine): ate 4 luzes, uma faixa R x 6R por luz, cacheada face a face.
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
    // SSAO (constantes da Light Prism, SsaoGPU): a projecao vem do chamador (mesma camera do quadro)
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

  // begin/draw/end: begin() so guarda os parametros e draw() so ANOTA os objetos; tudo e enviado a GPU em end(),
  // na ordem certa: 1) passe de sombra (uma passada por cascata), 2) passe principal (agrupado por malha).
  void begin(const Camera& cam, Vec3 clear = {0.02f, 0.025f, 0.035f}, f32 clearAlpha = 1.f) {
    if (!dev_) return;
    ++frame_;
    cam_ = cam; clear_ = clear; clearAlpha_ = clearAlpha;
    cam_zNear_ = cam.nearZ; cam_zFar_ = cam.farZ;
    draws_.clear(); tdraws_.clear(); shadowOn_ = false; shadowSkip_ = false; open_ = true;
    stats_ = {};
    stats_.valid = true;
    // Remocao de malhas que nao aparecem ha 240 quadros (evita vazamento de buffers na GPU).
    if ((frame_ & 63) == 0) for (auto it = meshes_.begin(); it != meshes_.end();) {
      if (frame_ - it->second.lastUse > 240) { freeMesh(it->second); it = meshes_.erase(it); } else ++it;
    }
  }

  // Desenha uma malha dinamica (um slot por lote). Use slots distintos para lotes diferentes no mesmo quadro.
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

  // Textura do albedo na GPU (cache por Texture::id). Sem textura, devolve a branca 1x1 (o shader ignora).
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
    queueItems_.clear();
    queueItems_.reserve(draws_.size());
    for (const Draw& d : draws_) {
      const render::SphereBounds worldBounds = render::transformSphere(d.world.m, d.mesh->bounds);
      const bool cameraVisible = render::sphereInFrustum(cameraFrustum, worldBounds);
      const bool emissive = !(d.mat.emissive.x <= 0.f && d.mat.emissive.y <= 0.f && d.mat.emissive.z <= 0.f);
      queueItems_.push_back({cameraVisible, false, cameraVisible && emissive, true, d.mat.castShadow && d.mesh->count > 0});
    }
    render::buildRenderQueues(queueItems_, renderQueues_);
    tdraws_.erase(std::remove_if(tdraws_.begin(), tdraws_.end(), [&](const Draw& d) {
      const render::SphereBounds worldBounds = render::transformSphere(d.world.m, d.mesh->bounds);
      return !render::sphereInFrustum(cameraFrustum, worldBounds);
    }), tdraws_.end());
    if (!ensureInstanceBuffer(std::max(renderQueues_.opaque.size(), tdraws_.size()))) {
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
      if (shadowInstances_.size() > shadowInstCap_) {
        u32 newCap = shadowInstCap_ ? shadowInstCap_ : 64u;
        while (newCap < shadowInstances_.size()) newCap *= 2u;
        if (!shadowInstBuf_.isNull()) dev_->destroy(shadowInstBuf_);
        shadowInstBuf_ = dev_->createBuffer({static_cast<size_t>(newCap) * sizeof(ShadowInstanceGPU), rhi::BufferUsage::Storage, true, sizeof(ShadowInstanceGPU)}, nullptr);
        shadowInstCap_ = newCap;
      }
      if (!shadowInstBuf_.isNull()) dev_->updateBuffer(shadowInstBuf_, shadowInstances_.data(), shadowInstances_.size() * sizeof(ShadowInstanceGPU));
    }
    // ---- 1) sombras: um passe por cascata ----
    if (shadowOn_ && !shadowSkip_) {
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::CsmShadow)]};
      for (u32 k = 0; k < shadowCount_; ++k) {
        auto cl = dev_->beginCommands();
        cl->begin(shColor_[k], shDepth_, 1.f, 1.f, 1.f, 1.f); // limpa para "longe" (z = 1)
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
          dev_->updateBuffer(batchCb_, &bc, sizeof bc);
          cl->setUniform(1, batchCb_);
          const Draw& d = *queue.orderedDraws[b.first];
          cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
          recordDraw(*cl, render::RenderPassId::CsmShadow, d.mesh->count, b.count);
        }
        cl->end(); dev_->submit(*cl);
      }
      shadowValid_ = true;
    }
    // ---- 1b) sombras Point/Spot: cada face invalida e limpa apenas sua regiao da faixa R x 6R ----
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
            dev_->updateBuffer(batchCb_, &bc, sizeof bc);
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
    // ---- 1c) SSAO: prepasso (normal de view + profundidade), AO bruto e blur. Sem SSAO o AO fica 1 (limpo). ----
    if (ssaoOn_) {
      std::vector<BatchItem> preItems;
      BatchPlan prePlan;
      planQueuedBatches(renderQueues_.prepass, preItems, prePlan, [&](u32 index) {
        return BatchItem{reinterpret_cast<u64>(draws_[index].mesh), 0u};
      }, 4096);
      std::vector<ShadowInstanceGPU> preCpu;
      preCpu.reserve(prePlan.order.size());
      for (u32 k : prePlan.order) {
        ShadowInstanceGPU inst{};
        inst.world = draws_[renderQueues_.prepass[k]].world;
        preCpu.push_back(inst);
      }
      if (!preCpu.empty()) {
        if (preCpu.size() > prepassInstCap_) {
          u32 newCap = prepassInstCap_ ? prepassInstCap_ : 64u;
          while (newCap < preCpu.size()) newCap *= 2u;
          if (!prepassInstBuf_.isNull()) dev_->destroy(prepassInstBuf_);
          prepassInstBuf_ = dev_->createBuffer({static_cast<size_t>(newCap) * sizeof(ShadowInstanceGPU), rhi::BufferUsage::Storage, true, sizeof(ShadowInstanceGPU)}, nullptr);
          prepassInstCap_ = newCap;
        }
        if (!prepassInstBuf_.isNull()) dev_->updateBuffer(prepassInstBuf_, preCpu.data(), preCpu.size() * sizeof(ShadowInstanceGPU));
      }
      render::PassTimer prepassTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Prepass)]};
      auto pl = dev_->beginCommands();
      pl->begin(gbuf_, depth_, 0.f, 0.f, 0.f, 0.f); // w = 0: sem geometria
      pl->setPipeline(prePipe_);
      pl->setUniform(0, frameCb_);
      if (!preCpu.empty()) pl->setStorage(0, prepassInstBuf_);
      for (const InstanceBatch& b : prePlan.batches) {
        BatchCB bc{};
        bc.first = b.first;
        dev_->updateBuffer(batchCb_, &bc, sizeof bc);
        pl->setUniform(1, batchCb_);
        const Draw& d = draws_[renderQueues_.prepass[prePlan.order[b.first]]];
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
      cl->begin(ao_, kNullHandle, 1.f, 1.f, 1.f, 1.f); // sem SSAO: AO = 1 em todo o quadro
      cl->end(); dev_->submit(*cl);
    }
    // Ligacoes comuns do passe PBR (opaco e transparente): mesmos buffers, mapas e luzes.
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
      // Mapas de sombra: as cascatas que nao existem apontam para o primeiro mapa (o shader so le as validas).
      for (u32 k = 0; k < 4; ++k) c->setTexture(1 + k, shColor_[k < shadowCount_ ? k : 0]);
      for (u32 i = 0; i < 4; ++i) c->setTexture(5 + i, pointStrip_[i < pointCount_ ? i : 0]);
      c->setTexture(9, ao_);
      c->setTexture(10, brdfTex_);   // LUT da BRDF (IBL especular)
    };
    render::PassTimer opaqueTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Opaque)]};
    cl_ = dev_->beginCommands();
    // Alvo UNORM (OpenGL): nao usamos (o OpenGL ainda nao tem iluminacao); mantemos o clear em linear para Vulkan/D3D12.
    cl_->begin(color_, depth_, clear_.x, clear_.y, clear_.z, clearAlpha_);
    bindScene(cl_.get(), pipe_);
    std::vector<BatchItem> items;
    BatchPlan plan;
    planQueuedBatches(renderQueues_.opaque, items, plan, [&](u32 index) {
      const Draw& d = draws_[index];
      const u64 meshId = reinterpret_cast<u64>(d.mesh);
      const u64 texId = d.tex.isNull() ? 0u : (static_cast<u64>(d.tex.generation) << 32) | d.tex.index;
      return BatchItem{meshId, texId};
    }, 4096);
    instCpu_.clear();
    instCpu_.reserve(plan.order.size());
    for (u32 k : plan.order) {
      const Draw& d = draws_[renderQueues_.opaque[k]];
      instCpu_.push_back(makeInstanceGPU(d, 1.f));
    }
    if (!instCpu_.empty()) {
      if (!ensureInstanceBuffer(instCpu_.size())) {
        cl_->end(); dev_->submit(*cl_); cl_.reset();
        lastError_ = "storage buffer de instancias indisponivel";
        releaseImports();
        return;
      }
      dev_->updateBuffer(instBuf_, instCpu_.data(), instCpu_.size() * sizeof(InstanceGPU));
      cl_->setStorage(4, instBuf_);
    }
    for (const InstanceBatch& b : plan.batches) {
      BatchCB bc{};
      bc.first = b.first;
      dev_->updateBuffer(batchCb_, &bc, sizeof bc);
      cl_->setUniform(1, batchCb_);
      const Draw& d = draws_[renderQueues_.opaque[plan.order[b.first]]];
      cl_->setTexture(0, d.tex.isNull() ? white_ : d.tex);
      cl_->setVertexBuffer(d.mesh->vertex); cl_->setIndexBuffer(d.mesh->index);
      recordDraw(*cl_, render::RenderPassId::Opaque, d.mesh->count, b.count);
    }
    cl_->end(); dev_->submit(*cl_); cl_.reset();
    opaqueTimer.stop();
    // Brilho do bloom: limpa em preto (nada emite) e desenha so os objetos com emissao, com a profundidade do desenho
    // principal. Assim o halo so aparece ao redor do que emite, e nao atras de paredes.
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
        instCpu_.clear();
        instCpu_.reserve(emissionPlan_.order.size());
        for (u32 k : emissionPlan_.order) {
          const Draw& d = draws_[renderQueues_.emissive[k]];
          InstanceGPU inst{};
          std::memcpy(inst.world, &d.world, sizeof d.world);
          inst.emissive[0] = d.mat.emissive.x;
          inst.emissive[1] = d.mat.emissive.y;
          inst.emissive[2] = d.mat.emissive.z;
          instCpu_.push_back(inst);
        }
        if (!instCpu_.empty() && !instBuf_.isNull()) {
          dev_->updateBuffer(instBuf_, instCpu_.data(), instCpu_.size() * sizeof(InstanceGPU));
          gl->setStorage(0, instBuf_);
          for (const InstanceBatch& b : emissionPlan_.batches) {
            BatchCB bc{};
            bc.first = b.first;
            dev_->updateBuffer(batchCb_, &bc, sizeof bc);
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
    // Passe transparente: objetos com opacidade < 1, do mais distante para o mais perto, misturados sobre o opaco.
    // Usa a profundidade do opaco sem escrever nela (nao aparecem atras de paredes). Sombra e brilho nao vem daqui.
    if (!tdraws_.empty() && !transPipe_.isNull()) {
      render::PassTimer transparentTimer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::Transparent)]};
      const Vec3 eye = cam_.eye;
      auto dist2 = [&](const Draw& d) { const Vec3 p = d.world.transformPoint({0, 0, 0}); const Vec3 q = p - eye; return dot(q, q); };
      std::sort(tdraws_.begin(), tdraws_.end(), [&](const Draw& a, const Draw& b) { return dist2(a) > dist2(b); });
      transparentItemsScratch_.clear();
      transparentItemsScratch_.reserve(tdraws_.size());
      for (const Draw& d : tdraws_) {
        const u64 meshId = reinterpret_cast<u64>(d.mesh);
        const u64 texId = d.tex.isNull() ? 0u : (static_cast<u64>(d.tex.generation) << 32) | d.tex.index;
        transparentItemsScratch_.push_back({meshId, texId});
      }
      planAdjacentInstanceBatches(transparentItemsScratch_, transparentPlan_, 4096);
      instCpu_.clear();
      instCpu_.reserve(transparentPlan_.order.size());
      for (u32 index : transparentPlan_.order) {
        const Draw& d = tdraws_[index];
        instCpu_.push_back(makeInstanceGPU(d, d.mat.opacity));
      }
      if (!instCpu_.empty()) {
        if (!ensureInstanceBuffer(instCpu_.size())) {
          lastError_ = "storage buffer de instancias indisponivel";
          transparentTimer.stop();
          releaseImports();
          return;
        }
        dev_->updateBuffer(instBuf_, instCpu_.data(), instCpu_.size() * sizeof(InstanceGPU));
      }
      auto tl = dev_->beginCommands();
      tl->beginKeep(color_, depth_);
      bindScene(tl.get(), transPipe_);
      if (!instCpu_.empty()) tl->setStorage(4, instBuf_);
      for (const InstanceBatch& b : transparentPlan_.batches) {
        BatchCB bc{};
        bc.first = b.first;
        dev_->updateBuffer(batchCb_, &bc, sizeof bc);
        tl->setUniform(1, batchCb_);
        const Draw& d = tdraws_[transparentPlan_.order[b.first]];
        tl->setTexture(0, d.tex.isNull() ? white_ : d.tex);
        tl->setVertexBuffer(d.mesh->vertex);
        tl->setIndexBuffer(d.mesh->index);
        recordDraw(*tl, render::RenderPassId::Transparent, d.mesh->count, b.count);
      }
      tl->end(); dev_->submit(*tl);
    }
    out_ = color_;
    if (bloomOn_ && !bloomA_.isNull() && !bloomThrPipe_.isNull()) { runBloom(); out_ = outFull_; }
    releaseImports();
    drawCount_ = static_cast<u32>(plan.batches.size());
  }

  bool readback(std::vector<u8>& rgba, u32& w, u32& h) { return dev_ && dev_->readTexture(out_, rgba, w, h); }
  // Leitura sem esperar a GPU (editor): 1 = quadro novo em rgba, 0 = ainda nao, -1 = falha. Ver rhi.hpp.
  int readbackAsync(int slot, std::vector<u8>& rgba, u32& w, u32& h) { return dev_ ? dev_->readTextureAsync(out_, slot, rgba, w, h) : -1; }
  // Bloom do quadro (pos-processamento): liga/desliga e parametros. Valem no proximo end().
  void setBloom(bool on, f32 threshold, f32 strength) { bloomOn_ = on; bloomThr_ = threshold; bloomStr_ = strength; }
  rhi::TextureId colorTarget() const { return color_; }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  u32 draws() const { return drawCount_; }
  size_t cachedMeshes() const { return meshes_.size(); }
  const render::RenderViewStats& renderStats() const { return stats_; }
  // Estatisticas da Light Prism do ultimo quadro (clusters em uso e memoria).
  const LightPrism::ClusterSystem* clusters() const { return clusters_.get(); }

  // Libera a copia na GPU de uma malha (chamar se a malha for destruida/alterada).
  void forget(const Mesh& m) {
    auto it = meshes_.find(&m);
    if (it == meshes_.end()) return;
    dev_->destroy(it->second.vertex); dev_->destroy(it->second.index); meshes_.erase(it);
  }

private:
  void recordDraw(rhi::ICommandList& commands, render::RenderPassId pass, u32 indexCount, u32 instances) {
    commands.drawIndexed(indexCount, instances);
    stats_.passes[static_cast<std::size_t>(pass)].recordDraw(indexCount, instances);
  }

  // Malha na GPU. posKey/nVert/nIdx = o que foi enviado: se mudar, os buffers sao refeitos (o endereco sozinho nao basta).
  struct GpuMesh { rhi::BufferId vertex, index; u32 count = 0; const void* posKey = nullptr; size_t nVert = 0, nIdx = 0, vCap = 0, iCap = 0; u64 lastUse = 0; u32 rev = 0; bool dynamic = false; render::SphereBounds bounds; };
  struct Draw { const GpuMesh* mesh; Mat4 world; Material mat; rhi::TextureId tex; f32 tw, th; };
  struct ShadowQueue { BatchPlan plan; std::vector<const Draw*> orderedDraws; size_t instanceBase = 0; };
  struct PointFaceSignature { u64 light = 0, casters = 0; bool operator==(const PointFaceSignature&) const = default; };
  struct PointFaceCache { PointFaceSignature signature{}; bool valid = false, targetResetPending = false, renderFailed = false; };
  struct Target { rhi::TextureId color, depth, gbuf, aoRaw, ao, bloomA, bloomB, out, glow; u64 lastUse = 0; };
  // Buffers da Light Prism importados neste quadro (validos ate releaseImports()).
  struct Imports { rhi::BufferId points, spots, globals, sun, grid, cells, indices; };

  static Mat4 toTool(const LightPrism::Mat4& m) { Mat4 r; for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r.m[c][k] = m.m[k][c]; return r; } // linha -> coluna

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
    shadowItemsScratch_.clear();
    shadowDrawsScratch_.clear();
    queue.orderedDraws.clear();
    queue.plan.order.clear();
    queue.plan.batches.clear();
    queue.instanceBase = 0;
    for (const Draw* d : casters) {
      const render::SphereBounds worldBounds = render::transformSphere(d->world.m, d->mesh->bounds);
      const bool visible = csm ? render::sphereInFrustumSides(frustum, worldBounds)
                               : render::sphereInFrustum(frustum, worldBounds);
      if (!visible) continue;
      shadowDrawsScratch_.push_back(d);
    }
    planQueuedBatches(shadowDrawsScratch_, shadowItemsScratch_, queue.plan, [](const Draw* d) {
      return BatchItem{reinterpret_cast<u64>(d->mesh), 0u};
    }, 4096);
    queue.orderedDraws.reserve(queue.plan.order.size());
    for (u32 index : queue.plan.order) {
      const Draw* d = shadowDrawsScratch_[index];
      queue.orderedDraws.push_back(d);
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

  // Layout de vertice fixo (stride 32): POSITION f3 @0, NORMAL f3 @12, TEXCOORD0 f2 @24 (0 se a malha nao tem UV).
  static void packVertices(const Mesh& m, std::vector<f32>& v) {
    v.clear(); v.reserve(m.pos.size() * 8);
    const bool hasUv = m.uv.size() == m.pos.size() * 2;
    for (size_t i = 0; i < m.pos.size(); ++i) { v.insert(v.end(), {m.pos[i].x, m.pos[i].y, m.pos[i].z, m.normal[i].x, m.normal[i].y, m.normal[i].z, hasUv ? m.uv[2 * i] : 0.f, hasUv ? m.uv[2 * i + 1] : 0.f}); }
  }
  void freeMesh(GpuMesh& g) {
    if (!g.vertex.isNull()) dev_->destroy(g.vertex);
    if (!g.index.isNull()) dev_->destroy(g.index);
    const u64 lu = g.lastUse; g = GpuMesh{}; g.lastUse = lu;   // preserva o uso: a remocao por inatividade depende dele
  }
  // Bloom: limiar (cor -> meia resolucao), blur horizontal e vertical, e composicao (cor + halo) em out_.
  void runBloom() {
    BloomCB base{};
    base.srcSize[0] = static_cast<f32>(w_); base.srcSize[1] = static_cast<f32>(h_);
    base.haloSize[0] = static_cast<f32>(std::max(1u, w_ / 2)); base.haloSize[1] = static_cast<f32>(std::max(1u, h_ / 2));
    base.threshold = 0.f; base.strength = bloomStr_; // o brilho ja so tem emissao: sem limiar
    BloomCB hz = base; hz.dir[0] = 1.f; hz.dir[1] = 0.f;
    BloomCB vt = base; vt.dir[0] = 0.f; vt.dir[1] = 1.f;
    dev_->updateBuffer(bloomCbThr_, &base, sizeof base);
    dev_->updateBuffer(bloomCbH_, &hz, sizeof hz);
    dev_->updateBuffer(bloomCbV_, &vt, sizeof vt);
    dev_->updateBuffer(bloomCbComp_, &base, sizeof base);
    auto fullscreen = [&](rhi::ICommandList& c, render::RenderPassId pass) {
      c.setVertexBuffer(fsVb_); c.setIndexBuffer(fsIb_); recordDraw(c, pass, 3, 1);
    };
    { // 1) limiar: cor -> bloomA (meia resolucao)
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomExtract)]};
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomThrPipe_); c->setUniform(0, bloomCbThr_); c->setTexture(0, glow_);
      fullscreen(*c, render::RenderPassId::BloomExtract); c->end(); dev_->submit(*c);
    }
    { // 2) blur horizontal: bloomA -> bloomB
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomBlurH)]};
      auto c = dev_->beginCommands();
      c->begin(bloomB_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbH_); c->setTexture(0, bloomA_);
      fullscreen(*c, render::RenderPassId::BloomBlurH); c->end(); dev_->submit(*c);
    }
    { // 3) blur vertical: bloomB -> bloomA
      render::PassTimer timer{stats_.passes[static_cast<std::size_t>(render::RenderPassId::BloomBlurV)]};
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbV_); c->setTexture(0, bloomB_);
      fullscreen(*c, render::RenderPassId::BloomBlurV); c->end(); dev_->submit(*c);
    }
    { // 4) composicao: cor + halo -> out_ (resolucao cheia)
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
  // Cache de malhas estaticas. Reenvia se o endereco OU o tamanho mudou (um ponteiro reaproveitado nao vira malha antiga).
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
  // Malha DINAMICA (ex.: particulas): mesmo slot = mesmos buffers, atualizados com updateBuffer.
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

  // Um mapa R32F por cascata + uma profundidade compartilhada (cada passe limpa a sua).
  void ensureShadowTargets(u32 res) {
    if (shW_ == res && !shColor_[0].isNull()) return;
    for (auto& t : shColor_) if (!t.isNull()) dev_->destroy(t);
    if (!shDepth_.isNull()) dev_->destroy(shDepth_);
    for (u32 k = 0; k < LightPrism::kMaxCsmCascades; ++k) shColor_[k] = dev_->createTexture({res, res, 1, rhi::Format::R32F, true}, nullptr);
    shDepth_ = dev_->createTexture({res, res, 1, rhi::Format::D32F, true}, nullptr);
    shW_ = res; shRes_ = static_cast<int>(res); shadowValid_ = false;
  }

  // Uma faixa R32F R x 6R por luz com sombra (4 no maximo) e uma profundidade R x 6R compartilhada (cada luz limpa a sua).
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
    for (auto* h : {&frameCb_, &objCb_, &batchCb_, &instBuf_, &shadowInstBuf_, &prepassInstBuf_, &shCb_, &csmCb_, &iblCb_, &pointCb_, &brdfTex_, &shDepth_, &pointDepth_, &pipe_, &shPipe_, &ptPipe_, &pointClearPipe_, &vs_, &ps_, &legacyVs_, &legacyPs_, &shVs_, &shPs_, &ptVs_, &ptPs_, &pointClearPs_,
                    &prePipe_, &ssaoPipe_, &blurPipe_, &fsVb_, &fsIb_, &ssaoCb_, &preVs_, &prePs_, &fsVs_, &ssaoPs_, &blurPs_,
                    &emiPipe_, &emiPs_, &transPipe_, &bloomThrPipe_, &bloomBlurPipe_, &bloomCompPipe_, &bloomCbThr_, &bloomCbH_, &bloomCbV_, &bloomCbComp_, &bloomThrPs_, &bloomBlurPs_, &bloomCompPs_}) if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    for (auto& t : shColor_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    for (auto& t : pointStrip_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    pointW_ = 0; pointOn_ = false; pointCount_ = 0; pointFaceCache_ = {};
    instCap_ = 0; shadowInstCap_ = 0; prepassInstCap_ = 0; instCpu_.clear();
    clusters_.reset(); lp_.reset();
    shW_ = 0; shRes_ = 0; shadowCount_ = 0; shadowValid_ = false; draws_.clear(); dev_ = nullptr;
  }

  rhi::IDevice* dev_ = nullptr;
  std::unique_ptr<LightPrism::IGraphicsDevice> lp_;
  std::unique_ptr<LightPrism::ClusterSystem> clusters_;
  rhi::ShaderId vs_, ps_, legacyVs_, legacyPs_, shVs_, shPs_, ptVs_, ptPs_, pointClearPs_, preVs_, prePs_, fsVs_, ssaoPs_, blurPs_;
  rhi::PipelineId prePipe_, ssaoPipe_, blurPipe_;
  rhi::BufferId fsVb_, fsIb_, ssaoCb_;
  rhi::PipelineId pipe_, shPipe_, ptPipe_, pointClearPipe_;
  rhi::BufferId frameCb_, objCb_, batchCb_, instBuf_, shadowInstBuf_, prepassInstBuf_, shCb_, csmCb_, iblCb_, pointCb_;
  u32 instCap_ = 0, shadowInstCap_ = 0, prepassInstCap_ = 0;
  std::vector<InstanceGPU> instCpu_;
  std::array<ShadowQueue, LightPrism::kMaxCsmCascades> csmQueues_;
  std::array<ShadowQueue, kMaxPointShadowLights * kMaxPointShadowFaces> pointQueues_;
  std::vector<const Draw*> shadowCasters_, shadowDrawsScratch_;
  std::vector<render::RenderQueueItem> queueItems_;
  render::RenderQueues renderQueues_;
  std::vector<BatchItem> emissionItemsScratch_;
  BatchPlan emissionPlan_;
  std::vector<BatchItem> transparentItemsScratch_;
  BatchPlan transparentPlan_;
  std::vector<BatchItem> shadowItemsScratch_;
  std::vector<ShadowInstanceGPU> shadowInstances_;
  rhi::TextureId brdfTex_;
  rhi::TextureId pointStrip_[kMaxPointShadowLights], pointDepth_;
  u32 pointW_ = 0, pointCount_ = 0, pointRes_ = 512;
  bool pointOn_ = false, ssaoOn_ = false;
  std::array<std::array<PointFaceCache, kMaxPointShadowFaces>, kMaxPointShadowLights> pointFaceCache_{};
  u32 pointFaces_[kMaxPointShadowLights] = {};
  ShadowLight pointLights_[kMaxPointShadowLights];
  Mat4 pointVp_[kMaxPointShadowLights][kMaxPointShadowFaces];
  std::vector<rhi::BufferId> imports_;   // handles importados neste quadro (liberados em releaseImports)
  Imports imp_{};
  std::unordered_map<u64, rhi::TextureId> texs_;   // texturas de albedo (por Texture::id)
  rhi::TextureId white_;                           // 1x1 branca: textura padrao
  rhi::TextureId makeWhite() { const u8 px[4] = {255, 255, 255, 255}; return dev_->createTexture({1, 1, 1, rhi::Format::RGBA8, false}, px); }
  Draw makeDraw(const GpuMesh* g, const Mat4& world, const Material& mat) {
    f32 tw = 0, th = 0;
    if (white_.isNull() && dev_) white_ = makeWhite();
    const rhi::TextureId tex = textureFor(mat.albedoMap, tw, th);
    return Draw{g, world, mat, tex, tw, th};
  }
  rhi::TextureId shColor_[LightPrism::kMaxCsmCascades], shDepth_; u32 shW_ = 0; int shRes_ = 0;
  u32 shadowCount_ = 0;
  bool shadowOn_ = false, open_ = false, shadowSkip_ = false, shadowValid_ = false;
  Mat4 cascadeVp_[LightPrism::kMaxCsmCascades];
  std::string lastError_;
  Camera cam_; Vec3 clear_; f32 clearAlpha_ = 1.f; f32 cam_zNear_ = 0.1f, cam_zFar_ = 100.f;
  std::vector<Draw> draws_;
  std::vector<Draw> tdraws_;                  // objetos transparentes (opacity < 1): passe proprio, do fundo para a frente
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
    oc.emissive[0] = d.mat.emissive.x; oc.emissive[1] = d.mat.emissive.y; oc.emissive[2] = d.mat.emissive.z;
    fillFog(oc, d.mat);
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
  bool ensureInstanceBuffer(size_t required) {
    if (required == 0) return true;
    if (required <= instCap_ && !instBuf_.isNull()) return true;
    u32 newCap = instCap_ ? instCap_ : 64u;
    while (newCap < required) newCap *= 2u;
    if (!instBuf_.isNull()) dev_->destroy(instBuf_);
    instBuf_ = dev_->createBuffer({static_cast<size_t>(newCap) * sizeof(InstanceGPU), rhi::BufferUsage::Storage, true, sizeof(InstanceGPU)}, nullptr);
    instCap_ = newCap;
    return !instBuf_.isNull();
  }
  void addDraw(Draw&& d) { (d.mat.opacity < 1.f ? tdraws_ : draws_).push_back(std::move(d)); }
  rhi::PipelineId transPipe_;
  rhi::TextureId color_, depth_, gbuf_, aoRaw_, ao_;
  rhi::TextureId glow_;                       // brilho do bloom (so a emissao), mesma resolucao da cor
  rhi::ShaderId emiPs_; rhi::PipelineId emiPipe_;
  rhi::TextureId bloomA_, bloomB_, outFull_;  // outFull_: alvo da composicao (cor + bloom), separado da cor (le e escreve)
  rhi::TextureId out_;                         // imagem final lida pela CPU: color_ ou outFull_
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
  std::unordered_map<u32, GpuMesh> dyn_;   // malhas dinamicas por slot
  std::vector<f32> packBuf_;
  u32 w_ = 0, h_ = 0, drawCount_ = 0;
};

} // namespace tool
