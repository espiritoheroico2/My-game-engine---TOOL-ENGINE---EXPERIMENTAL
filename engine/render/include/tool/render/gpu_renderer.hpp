// ToolEngine - GpuRenderer: desenha a cena 3D iluminada (PBR) sobre o RHI (Diligent: Vulkan / D3D12).
// A iluminacao vem da Light Prism: luzes e clusters sao calculados pelo backend Diligent da propria biblioteca
// (ClusterSystem), no mesmo dispositivo e contexto do RHI. As cascatas do sol sao calculadas em CPU (ComputeCsm) e
// cada uma tem o seu mapa de profundidade. Shaders: assets/shaders/pbr.hlsl + assets/shaders/lightprism/.
#pragma once
#include "tool/render/lighting.hpp"
#include "tool/render/soft_renderer.hpp"
#include "tool/rhi/rhi.hpp"

#include "LightPrism/Backends/DiligentDevice.hpp"
#include "LightPrism/Cluster/ClusterSystem.hpp"

#include <cstring>
#include <filesystem>
#include <set>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace tool {

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
struct ObjectCB { Mat4 world; f32 color[4]; f32 metallic, roughness, receiveShadow, pad; f32 texInfo[4]; f32 emissive[4]; }; // texInfo: largura, altura, 1 = tem textura; emissive: rgb = cor x intensidade
struct ShadowCB { Mat4 lightViewProj, world; };
// Parametros do bloom (espelho de bloom.hlsl): tamanhos em pixels, direcao do blur, limiar e forca.
struct BloomCB { f32 srcSize[2]; f32 haloSize[2]; f32 dir[2]; f32 threshold, strength; };
static_assert(sizeof(FrameCB) == 144 && sizeof(ObjectCB) == 128 && sizeof(ShadowCB) == 128 && sizeof(BloomCB) == 32, "layout de cbuffer");
// Sombras de luzes pontuais/spot (espelho de PointShadowCB em lightprism/Common/PointShadow.hlsli).
struct PointShadowLightGPU { f32 posType[4]; f32 params[4]; Mat4 vp[6]; };
struct PointShadowGPU { u32 count; u32 pad[3]; PointShadowLightGPU lights[kMaxPointShadowLights]; };
static_assert(sizeof(PointShadowLightGPU) == 416 && sizeof(PointShadowGPU) == 16 + 4 * 416, "layout de PointShadowCB");
static_assert(sizeof(LightPrism::CsmGPU) == 352 && sizeof(LightPrism::Ibl::IblGPU) == 160, "layout dos cbuffers da Light Prism");

