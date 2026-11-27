// ToolEngine - FSR 1.0 (EASU + RCAS) na GPU, como dois passes de pixel shader em tela cheia sobre o RHI.
//
// Algoritmo: AMD FidelityFX Super Resolution 1.0 (licenca MIT), portado para HLSL em assets/shaders/hlsl/fsr1.hlsl
// a partir de https://github.com/GPUOpen-Effects/FidelityFX-FSR (ffx-fsr/ffx_fsr1.h). O aviso de copyright da AMD
// fica no topo do shader.
//
// Uso: init() uma vez; run() a cada quadro com a textura renderizada em baixa resolucao (srcW x srcH) e o tamanho de
// saida (outW x outH). Devolve a textura final (EASU -> RCAS). Sem compute: usa so pixel shader e alvos de render,
// entao funciona em qualquer backend do RHI que crie pipelines graficos com texturas (Vulkan, D3D12, D3D11).
#pragma once
#include "tool/rhi/rhi.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace tool::render {

// Constantes do shader (cbuffer FsrCB do fsr1.hlsl; 16 bytes de alinhamento).
struct Fsr1CB {
  f32 con0[4];       // EASU: escala saida->entrada e deslocamento (FsrEasuCon, con0)
  f32 srcSize[2];    // tamanho da textura lida pelo passe
  f32 gamma;         // 1 = alvo sRGB (converte linear <-> perceptual dentro do shader)
  f32 rcasScale;     // 2^-stops (FsrRcasCon); 0 = sem nitidez
};

class Fsr1Pass {
public:
  Fsr1Pass() = default;
  ~Fsr1Pass() { release(); }
  Fsr1Pass(const Fsr1Pass&) = delete;
  Fsr1Pass& operator=(const Fsr1Pass&) = delete;

  // Cria shaders, pipelines e buffers. `colorFormat` e o formato das texturas de entrada/saida (o mesmo do renderer).
  // Devolve false (sem lancar) se algo falhar: o chamador deve desligar o FSR 1 e avisar uma vez.
  bool init(rhi::IDevice& dev, const std::string& hlslDir, rhi::Format colorFormat) {
    release();
    dev_ = &dev; fmt_ = colorFormat;
    std::ifstream in(hlslDir + "/fsr1.hlsl", std::ios::binary);
    if (!in) { why_ = "nao achei fsr1.hlsl em " + hlslDir; dev_ = nullptr; return false; }
    const std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    rhi::ShaderDesc vs; vs.stage = rhi::ShaderStage::Vertex; vs.entry = "VSMain"; vs.name = "fsr1.vs"; vs.source = src;
    rhi::ShaderDesc pe = vs; pe.stage = rhi::ShaderStage::Pixel; pe.entry = "EasuMain"; pe.name = "fsr1.easu";
    rhi::ShaderDesc pr = vs; pr.stage = rhi::ShaderStage::Pixel; pr.entry = "RcasMain"; pr.name = "fsr1.rcas";
    vs_ = dev.createShader(vs); easuPs_ = dev.createShader(pe); rcasPs_ = dev.createShader(pr);
    if (vs_.isNull() || easuPs_.isNull() || rcasPs_.isNull()) { why_ = "falha ao compilar fsr1.hlsl"; release(); return false; }
    // Triangulo de tela cheia (mesmo formato dos outros passes do renderer: posicao float3).
    static const float tri[9] = {-1.f, -1.f, 0.f, 3.f, -1.f, 0.f, -1.f, 3.f, 0.f};
    static const u32 idx[3] = {0, 1, 2};
    vb_ = dev.createBuffer({sizeof tri, rhi::BufferUsage::Vertex, false}, tri);
    ib_ = dev.createBuffer({sizeof idx, rhi::BufferUsage::Index, false}, idx);
    cbEasu_ = dev.createBuffer({sizeof(Fsr1CB), rhi::BufferUsage::Uniform, true}, nullptr);
    cbRcas_ = dev.createBuffer({sizeof(Fsr1CB), rhi::BufferUsage::Uniform, true}, nullptr);
    rhi::PipelineDesc pd; pd.vs = vs_; pd.ps = easuPs_; pd.name = "fsr1.easu"; pd.layout = {{3, 0}}; pd.vertexStride = 12;
    pd.uniformNames = {"FsrCB"}; pd.textureNames = {"Src"}; pd.color = colorFormat; pd.cull = rhi::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    easuPipe_ = dev.createPipeline(pd);
    rhi::PipelineDesc pr2 = pd; pr2.ps = rcasPs_; pr2.name = "fsr1.rcas";
    rcasPipe_ = dev.createPipeline(pr2);
    if (vb_.isNull() || ib_.isNull() || cbEasu_.isNull() || cbRcas_.isNull() || easuPipe_.isNull() || rcasPipe_.isNull()) {
      why_ = "falha ao criar pipelines/buffers do FSR 1"; release(); return false;
    }
    return true;
  }

