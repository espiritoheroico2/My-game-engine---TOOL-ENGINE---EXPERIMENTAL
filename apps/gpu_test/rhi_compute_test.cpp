// Teste com GPU real do RHI: compute shader, buffers escritos pela GPU (StorageRW) e draw indireto.
// Teste 1: compute escreve out[i] = in[i]*2+1 e o resultado e lido de volta (readBuffer).
// Teste 2: compute conta itens visiveis e grava DrawIndexedIndirectArgs; os args sao conferidos e o
//          drawIndexedIndirect desenha num alvo offscreen (confere um pixel).
// Uso: rhi_compute_test [vulkan|d3d12|d3d11]   (padrao: vulkan, offscreen sem janela)
#include "tool/rhi/diligent_device.hpp"
#include "tool/math/math.hpp"
#include "tool/render/frustum_cull.hpp"
#include "tool/render/instance_batch.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tool;

namespace {

int failures = 0;

// Registra uma verificacao; imprime e conta falhas.
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "[ok]  " : "[FALHA]", what);
  if (!ok) ++failures;
}

// Compute 1: out[i] = in[i]*2+1 (grupos de 64 threads).
const char* kCs1 = R"(
StructuredBuffer<uint> In;
RWStructuredBuffer<uint> Out;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Out[id.x] = In[id.x] * 2u + 1u; }
)";

// Compute 2: uma thread conta os flags != 0 e grava os 5 campos de DrawIndexedIndirectArgs (RWByteAddressBuffer).
const char* kCs2 = R"(
StructuredBuffer<uint> Flags;
RWByteAddressBuffer Args;
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint n = 0;
  for (uint i = 0; i < 100; ++i) n += (Flags[i] != 0u) ? 1u : 0u;
  Args.Store(0, 3u);   // indexCount
  Args.Store(4, n);    // instanceCount (calculado na GPU)
  Args.Store(8, 0u);   // firstIndex
  Args.Store(12, 0u);  // baseVertex
  Args.Store(16, 0u);  // firstInstance
}
)";