// Nomes dos recursos no pbr.hlsl (ordem = slots do pipeline). Mudou no HLSL? Mude aqui tambem.
inline const std::vector<std::string>& pbrUniformNames() {
  static const std::vector<std::string> n = {"FrameBuffer", "ObjectBuffer", "GlobalLightCB", "SunLightCB", "CsmCB", "IblCB", "ClusterGridCB", "PointShadowCB"};
  return n;
}
inline const std::vector<std::string>& pbrTextureNames() {
  static const std::vector<std::string> n = {"AlbedoMap", "CsmMap0", "CsmMap1", "CsmMap2", "CsmMap3", "PointShadowMap0", "PointShadowMap1", "PointShadowMap2", "PointShadowMap3", "AoMap", "IblBrdfLut"};
  return n;
}
inline const std::vector<std::string>& pbrStorageNames() {
  static const std::vector<std::string> n = {"PointLightsBuffer", "SpotLightsBuffer", "LightGrid", "LightIndexList"};
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
  // (rhi::createLightPrismDevice). Sem ele, ou no OpenGL (ainda sem a versao GLSL da iluminacao), o init falha e o
  // editor volta para o renderizador em CPU.
  bool init(rhi::IDevice& dev, std::unique_ptr<LightPrism::IGraphicsDevice> lp, const std::string& shaderDir, u32 w, u32 h) {
    release();
    if (!lp) { debug().report("TOOL-E142", Severity::Error, "dispositivo Light Prism indisponivel (backend sem Diligent)"); return false; }
    dev_ = &dev; lp_ = std::move(lp);
    LightPrism::ClusterSystemDesc cd;
    cd.shaderDirectory = shaderDir + "/lightprism";
    clusters_ = std::make_unique<LightPrism::ClusterSystem>(cd);

    std::string src;
    if (!loadHlsl(shaderDir, "pbr.hlsl", src)) { debug().report("TOOL-E140", Severity::Error, "nao achei pbr.hlsl em " + shaderDir); release(); return false; }
    std::string sh;
    if (!loadHlsl(shaderDir, "shadow.hlsl", sh)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow.hlsl em " + shaderDir); release(); return false; }
    std::string ssaoSrc, prepassSrc;
    if (!loadHlsl(shaderDir, "ssao.hlsl", ssaoSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei ssao.hlsl em " + shaderDir); release(); return false; }
    if (!loadHlsl(shaderDir, "prepass.hlsl", prepassSrc)) { debug().report("TOOL-E140", Severity::Error, "nao achei prepass.hlsl em " + shaderDir); release(); return false; }
    std::string shp;
    if (!loadHlsl(shaderDir, "shadow_point.hlsl", shp)) { debug().report("TOOL-E140", Severity::Error, "nao achei shadow_point.hlsl em " + shaderDir); release(); return false; }

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
    rhi::PipelineDesc pd; pd.vs = vs_; pd.ps = ps_; pd.name = "pbr";
    pd.layout = {{3, 0}, {3, 12}, {2, 24}}; pd.vertexStride = 32;
    pd.uniformNames = pbrUniformNames(); pd.textureNames = pbrTextureNames(); pd.storageNames = pbrStorageNames();
    // Cor: Vulkan/D3D12 usam alvo sRGB (o hardware converte a saida linear do ACES).
    colorFmt_ = rhi::Format::RGBA8_SRGB;
    pd.color = colorFmt_;
    pipe_ = dev.createPipeline(pd);

    // passe de sombra: so posicao, grava z da luz num alvo R32F, sem culling (as duas faces projetam sombra)
    rhi::ShaderDesc svs = vs, sps = ps;
    svs.name = "shadow.vs"; sps.name = "shadow.ps"; svs.source = sh; sps.source = sh;
    svs.entry = "VSMain"; sps.entry = "PSMain";
    shVs_ = mk(svs); shPs_ = mk(sps);
    rhi::PipelineDesc sd; sd.vs = shVs_; sd.ps = shPs_; sd.name = "shadow"; sd.color = rhi::Format::R32F; sd.cull = rhi::CullMode::None;
    sd.layout = {{3, 0}}; sd.vertexStride = 32; sd.uniformNames = {"ShadowBuffer"};
    shPipe_ = dev.createPipeline(sd);
    // passe de sombra de pontuais/spot: grava a distancia (clip.w) numa faixa R x 6R por luz
    rhi::ShaderDesc pvs = svs, pps = sps;
    pvs.name = "shadowpoint.vs"; pps.name = "shadowpoint.ps"; pvs.source = shp; pps.source = shp;
    ptVs_ = mk(pvs); ptPs_ = mk(pps);
    rhi::PipelineDesc pd2 = sd; pd2.vs = ptVs_; pd2.ps = ptPs_; pd2.name = "shadowpoint";
    ptPipe_ = dev.createPipeline(pd2);
    // SSAO: prepasso (normal + profundidade RGBA16F), AO bruto e blur (triangulo de tela cheia, ver fsVb_)
    rhi::ShaderDesc pre; pre.stage = rhi::ShaderStage::Vertex; pre.entry = "VSMain"; pre.name = "prepass.vs"; pre.source = prepassSrc;
    rhi::ShaderDesc prePs = pre; prePs.stage = rhi::ShaderStage::Pixel; prePs.entry = "PSMain"; prePs.name = "prepass.ps";
    preVs_ = mk(pre); prePs_ = mk(prePs);
    rhi::PipelineDesc pp; pp.vs = preVs_; pp.ps = prePs_; pp.name = "prepass";
    pp.layout = {{3, 0}, {3, 12}, {2, 24}}; pp.vertexStride = 32; pp.uniformNames = {"FrameBuffer", "ObjectBuffer"};
    pp.color = rhi::Format::RGBA16F; pp.cull = rhi::CullMode::Back;
    prePipe_ = dev.createPipeline(pp);
    rhi::ShaderDesc fsv; fsv.stage = rhi::ShaderStage::Vertex; fsv.entry = "VSMain"; fsv.name = "ssao.vs"; fsv.source = ssaoSrc;
    rhi::ShaderDesc ssP = fsv; ssP.stage = rhi::ShaderStage::Pixel; ssP.entry = "SsaoMain"; ssP.name = "ssao.ps";
    rhi::ShaderDesc blP = fsv; blP.stage = rhi::ShaderStage::Pixel; blP.entry = "SsaoBlur"; blP.name = "ssaoblur.ps";
    fsVs_ = mk(fsv); ssaoPs_ = mk(ssP); blurPs_ = mk(blP);
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
    if (prePipe_.isNull() || ssaoPipe_.isNull() || blurPipe_.isNull() || fsVb_.isNull() || fsIb_.isNull() || ssaoCb_.isNull() || pipe_.isNull() || shPipe_.isNull() || ptPipe_.isNull() || frameCb_.isNull() || objCb_.isNull() || shCb_.isNull() || csmCb_.isNull() || iblCb_.isNull() || pointCb_.isNull()) { release(); return false; }
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
      it = targets_.emplace(key, t).first;
    }
    it->second.lastUse = frame_;
    color_ = it->second.color; depth_ = it->second.depth; gbuf_ = it->second.gbuf; aoRaw_ = it->second.aoRaw; ao_ = it->second.ao;
    bloomA_ = it->second.bloomA; bloomB_ = it->second.bloomB; outFull_ = it->second.out;
    for (auto i = targets_.begin(); i != targets_.end();) {
      if (frame_ - i->second.lastUse > 120) { destroyTarget(i->second); i = targets_.erase(i); }
      else ++i;
    }
  }

  // Iluminacao do quadro (chamar entre begin() e draw()). `rig` ja fez LightRig::build() com as MESMAS view/proj desta
  // camera. Sobe as luzes e os clusters para a GPU (LightPrism), monta o ambiente e prepara as cascatas do sol.
  // reuse = true: nada que projeta sombra nem a luz mudou: reaproveita os mapas do quadro anterior (sem passe de sombra).
  // Retorna false se a Light Prism falhar (LastError() diz o motivo).
  // pointReuse = true: luzes pontuais/spot e casters iguais ao quadro anterior (a camera nao entra aqui): os mapas de
  // face sao mantidos e o passe de sombra pontual nao roda. O chamador garante a igualdade (assinatura).
  bool setLighting(LightRig& rig, const Mat4& view, const Mat4& proj, bool reuse = false, bool pointReuse = false) {
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
    // Sombras de pontuais/spot (engine): ate 4 luzes, uma faixa R x 6R por luz. Reaproveitadas com pointReuse.
    pointCount_ = std::min<u32>(static_cast<u32>(fr.shadowLights.size()), kMaxPointShadowLights);
    pointOn_ = pointCount_ > 0;
    pointRes_ = std::clamp<u32>(fr.pointShadowResolution, 64u, 1024u);
    if (pointOn_) ensurePointTargets(pointRes_); // recriar os alvos zera pointValid_
    pointSkip_ = pointOn_ && pointReuse && pointValid_ && pointRes_ == pointResBuilt_ && pointCount_ == pointCountBuilt_;
    PointShadowGPU pg{};
    pg.count = pointCount_;
    for (u32 i = 0; i < pointCount_; ++i) {
      const ShadowLight& sl = fr.shadowLights[i];
      PointShadowLightGPU& L = pg.lights[i];
      L.posType[0] = sl.position.x; L.posType[1] = sl.position.y; L.posType[2] = sl.position.z;
      L.posType[3] = sl.type == LightType::Spot ? 1.f : 0.f;
      L.params[0] = sl.nearZ; L.params[1] = static_cast<f32>(sl.faces); L.params[2] = static_cast<f32>(pointRes_); L.params[3] = 0.f;
      pointFaces_[i] = sl.faces;
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
    draws_.clear(); shadowOn_ = false; shadowSkip_ = false; open_ = true;
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
    draws_.push_back(makeDraw(&g, world, mat));
  }

  void draw(const Mesh& m, const Mat4& world, const Material& mat) {
    if (!open_) return;
    GpuMesh& g = upload(m);
    if (g.index.isNull()) return;
    draws_.push_back(makeDraw(&g, world, mat));
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
    // ---- 1) sombras: um passe por cascata ----
    if (shadowOn_ && !shadowSkip_) {
      for (u32 k = 0; k < shadowCount_; ++k) {
        auto cl = dev_->beginCommands();
        cl->begin(shColor_[k], shDepth_, 1.f, 1.f, 1.f, 1.f); // limpa para "longe" (z = 1)
        cl->setPipeline(shPipe_);
        for (const Draw& d : draws_) {
          if (!d.mat.castShadow) continue;
          ShadowCB sc{cascadeVp_[k], d.world};
          dev_->updateBuffer(shCb_, &sc, sizeof sc);
          cl->setUniform(0, shCb_);
          cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
          cl->drawIndexed(d.mesh->count, 1);
        }
        cl->end(); dev_->submit(*cl);
      }
      shadowValid_ = true;
    }
    // ---- 1b) sombras de pontuais/spot: uma faixa R x 6R por luz; cada face e um viewport da faixa ----
    if (pointOn_ && !pointSkip_) {
      for (u32 i = 0; i < pointCount_; ++i) {
        auto cl = dev_->beginCommands();
        cl->begin(pointStrip_[i], pointDepth_, 1e30f, 1e30f, 1e30f, 1e30f); // "longe" em metros (distancia)
        cl->setPipeline(ptPipe_);
        for (u32 f = 0; f < pointFaces_[i]; ++f) {
          cl->setViewport(0, f * pointRes_, pointRes_, pointRes_);
          for (const Draw& d : draws_) {
            if (!d.mat.castShadow) continue;
            ShadowCB sc{pointVp_[i][f], d.world};
            dev_->updateBuffer(shCb_, &sc, sizeof sc);
            cl->setUniform(0, shCb_);
            cl->setVertexBuffer(d.mesh->vertex); cl->setIndexBuffer(d.mesh->index);
            cl->drawIndexed(d.mesh->count, 1);
          }
        }
        cl->end(); dev_->submit(*cl);
      }
    }
    pointValid_ = pointOn_;                       // mapas prontos (refeitos agora ou reaproveitados)
    pointResBuilt_ = pointRes_; pointCountBuilt_ = pointCount_;
    // ---- 2) passe principal ----
    const Mat4 view = Mat4::lookAt(cam_.eye, cam_.target, cam_.up);
    const Mat4 vp = cam_.projection(static_cast<f32>(w_), static_cast<f32>(h_)) * view;
    FrameCB fc; fc.viewProj = vp; fc.view = view; fc.camera[0] = cam_.eye.x; fc.camera[1] = cam_.eye.y; fc.camera[2] = cam_.eye.z;
    dev_->updateBuffer(frameCb_, &fc, sizeof fc);
    std::stable_sort(draws_.begin(), draws_.end(), [](const Draw& a, const Draw& b) { return a.mesh->vertex.index < b.mesh->vertex.index; });
    // ---- 1c) SSAO: prepasso (normal de view + profundidade), AO bruto e blur. Sem SSAO o AO fica 1 (limpo). ----
    if (ssaoOn_) {
      auto pl = dev_->beginCommands();
      pl->begin(gbuf_, depth_, 0.f, 0.f, 0.f, 0.f); // w = 0: sem geometria
      pl->setPipeline(prePipe_);
      pl->setUniform(0, frameCb_);
      for (const Draw& d : draws_) {
        ObjectCB oc{}; oc.world = d.world;
        dev_->updateBuffer(objCb_, &oc, sizeof oc);
        pl->setUniform(1, objCb_);
        pl->setVertexBuffer(d.mesh->vertex); pl->setIndexBuffer(d.mesh->index);
        pl->drawIndexed(d.mesh->count, 1);
      }
      pl->end(); dev_->submit(*pl);
      auto sc = dev_->beginCommands();
      sc->begin(aoRaw_, kNullHandle, 1.f, 1.f, 1.f, 1.f);
      sc->setPipeline(ssaoPipe_);
      sc->setUniform(0, ssaoCb_);
      sc->setTexture(0, gbuf_);
      sc->setVertexBuffer(fsVb_); sc->setIndexBuffer(fsIb_);
      sc->drawIndexed(3, 1);
      sc->end(); dev_->submit(*sc);
      auto bl = dev_->beginCommands();
      bl->begin(ao_, kNullHandle, 1.f, 1.f, 1.f, 1.f);
      bl->setPipeline(blurPipe_);
      bl->setUniform(0, ssaoCb_);
      bl->setTexture(0, gbuf_);
      bl->setTexture(1, aoRaw_);
      bl->setVertexBuffer(fsVb_); bl->setIndexBuffer(fsIb_);
      bl->drawIndexed(3, 1);
      bl->end(); dev_->submit(*bl);
    } else {
      auto cl = dev_->beginCommands();
      cl->begin(ao_, kNullHandle, 1.f, 1.f, 1.f, 1.f); // sem SSAO: AO = 1 em todo o quadro
      cl->end(); dev_->submit(*cl);
    }
    cl_ = dev_->beginCommands();
    // Alvo UNORM (OpenGL): nao usamos (o OpenGL ainda nao tem iluminacao); mantemos o clear em linear para Vulkan/D3D12.
    cl_->begin(color_, depth_, clear_.x, clear_.y, clear_.z, clearAlpha_);
    cl_->setPipeline(pipe_);
    cl_->setUniform(0, frameCb_);
    cl_->setUniform(2, imp_.globals);
    cl_->setUniform(3, imp_.sun);
    cl_->setUniform(4, csmCb_);
    cl_->setUniform(5, iblCb_);
    cl_->setUniform(6, imp_.grid);
    cl_->setUniform(7, pointCb_);
    cl_->setStorage(0, imp_.points);
    cl_->setStorage(1, imp_.spots);
    cl_->setStorage(2, imp_.cells);
    cl_->setStorage(3, imp_.indices);
    // Mapas de sombra: as cascatas que nao existem apontam para o primeiro mapa (o shader so le as validas).
    for (u32 k = 0; k < 4; ++k) cl_->setTexture(1 + k, shColor_[k < shadowCount_ ? k : 0]);
    for (u32 i = 0; i < 4; ++i) cl_->setTexture(5 + i, pointStrip_[i < pointCount_ ? i : 0]);
    cl_->setTexture(9, ao_);
    cl_->setTexture(10, brdfTex_);   // LUT da BRDF (IBL especular)
    const GpuMesh* last = nullptr;
    for (const Draw& d : draws_) {
      ObjectCB oc{}; oc.world = d.world; oc.color[0] = d.mat.albedo.x; oc.color[1] = d.mat.albedo.y; oc.color[2] = d.mat.albedo.z; oc.color[3] = 1.f;
      oc.metallic = d.mat.metallic; oc.roughness = d.mat.roughness; oc.receiveShadow = d.mat.receiveShadow ? 1.f : 0.f; // o shader decide: sol (cascatas) e pontuais/spot
      oc.texInfo[0] = d.tw; oc.texInfo[1] = d.th; oc.texInfo[2] = d.tw > 0.f ? 1.f : 0.f;
      oc.emissive[0] = d.mat.emissive.x; oc.emissive[1] = d.mat.emissive.y; oc.emissive[2] = d.mat.emissive.z; oc.emissive[3] = 0.f;
      dev_->updateBuffer(objCb_, &oc, sizeof oc);
      cl_->setUniform(1, objCb_);
      cl_->setTexture(0, d.tex.isNull() ? white_ : d.tex);
      if (d.mesh != last) { cl_->setVertexBuffer(d.mesh->vertex); cl_->setIndexBuffer(d.mesh->index); last = d.mesh; }
      cl_->drawIndexed(d.mesh->count, 1);
    }
    cl_->end(); dev_->submit(*cl_); cl_.reset();
    out_ = color_;
    if (bloomOn_ && !bloomA_.isNull() && !bloomThrPipe_.isNull()) { runBloom(); out_ = outFull_; }
    releaseImports();
    drawCount_ = static_cast<u32>(draws_.size());
  }

  bool readback(std::vector<u8>& rgba, u32& w, u32& h) { return dev_ && dev_->readTexture(out_, rgba, w, h); }
  // Bloom do quadro (pos-processamento): liga/desliga e parametros. Valem no proximo end().
  void setBloom(bool on, f32 threshold, f32 strength) { bloomOn_ = on; bloomThr_ = threshold; bloomStr_ = strength; }
  rhi::TextureId colorTarget() const { return color_; }
  u32 width() const { return w_; }
  u32 height() const { return h_; }
  u32 draws() const { return drawCount_; }
  size_t cachedMeshes() const { return meshes_.size(); }
  // Estatisticas da Light Prism do ultimo quadro (clusters em uso e memoria).
  const LightPrism::ClusterSystem* clusters() const { return clusters_.get(); }

  // Libera a copia na GPU de uma malha (chamar se a malha for destruida/alterada).
  void forget(const Mesh& m) {
    auto it = meshes_.find(&m);
    if (it == meshes_.end()) return;
    dev_->destroy(it->second.vertex); dev_->destroy(it->second.index); meshes_.erase(it);
  }

private:
  // Malha na GPU. posKey/nVert/nIdx = o que foi enviado: se mudar, os buffers sao refeitos (o endereco sozinho nao basta).
  struct GpuMesh { rhi::BufferId vertex, index; u32 count = 0; const void* posKey = nullptr; size_t nVert = 0, nIdx = 0, vCap = 0, iCap = 0; u64 lastUse = 0; u32 rev = 0; };
  struct Draw { const GpuMesh* mesh; Mat4 world; Material mat; rhi::TextureId tex; f32 tw, th; };
  struct Target { rhi::TextureId color, depth, gbuf, aoRaw, ao, bloomA, bloomB, out; u64 lastUse = 0; };
  // Buffers da Light Prism importados neste quadro (validos ate releaseImports()).
  struct Imports { rhi::BufferId points, spots, globals, sun, grid, cells, indices; };

  static Mat4 toTool(const LightPrism::Mat4& m) { Mat4 r; for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r.m[c][k] = m.m[k][c]; return r; } // linha -> coluna

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
    base.threshold = bloomThr_; base.strength = bloomStr_;
    BloomCB hz = base; hz.dir[0] = 1.f; hz.dir[1] = 0.f;
    BloomCB vt = base; vt.dir[0] = 0.f; vt.dir[1] = 1.f;
    dev_->updateBuffer(bloomCbThr_, &base, sizeof base);
    dev_->updateBuffer(bloomCbH_, &hz, sizeof hz);
    dev_->updateBuffer(bloomCbV_, &vt, sizeof vt);
    dev_->updateBuffer(bloomCbComp_, &base, sizeof base);
    auto fullscreen = [&](rhi::ICommandList& c) { c.setVertexBuffer(fsVb_); c.setIndexBuffer(fsIb_); c.drawIndexed(3, 1); };
    { // 1) limiar: cor -> bloomA (meia resolucao)
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomThrPipe_); c->setUniform(0, bloomCbThr_); c->setTexture(0, color_);
      fullscreen(*c); c->end(); dev_->submit(*c);
    }
    { // 2) blur horizontal: bloomA -> bloomB
      auto c = dev_->beginCommands();
      c->begin(bloomB_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbH_); c->setTexture(0, bloomA_);
      fullscreen(*c); c->end(); dev_->submit(*c);
    }
    { // 3) blur vertical: bloomB -> bloomA
      auto c = dev_->beginCommands();
      c->begin(bloomA_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomBlurPipe_); c->setUniform(0, bloomCbV_); c->setTexture(0, bloomB_);
      fullscreen(*c); c->end(); dev_->submit(*c);
    }
    { // 4) composicao: cor + halo -> out_ (resolucao cheia)
      auto c = dev_->beginCommands();
      c->begin(outFull_, kNullHandle, 0.f, 0.f, 0.f, 1.f);
      c->setPipeline(bloomCompPipe_); c->setUniform(0, bloomCbComp_); c->setTexture(0, color_); c->setTexture(1, bloomA_);
      fullscreen(*c); c->end(); dev_->submit(*c);
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
    if (pointW_ == res && !pointStrip_[0].isNull()) return;
    pointValid_ = false;
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
    targets_.clear(); color_ = depth_ = bloomA_ = bloomB_ = outFull_ = out_ = kNullHandle;
    for (auto& [k, t] : texs_) dev_->destroy(t);
    texs_.clear();
    if (!white_.isNull()) { dev_->destroy(white_); white_ = kNullHandle; }
    for (auto* h : {&frameCb_, &objCb_, &shCb_, &csmCb_, &iblCb_, &pointCb_, &brdfTex_, &shDepth_, &pointDepth_, &pipe_, &shPipe_, &ptPipe_, &vs_, &ps_, &shVs_, &shPs_, &ptVs_, &ptPs_,
                    &prePipe_, &ssaoPipe_, &blurPipe_, &fsVb_, &fsIb_, &ssaoCb_, &preVs_, &prePs_, &fsVs_, &ssaoPs_, &blurPs_,
                    &bloomThrPipe_, &bloomBlurPipe_, &bloomCompPipe_, &bloomCbThr_, &bloomCbH_, &bloomCbV_, &bloomCbComp_, &bloomThrPs_, &bloomBlurPs_, &bloomCompPs_}) if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    for (auto& t : shColor_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    for (auto& t : pointStrip_) if (!t.isNull()) { dev_->destroy(t); t = kNullHandle; }
    pointW_ = 0; pointOn_ = false; pointCount_ = 0; pointValid_ = false;
    clusters_.reset(); lp_.reset();
    shW_ = 0; shRes_ = 0; shadowCount_ = 0; shadowValid_ = false; draws_.clear(); dev_ = nullptr;
  }

  rhi::IDevice* dev_ = nullptr;
  std::unique_ptr<LightPrism::IGraphicsDevice> lp_;
  std::unique_ptr<LightPrism::ClusterSystem> clusters_;
  rhi::ShaderId vs_, ps_, shVs_, shPs_, ptVs_, ptPs_, preVs_, prePs_, fsVs_, ssaoPs_, blurPs_;
  rhi::PipelineId prePipe_, ssaoPipe_, blurPipe_;
  rhi::BufferId fsVb_, fsIb_, ssaoCb_;
  rhi::PipelineId pipe_, shPipe_, ptPipe_;
  rhi::BufferId frameCb_, objCb_, shCb_, csmCb_, iblCb_, pointCb_;
  rhi::TextureId brdfTex_;
  rhi::TextureId pointStrip_[kMaxPointShadowLights], pointDepth_;
  u32 pointW_ = 0, pointCount_ = 0, pointRes_ = 512;
  bool pointOn_ = false, ssaoOn_ = false;
  bool pointSkip_ = false, pointValid_ = false;   // mapas de sombra pontual reaproveitados / prontos
  u32 pointResBuilt_ = 0, pointCountBuilt_ = 0;
  u32 pointFaces_[kMaxPointShadowLights] = {};
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
  rhi::TextureId color_, depth_, gbuf_, aoRaw_, ao_;
  rhi::TextureId bloomA_, bloomB_, outFull_;  // outFull_: alvo da composicao (cor + bloom), separado da cor (le e escreve)
  rhi::TextureId out_;                         // imagem final lida pela CPU: color_ ou outFull_
  bool bloomOn_ = false; f32 bloomThr_ = 0.75f, bloomStr_ = 0.8f;
  rhi::ShaderId bloomThrPs_, bloomBlurPs_, bloomCompPs_;
  rhi::PipelineId bloomThrPipe_, bloomBlurPipe_, bloomCompPipe_;
  rhi::BufferId bloomCbThr_, bloomCbH_, bloomCbV_, bloomCbComp_;
  void destroyTarget(Target& t) { dev_->destroy(t.color); dev_->destroy(t.depth); dev_->destroy(t.gbuf); dev_->destroy(t.aoRaw); dev_->destroy(t.ao); dev_->destroy(t.bloomA); dev_->destroy(t.bloomB); dev_->destroy(t.out); }
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