  bool ready() const { return dev_ && !easuPipe_.isNull() && !rcasPipe_.isNull(); }
  const std::string& whyNot() const { return why_; }

  // Roda EASU (src -> outW x outH) e depois RCAS (nitidez, `sharpness` 0..1). Devolve a textura final ou nulo se falhar.
  // A textura devolvida pertence ao passe (reaproveitada a cada quadro, recriada se o tamanho de saida mudar).
  rhi::TextureId run(rhi::TextureId src, u32 srcW, u32 srcH, u32 outW, u32 outH, f32 sharpness) {
    if (!ready() || src.isNull() || srcW == 0 || srcH == 0 || outW == 0 || outH == 0) return kNullHandle;
    if (!ensureTargets(outW, outH)) return kNullHandle;
    const f32 gamma = fmt_ == rhi::Format::RGBA8_SRGB ? 1.f : 0.f;
    // Nitidez 0..1 -> "stops" da AMD (0 = mais nitido; 2 = bem suave). 0 desliga o RCAS (escala 0).
    const f32 s = std::clamp(sharpness, 0.f, 1.f);
    const f32 rcas = s <= 0.f ? 0.f : std::exp2(-(1.f - s) * 2.f);
    // FsrEasuCon: posicao de saida -> posicao no espaco da entrada.
    Fsr1CB e{};
    const f32 sx = static_cast<f32>(srcW) / static_cast<f32>(outW), sy = static_cast<f32>(srcH) / static_cast<f32>(outH);
    e.con0[0] = sx; e.con0[1] = sy; e.con0[2] = 0.5f * sx - 0.5f; e.con0[3] = 0.5f * sy - 0.5f;
    e.srcSize[0] = static_cast<f32>(srcW); e.srcSize[1] = static_cast<f32>(srcH); e.gamma = gamma; e.rcasScale = rcas;
    Fsr1CB r{};
    r.srcSize[0] = static_cast<f32>(outW); r.srcSize[1] = static_cast<f32>(outH); r.gamma = gamma; r.rcasScale = rcas;
    dev_->updateBuffer(cbEasu_, &e, sizeof e);
    dev_->updateBuffer(cbRcas_, &r, sizeof r);
    fullscreen(easuTex_, easuPipe_, cbEasu_, src);
    fullscreen(outTex_, rcasPipe_, cbRcas_, easuTex_);
    return outTex_;
  }

  // Textura intermediaria (saida do EASU, antes do RCAS). Usada pelo teste para comparar com o resultado final.
  rhi::TextureId easuTexture() const { return easuTex_; }

  void release() {
    if (dev_) {
      for (auto* h : {&vs_, &easuPs_, &rcasPs_, &vb_, &ib_, &cbEasu_, &cbRcas_, &easuPipe_, &rcasPipe_, &easuTex_, &outTex_})
        if (!h->isNull()) { dev_->destroy(*h); *h = kNullHandle; }
    }
    tw_ = th_ = 0; dev_ = nullptr;
  }

private:
  // Recria os alvos intermediario e final quando o tamanho de saida muda.
  bool ensureTargets(u32 w, u32 h) {
    if (!easuTex_.isNull() && !outTex_.isNull() && tw_ == w && th_ == h) return true;
    for (auto* t : {&easuTex_, &outTex_}) if (!t->isNull()) { dev_->destroy(*t); *t = kNullHandle; }
    easuTex_ = dev_->createTexture({w, h, 1, fmt_, true}, nullptr);
    outTex_ = dev_->createTexture({w, h, 1, fmt_, true}, nullptr);
    tw_ = w; th_ = h;
    return !easuTex_.isNull() && !outTex_.isNull();
  }
  // Um passe de tela cheia: lê `in` com o pipeline e grava em `target`.
  void fullscreen(rhi::TextureId target, rhi::PipelineId pipe, rhi::BufferId cb, rhi::TextureId in) {
    auto c = dev_->beginCommands();
    c->begin(target, kNullHandle, 0.f, 0.f, 0.f, 1.f);
    c->setPipeline(pipe); c->setUniform(0, cb); c->setTexture(0, in);
    c->setVertexBuffer(vb_); c->setIndexBuffer(ib_); c->drawIndexed(3, 1);
    c->end(); dev_->submit(*c);
  }

  rhi::IDevice* dev_ = nullptr;
  rhi::Format fmt_ = rhi::Format::RGBA8;
  rhi::ShaderId vs_, easuPs_, rcasPs_;
  rhi::PipelineId easuPipe_, rcasPipe_;
  rhi::BufferId vb_, ib_, cbEasu_, cbRcas_;
  rhi::TextureId easuTex_, outTex_;
  u32 tw_ = 0, th_ = 0;
  std::string why_;
};

} // namespace tool::render