const char* kVs = R"(
struct VSOut { float4 pos : SV_POSITION; };
VSOut main(float2 p : ATTRIB0) { VSOut o; o.pos = float4(p, 0.5, 1.0); return o; }
)";
const char* kPs = R"(
float4 main(float4 pos : SV_POSITION) : SV_TARGET { return float4(1.0, 0.0, 0.0, 1.0); }
)";

} // namespace

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  rhi::DeviceInfo info;
  info.width = 64; info.height = 64;
  if (which == "vulkan") info.backend = rhi::Backend::Vulkan;
  else if (which == "d3d12") info.backend = rhi::Backend::D3D12;
  else { std::fprintf(stderr, "backend nao suportado por este teste: %s\n", which.c_str()); return 2; }

  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar o dispositivo: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s compute=%d indirect=%d\n", rhi::toString(dev->backend()), dev->deviceName().c_str(),
              dev->supportsCompute() ? 1 : 0, dev->supportsIndirect() ? 1 : 0);
  check(dev->supportsCompute() && dev->supportsIndirect(), "device suporta compute e indirect");
  if (!dev->supportsCompute()) return 1;

  // ---------------- Teste 1: compute simples ----------------
  constexpr u32 kN = 256;
  std::vector<u32> in(kN);
  for (u32 i = 0; i < kN; ++i) in[i] = i * 3 + 7;
  const rhi::BufferId inBuf = dev->createBuffer({kN * 4, rhi::BufferUsage::Storage, false, 4}, in.data());
  const rhi::BufferId outBuf = dev->createBuffer({kN * 4, rhi::BufferUsage::StorageRW, false, 4}, nullptr);
  check(!inBuf.isNull() && !outBuf.isNull(), "buffers Storage e StorageRW criados");

  rhi::ShaderDesc sd; sd.lang = rhi::ShaderLang::HLSL; sd.stage = rhi::ShaderStage::Compute; sd.entry = "main"; sd.name = "cs1"; sd.source = kCs1;
  const rhi::ShaderId cs1 = dev->createShader(sd);
  rhi::PipelineDesc pd; pd.cs = cs1; pd.name = "cs1"; pd.storageNames = {"In"}; pd.storageRWNames = {"Out"};
  const rhi::PipelineId pso1 = cs1.isNull() ? rhi::PipelineId{} : dev->createPipeline(pd);
  check(!pso1.isNull(), "pipeline de compute 1 criado");
  if (pso1.isNull()) return 1;
  {
    auto cl = dev->beginCommands();
    cl->setPipeline(pso1);
    cl->setStorage(0, inBuf);
    cl->setStorageRW(0, outBuf);
    cl->dispatch(kN / 64, 1, 1);
    dev->submit(*cl);
  }
  std::vector<u8> raw;
  check(dev->readBuffer(outBuf, raw) && raw.size() == kN * 4, "readBuffer do resultado");
  bool all = raw.size() == kN * 4;
  u32 bad = 0;
  if (all) for (u32 i = 0; i < kN; ++i) { u32 v; std::memcpy(&v, raw.data() + i * 4, 4); if (v != in[i] * 2 + 1) { if (bad++ < 3) std::printf("  out[%u]=%u esperado %u\n", i, v, in[i] * 2 + 1); } }
  check(all && bad == 0, "out[i] == in[i]*2+1 para 256 elementos");

  // ---------------- Teste 2: compute grava args, drawIndexedIndirect ----------------
  std::vector<u32> flags(100, 0);
  u32 expected = 0;
  for (u32 i = 0; i < 100; i += 3) { flags[i] = 1; ++expected; } // 34 itens "visiveis"
  const rhi::BufferId flagBuf = dev->createBuffer({100 * 4, rhi::BufferUsage::Storage, false, 4}, flags.data());
  const rhi::BufferId argsBuf = dev->createBuffer({sizeof(rhi::DrawIndexedIndirectArgs), rhi::BufferUsage::Indirect, false, 0}, nullptr);
  check(!flagBuf.isNull() && !argsBuf.isNull(), "buffers Storage e Indirect criados");

  sd.name = "cs2"; sd.source = kCs2;
  const rhi::ShaderId cs2 = dev->createShader(sd);
  rhi::PipelineDesc pd2; pd2.cs = cs2; pd2.name = "cs2"; pd2.storageNames = {"Flags"}; pd2.storageRWNames = {"Args"};
  const rhi::PipelineId pso2 = cs2.isNull() ? rhi::PipelineId{} : dev->createPipeline(pd2);
  check(!pso2.isNull(), "pipeline de compute 2 criado");
  if (pso2.isNull()) return 1;

  // Geometria: triangulo grande que cobre o alvo todo.
  const float verts[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
  const u32 idx[] = {0, 1, 2};
  const rhi::BufferId vb = dev->createBuffer({sizeof(verts), rhi::BufferUsage::Vertex, false, 0}, verts);
  const rhi::BufferId ib = dev->createBuffer({sizeof(idx), rhi::BufferUsage::Index, false, 0}, idx);
  rhi::ShaderDesc vsd; vsd.stage = rhi::ShaderStage::Vertex; vsd.name = "vs"; vsd.source = kVs;
  rhi::ShaderDesc psd; psd.stage = rhi::ShaderStage::Pixel; psd.name = "ps"; psd.source = kPs;
  rhi::PipelineDesc gp; gp.vs = dev->createShader(vsd); gp.ps = dev->createShader(psd);
  gp.depthTest = false; gp.depthWrite = false; gp.cull = rhi::CullMode::None;
  gp.color = rhi::Format::RGBA8; gp.depth = rhi::Format::D32F;
  gp.layout = {{2, 0}}; gp.vertexStride = 8; gp.name = "indirect draw";
  const rhi::PipelineId gpso = dev->createPipeline(gp);
  const rhi::TextureId target = dev->createTexture({64, 64, 1, rhi::Format::RGBA8, true}, nullptr);
  const rhi::TextureId depth = dev->createTexture({64, 64, 1, rhi::Format::D32F, true}, nullptr);
  check(!gpso.isNull() && !target.isNull() && !depth.isNull() && !vb.isNull() && !ib.isNull(), "pipeline grafico e alvos criados");
  if (gpso.isNull() || target.isNull()) return 1;

  {
    auto cl = dev->beginCommands();
    cl->setPipeline(pso2);
    cl->setStorage(0, flagBuf);
    cl->setStorageRW(0, argsBuf);
    cl->dispatch(1, 1, 1);
    // Mesma lista: o draw usa os argumentos que a GPU acabou de gravar (barreira UAV -> INDIRECT_ARGUMENT).
    cl->begin(target, depth, 0.f, 0.f, 0.f, 1.f);
    cl->setPipeline(gpso);
    cl->setVertexBuffer(vb);
    cl->setIndexBuffer(ib);
    cl->drawIndexedIndirect(argsBuf, 0);
    cl->end();
    dev->submit(*cl);
  }
  std::vector<u8> argsRaw;
  rhi::DrawIndexedIndirectArgs args{};
  const bool readOk = dev->readBuffer(argsBuf, argsRaw) && argsRaw.size() >= sizeof(args);
  check(readOk, "readBuffer dos args");
  if (readOk) std::memcpy(&args, argsRaw.data(), sizeof(args));
  std::printf("  args: indexCount=%u instanceCount=%u firstIndex=%u (esperado instanceCount=%u)\n", args.indexCount, args.instanceCount, args.firstIndex, expected);
  check(readOk && args.indexCount == 3 && args.instanceCount == expected && args.firstIndex == 0 && args.baseVertex == 0 && args.firstInstance == 0,
        "compute gravou DrawIndexedIndirectArgs esperados");

  std::vector<u8> img; u32 w = 0, h = 0;
  const bool texOk = dev->readTexture(target, img, w, h) && w == 64 && h == 64;
  check(texOk, "readTexture do alvo");
  if (texOk) {
    const u8* p = img.data() + (static_cast<size_t>(32) * w + 32) * 4;
    std::printf("  pixel(32,32)=%u,%u,%u,%u\n", p[0], p[1], p[2], p[3]);
    check(p[0] > 200 && p[1] < 20 && p[2] < 20, "drawIndexedIndirect pintou o pixel central de vermelho");
  }

  // ---------------- Teste 3: frustum_cull.hlsl (culling de frustum no compute) x frustum da CPU ----------------
  // 2 lotes de 1500 instancias com posicoes/escalas pseudo-aleatorias: o lote 0 tem esfera local valida, o 1 tem bounds
  // invalidos (raio < 0: nunca culla). Confere instanceCount por lote e o CONJUNTO de instancias compactadas na faixa do lote.
  {
    std::ifstream in(std::string(TOOL_SOURCE_DIR) + "/assets/shaders/hlsl/frustum_cull.hlsl", std::ios::binary);
    const std::string cullSrc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(!cullSrc.empty(), "frustum_cull.hlsl lido");
    struct CullBatchT { u32 first, count, indexCount, argsIndex; f32 sphere[4]; };
    constexpr u32 kPer = 1500, kTotal = kPer * 2;
    std::vector<render::InstanceGPU> src(kTotal);
    const Mat4 vp = Mat4::perspective(1.0f, 16.f / 9.f, 0.1f, 80.f) * Mat4::lookAt({0.f, 5.f, 20.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const render::Frustum fr = render::frustumFromViewProj(vp.m, true);
    u32 seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<f32>(seed >> 8) / 16777216.f; };
    std::vector<std::vector<u32>> expect(2);
    const render::SphereBounds sph{{0.5f, 0.f, 0.f}, 1.f, true};
    for (u32 i = 0; i < kTotal; ++i) {
      const f32 s = 0.5f + 1.5f * rnd();
      const Mat4 w = Mat4::translation({-60.f + 120.f * rnd(), -30.f + 60.f * rnd(), -60.f + 120.f * rnd()}) * Mat4::scale({s, s * (0.5f + rnd()), s});
      std::memset(&src[i], 0, sizeof src[i]);
      std::memcpy(src[i].world, w.m, sizeof src[i].world);
      src[i].color[0] = static_cast<f32>(i);   // identifica a instancia
      const u32 b = i / kPer;
      const bool vis = b == 1 ? true : render::sphereInFrustum(fr, render::transformSphere(w.m, sph));
      if (vis) expect[b].push_back(i);
    }
    CullBatchT cb[2] = {{0, kPer, 36, 0, {0.5f, 0.f, 0.f, 1.f}}, {kPer, kPer, 36, 1, {0.f, 0.f, 0.f, -1.f}}};
    float planes[6][4];
    for (int p = 0; p < 6; ++p) { planes[p][0] = fr.p[p].n[0]; planes[p][1] = fr.p[p].n[1]; planes[p][2] = fr.p[p].n[2]; planes[p][3] = fr.p[p].d; }
    rhi::DrawIndexedIndirectArgs a0[2] = {{36, 0, 0, 0, 0}, {36, 0, 0, 0, 0}};
    const rhi::BufferId srcBuf = dev->createBuffer({sizeof(render::InstanceGPU) * kTotal, rhi::BufferUsage::Storage, false, 160}, src.data());
    const rhi::BufferId dstBuf = dev->createBuffer({sizeof(render::InstanceGPU) * kTotal, rhi::BufferUsage::StorageRW, false, 160}, nullptr);
    const rhi::BufferId cbBuf = dev->createBuffer({sizeof(cb), rhi::BufferUsage::Storage, false, 32}, cb);
    const rhi::BufferId planeBuf = dev->createBuffer({sizeof(planes), rhi::BufferUsage::Uniform, true}, nullptr);
    const rhi::BufferId argBuf = dev->createBuffer({sizeof(a0), rhi::BufferUsage::Indirect, false, 0}, a0);
    check(!srcBuf.isNull() && !dstBuf.isNull() && !cbBuf.isNull() && !planeBuf.isNull() && !argBuf.isNull(), "buffers do culling criados");
    dev->updateBuffer(planeBuf, planes, sizeof planes);
    rhi::ShaderDesc cd; cd.stage = rhi::ShaderStage::Compute; cd.entry = "CullInstancesCS"; cd.name = "frustum_cull"; cd.source = cullSrc;
    const rhi::ShaderId ccs = dev->createShader(cd);
    rhi::PipelineDesc cp; cp.cs = ccs; cp.name = "frustum_cull";
    cp.uniformNames = {"CullCB"}; cp.storageNames = {"CullBatches", "SrcInstances"}; cp.storageRWNames = {"DstInstances", "DrawArgs"};
    const rhi::PipelineId cpso = ccs.isNull() ? rhi::PipelineId{} : dev->createPipeline(cp);
    check(!cpso.isNull(), "pipeline frustum_cull criado");
    if (!cpso.isNull()) {
      auto cl = dev->beginCommands();
      cl->setPipeline(cpso);
      cl->setUniform(0, planeBuf);
      cl->setStorage(0, cbBuf);
      cl->setStorage(1, srcBuf);
      cl->setStorageRW(0, dstBuf);
      cl->setStorageRW(1, argBuf);
      cl->dispatch((kPer + 63) / 64, 2, 1);
      dev->submit(*cl);
      std::vector<u8> ar, dr;
      const bool ok = dev->readBuffer(argBuf, ar) && dev->readBuffer(dstBuf, dr) && ar.size() >= sizeof(a0) && dr.size() >= sizeof(render::InstanceGPU) * kTotal;
      check(ok, "readBuffer de args e buffer compactado");
      if (ok) {
        rhi::DrawIndexedIndirectArgs ga[2];
        std::memcpy(ga, ar.data(), sizeof ga);
        for (u32 b = 0; b < 2; ++b) {
          std::printf("  lote %u: GPU visiveis=%u, CPU visiveis=%zu, indexCount=%u\n", b, ga[b].instanceCount, expect[b].size(), ga[b].indexCount);
          check(ga[b].instanceCount == expect[b].size() && ga[b].indexCount == 36 && ga[b].firstIndex == 0 && ga[b].firstInstance == 0, b == 0 ? "lote 0: contagem GPU == CPU" : "lote 1 (bounds invalidos): todos visiveis");
          std::vector<u32> got;
          const render::InstanceGPU* d = reinterpret_cast<const render::InstanceGPU*>(dr.data());
          for (u32 k = 0; k < ga[b].instanceCount && k < kPer; ++k) got.push_back(static_cast<u32>(d[b * kPer + k].color[0]));
          std::sort(got.begin(), got.end());
          check(got == expect[b], b == 0 ? "lote 0: conjunto compactado == visiveis da CPU" : "lote 1: conjunto compactado == todas");
        }
        check(!expect[0].empty() && expect[0].size() < kPer, "cena de teste mistura visiveis e culladas");
      }
    }
  }

  std::printf(failures == 0 ? "RESULTADO: OK\n" : "RESULTADO: %d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
