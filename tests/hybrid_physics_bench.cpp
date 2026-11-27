// EXPERIMENTO HIBRIDO de fisica de esferas: GPU integra e gera os pares de contato, CPU faz narrowphase + solver.
// Compara tres variantes na MESMA cena (esferas de mesmo raio caindo sobre o chao):
//   A) CPU pura  : integracao, grade, pares e solver na CPU (uma thread).
//   B) GPU pura  : tudo em compute (igual ao protótipo gpu_sphere_physics_bench).
//   C) HIBRIDA   : GPU = velocidade/integracao/grade/lista de pares; readback (sincrono) de posicoes e pares;
//                  CPU = narrowphase (esfera-esfera e esfera-chao) + correcao de posicao; upload das posicoes corrigidas.
// NAO substitui o Newtoon e nao toca em engine/physics. Ver docs/planos/27_FISICA_HIBRIDA.md.
// Uso: tool_hybrid_physics_bench [vulkan|d3d12]
#include "tool/rhi/diligent_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tool;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "[ok]  " : "[FALHA]", what);
  if (!ok) ++failures;
}

// ---------------- Parametros do cenario ----------------
struct Scene {
  u32 n = 0, nx = 0, nz = 0, layers = 0;
  u32 dx = 0, dy = 0, dz = 0, ncells = 0;
  float spacing = 1.05f, offsetY = 2.0f, margin = 4.0f;
  u32 cap = 0; // capacidade do buffer de pares (8 por esfera)
};

constexpr float kRadius = 0.5f;
constexpr float kDiam = 1.0f;       // tamanho da celula = diametro
constexpr float kPairCut = 1.1f;    // corte da lista de pares (folga de 10% sobre o diametro)
constexpr float kDt = 1.0f / 60.f;
constexpr float kGravity = 9.81f;
constexpr u32 kIters = 4;
constexpr float kRelax = 0.5f;
constexpr float kFix = 65536.f;
constexpr u32 kSteps = 300;
constexpr u32 kRuns = 3;

Scene makeScene(u32 nx, u32 nz, u32 layers) {
  Scene s;
  s.nx = nx; s.nz = nz; s.layers = layers; s.n = nx * nz * layers;
  s.dx = static_cast<u32>(std::ceil(nx * s.spacing + 2.f * s.margin));
  s.dz = static_cast<u32>(std::ceil(nz * s.spacing + 2.f * s.margin));
  s.dy = static_cast<u32>(std::ceil(s.offsetY + layers * s.spacing + 8.f));
  s.ncells = s.dx * s.dy * s.dz;
  s.cap = s.n * 8;
  return s;
}

// Estado inicial deterministico (pilha em grade com jitter), posicoes como float4.
void initialPositions(const Scene& sc, std::vector<float>& pos) {
  pos.assign(static_cast<size_t>(sc.n) * 4, 0.f);
  for (u32 i = 0; i < sc.n; ++i) {
    const u32 x = i % sc.nx, z = (i / sc.nx) % sc.nz, y = i / (sc.nx * sc.nz);
    u32 h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const float jx = (static_cast<float>(h & 255u) / 255.f - 0.5f) * 0.04f;
    const float jz = (static_cast<float>((h >> 8) & 255u) / 255.f - 0.5f) * 0.04f;
    pos[i * 4 + 0] = sc.margin + (x + 0.5f) * sc.spacing + jx;
    pos[i * 4 + 1] = sc.offsetY + kRadius + y * sc.spacing;
    pos[i * 4 + 2] = sc.margin + (z + 0.5f) * sc.spacing + jz;
  }
}

// ---------------- Shaders (HLSL) ----------------
std::string makeHeader(const Scene& s) {
  char b[1280];
  std::snprintf(b, sizeof b,
    "static const uint N = %uu;\nstatic const int DX = %d;\nstatic const int DY = %d;\nstatic const int DZ = %d;\n"
    "static const uint NCELLS = %uu;\nstatic const float R = %.6f;\nstatic const float D = %.6f;\n"
    "static const float DT = %.9f;\nstatic const float G = %.6f;\nstatic const float RELAX = %.6f;\n"
    "static const float FIX = 65536.0;\nstatic const float3 DOM = float3(%d.0, %d.0, %d.0);\n"
    "static const uint CAP = %uu;\nstatic const float PCUT = %.6f;\n",
    s.n, static_cast<int>(s.dx), static_cast<int>(s.dy), static_cast<int>(s.dz), s.ncells, kRadius, kDiam, kDt, kGravity, kRelax,
    static_cast<int>(s.dx), static_cast<int>(s.dy), static_cast<int>(s.dz), s.cap, kPairCut * kDiam);
  return b;
}

const char* kCommon = R"(
int3 cellCoord(float3 p) {
  int3 c = int3(floor(p / D));
  return clamp(c, int3(0, 0, 0), int3(DX - 1, DY - 1, DZ - 1));
}
uint cellIndex(int3 c) { return (uint(c.z) * uint(DY) + uint(c.y)) * uint(DX) + uint(c.x); }
float3 clampDomain(float3 p) { return clamp(p, float3(R, R, R), DOM - float3(R, R, R)); }
)";

const char* kIntegrate = R"(
RWStructuredBuffer<float4> Pos;
RWStructuredBuffer<float4> Vel;
RWStructuredBuffer<float4> Prev;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  float3 p = Pos[i].xyz; float3 v = Vel[i].xyz;
  Prev[i] = float4(p, 0);
  v.y -= G * DT;
  p += v * DT;
  Pos[i] = float4(clampDomain(p), 0);
  Vel[i] = float4(v, 0);
}
)";

const char* kClear = R"(
RWStructuredBuffer<uint> CellCount;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { if (id.x < NCELLS) CellCount[id.x] = 0u; }
)";

const char* kPairClear = R"(
RWStructuredBuffer<uint> PairCount;
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { PairCount[0] = 0u; }
)";

const char* kCount = R"(
RWStructuredBuffer<float4> Pos;
RWStructuredBuffer<uint> CellCount;
RWStructuredBuffer<uint> CellOf;
RWStructuredBuffer<uint> SlotIn;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  uint cell = cellIndex(cellCoord(Pos[i].xyz));
  uint slot;
  InterlockedAdd(CellCount[cell], 1u, slot);
  CellOf[i] = cell; SlotIn[i] = slot;
}
)";

const char* kScan = R"(
RWStructuredBuffer<uint> CellCount;
RWStructuredBuffer<uint> CellStart;
groupshared uint gs[1024];
[numthreads(1024, 1, 1)]
void main(uint3 tid3 : SV_GroupThreadID) {
  uint tid = tid3.x;
  uint chunk = (NCELLS + 1023u) / 1024u;
  uint b = tid * chunk; uint e = min(b + chunk, NCELLS);
  uint sum = 0u;
  for (uint c = b; c < e; ++c) sum += CellCount[c];
  gs[tid] = sum;
  GroupMemoryBarrierWithGroupSync();
  for (uint off = 1u; off < 1024u; off <<= 1u) {
    uint v = (tid >= off) ? gs[tid - off] : 0u;
    GroupMemoryBarrierWithGroupSync();
    gs[tid] += v;
    GroupMemoryBarrierWithGroupSync();
  }
  uint run = gs[tid] - sum;
  for (uint c2 = b; c2 < e; ++c2) { CellStart[c2] = run; run += CellCount[c2]; }
}
)";

const char* kScatter = R"(
RWStructuredBuffer<uint> CellOf;
RWStructuredBuffer<uint> SlotIn;
RWStructuredBuffer<uint> CellStart;
RWStructuredBuffer<uint> CellList;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  CellList[CellStart[CellOf[i]] + SlotIn[i]] = i;
}
)";

const char* kSolve = R"(
RWStructuredBuffer<float4> PosIn;
RWStructuredBuffer<float4> PosOut;
RWStructuredBuffer<uint> CellCount;
RWStructuredBuffer<uint> CellStart;
RWStructuredBuffer<uint> CellList;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  float3 p = PosIn[i].xyz;
  int3 c = cellCoord(p);
  int3 s = int3(0, 0, 0);
  for (int oz = -1; oz <= 1; ++oz)
  for (int oy = -1; oy <= 1; ++oy)
  for (int ox = -1; ox <= 1; ++ox) {
    int3 q = c + int3(ox, oy, oz);
    if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= DX || q.y >= DY || q.z >= DZ) continue;
    uint cell = cellIndex(q);
    uint st = CellStart[cell]; uint cnt = CellCount[cell];
    for (uint k = 0u; k < cnt; ++k) {
      uint j = CellList[st + k];
      if (j == i) continue;
      float3 d = p - PosIn[j].xyz;
      float d2 = dot(d, d);
      if (d2 >= D * D) continue;
      float dist = sqrt(d2);
      float3 n = (dist > 1e-5) ? d / dist : ((i < j) ? float3(1, 0, 0) : float3(-1, 0, 0));
      float3 corr = n * ((D - dist) * 0.5 * RELAX);
      s += int3(round(corr * FIX));
    }
  }
  float3 dp = float3(s) / FIX;
  float l = length(dp);
  if (l > 0.25 * D) dp *= (0.25 * D / l);
  PosOut[i] = float4(clampDomain(p + dp), 0);
}
)";

// Lista de pares (i<j, distancia < PCUT) escrita num buffer compacto por InterlockedAdd; a ordem e nao deterministica,
// mas o solver da CPU acumula em ponto fixo (soma inteira associativa), entao o resultado final nao depende da ordem.
const char* kPairs = R"(
RWStructuredBuffer<float4> Pos;
RWStructuredBuffer<uint> CellCount;
RWStructuredBuffer<uint> CellStart;
RWStructuredBuffer<uint> CellList;
RWStructuredBuffer<uint2> Pairs;
RWStructuredBuffer<uint> PairCount;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  float3 p = Pos[i].xyz;
  int3 c = cellCoord(p);
  for (int oz = -1; oz <= 1; ++oz)
  for (int oy = -1; oy <= 1; ++oy)
  for (int ox = -1; ox <= 1; ++ox) {
    int3 q = c + int3(ox, oy, oz);
    if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= DX || q.y >= DY || q.z >= DZ) continue;
    uint cell = cellIndex(q);
    uint st = CellStart[cell]; uint cnt = CellCount[cell];
    for (uint k = 0u; k < cnt; ++k) {
      uint j = CellList[st + k];
      if (j <= i) continue;
      float3 d = p - Pos[j].xyz;
      if (dot(d, d) >= PCUT * PCUT) continue;
      uint idx;
      InterlockedAdd(PairCount[0], 1u, idx);
      if (idx < CAP) Pairs[idx] = uint2(i, j);
    }
  }
}
)";

const char* kFinish = R"(
RWStructuredBuffer<float4> Pos;
RWStructuredBuffer<float4> Vel;
RWStructuredBuffer<float4> Prev;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x; if (i >= N) return;
  float3 p = Pos[i].xyz;
  float3 v = (p - Prev[i].xyz) / DT;
  v *= 0.995;
  if (p.y <= R + 1e-3) { v.y = max(v.y, 0.0); v.xz *= 0.9; }
  float sp = length(v);
  if (sp > 30.0) v *= 30.0 / sp;
  Vel[i] = float4(v, 0);
}
)";

// ---------------- Estado de GPU ----------------
struct GpuSim {
  rhi::IDevice* dev = nullptr;
  Scene sc;
  rhi::BufferId posA, posB, vel, prev, cellCount, cellStart, cellOf, slotIn, cellList, sync, pairs, pairCount;
  rhi::PipelineId pIntegrate, pClear, pCount, pScan, pScatter, pSolve, pFinish, pPairClear, pPairs;
  u32 groupsN = 0, groupsC = 0;

  rhi::PipelineId makePso(const char* name, const char* body, std::vector<std::string> rw) {
    rhi::ShaderDesc sd; sd.lang = rhi::ShaderLang::HLSL; sd.stage = rhi::ShaderStage::Compute; sd.entry = "main"; sd.name = name;
    sd.source = makeHeader(sc) + kCommon + body;
    const rhi::ShaderId cs = dev->createShader(sd);
    if (cs.isNull()) return {};
    rhi::PipelineDesc pd; pd.cs = cs; pd.name = name; pd.storageRWNames = std::move(rw);
    return dev->createPipeline(pd);
  }

  bool init(rhi::IDevice* d, const Scene& s) {
    dev = d; sc = s;
    groupsN = (s.n + 63) / 64; groupsC = (s.ncells + 63) / 64;
    pIntegrate = makePso("integrate", ::kIntegrate, {"Pos", "Vel", "Prev"});
    pClear = makePso("clear", ::kClear, {"CellCount"});
    pCount = makePso("count", ::kCount, {"Pos", "CellCount", "CellOf", "SlotIn"});
    pScan = makePso("scan", ::kScan, {"CellCount", "CellStart"});
    pScatter = makePso("scatter", ::kScatter, {"CellOf", "SlotIn", "CellStart", "CellList"});
    pSolve = makePso("solve", ::kSolve, {"PosIn", "PosOut", "CellCount", "CellStart", "CellList"});
    pFinish = makePso("finish", ::kFinish, {"Pos", "Vel", "Prev"});
    pPairClear = makePso("pairclear", ::kPairClear, {"PairCount"});
    pPairs = makePso("pairs", ::kPairs, {"Pos", "CellCount", "CellStart", "CellList", "Pairs", "PairCount"});
    if (pIntegrate.isNull() || pClear.isNull() || pCount.isNull() || pScan.isNull() || pScatter.isNull() || pSolve.isNull() ||
        pFinish.isNull() || pPairClear.isNull() || pPairs.isNull()) return false;
    auto mk = [&](size_t count, u32 stride) { return dev->createBuffer({count * stride, rhi::BufferUsage::StorageRW, false, stride}, nullptr); };
    posA = mk(s.n, 16); posB = mk(s.n, 16); vel = mk(s.n, 16); prev = mk(s.n, 16);
    cellCount = mk(s.ncells, 4); cellStart = mk(s.ncells, 4); cellOf = mk(s.n, 4); slotIn = mk(s.n, 4); cellList = mk(s.n, 4);
    sync = mk(4, 4); pairs = mk(s.cap, 8); pairCount = mk(4, 4);
    return !posA.isNull() && !posB.isNull() && !vel.isNull() && !prev.isNull() && !cellCount.isNull() && !cellStart.isNull() &&
           !cellOf.isNull() && !slotIn.isNull() && !cellList.isNull() && !sync.isNull() && !pairs.isNull() && !pairCount.isNull();
  }

  // prevIsPos: no hibrido o "finish" roda no inicio do passo seguinte, entao prev precisa comecar igual a pos (velocidade zero).
  void reset(const std::vector<float>& hostPos, bool prevIsPos) {
    std::vector<float> zero(static_cast<size_t>(sc.n) * 4, 0.f);
    dev->updateBuffer(posA, hostPos.data(), hostPos.size() * 4);
    dev->updateBuffer(vel, zero.data(), zero.size() * 4);
    dev->updateBuffer(prev, prevIsPos ? hostPos.data() : zero.data(), zero.size() * 4);
  }

  void buildGrid(rhi::ICommandList& cl) {
    cl.setPipeline(pClear); cl.setStorageRW(0, cellCount); cl.dispatch(groupsC, 1, 1);
    cl.setPipeline(pCount); cl.setStorageRW(0, posA); cl.setStorageRW(1, cellCount); cl.setStorageRW(2, cellOf); cl.setStorageRW(3, slotIn); cl.dispatch(groupsN, 1, 1);
    cl.setPipeline(pScan); cl.setStorageRW(0, cellCount); cl.setStorageRW(1, cellStart); cl.dispatch(1, 1, 1);
    cl.setPipeline(pScatter); cl.setStorageRW(0, cellOf); cl.setStorageRW(1, slotIn); cl.setStorageRW(2, cellStart); cl.setStorageRW(3, cellList); cl.dispatch(groupsN, 1, 1);
  }
  void integrate(rhi::ICommandList& cl) {
    cl.setPipeline(pIntegrate); cl.setStorageRW(0, posA); cl.setStorageRW(1, vel); cl.setStorageRW(2, prev); cl.dispatch(groupsN, 1, 1);
  }
  void finish(rhi::ICommandList& cl) {
    cl.setPipeline(pFinish); cl.setStorageRW(0, posA); cl.setStorageRW(1, vel); cl.setStorageRW(2, prev); cl.dispatch(groupsN, 1, 1);
  }

  // Variante B: passo completo na GPU. Resultado em posA.
  void recordFull(rhi::ICommandList& cl) {
    integrate(cl);
    buildGrid(cl);
    rhi::BufferId in = posA, out = posB;
    for (u32 it = 0; it < kIters; ++it) {
      cl.setPipeline(pSolve);
      cl.setStorageRW(0, in); cl.setStorageRW(1, out); cl.setStorageRW(2, cellCount); cl.setStorageRW(3, cellStart); cl.setStorageRW(4, cellList);
      cl.dispatch(groupsN, 1, 1);
      std::swap(in, out);
    }
    finish(cl);
  }

  // Variante C, lado GPU: velocidade do passo anterior (posA ja corrigida pela CPU), integracao, grade e lista de pares.
  void recordHybrid(rhi::ICommandList& cl) {
    finish(cl);
    integrate(cl);
    buildGrid(cl);
    cl.setPipeline(pPairClear); cl.setStorageRW(0, pairCount); cl.dispatch(1, 1, 1);
    cl.setPipeline(pPairs);
    cl.setStorageRW(0, posA); cl.setStorageRW(1, cellCount); cl.setStorageRW(2, cellStart); cl.setStorageRW(3, cellList);
    cl.setStorageRW(4, pairs); cl.setStorageRW(5, pairCount);
    cl.dispatch(groupsN, 1, 1);
  }

  void waitGpu() { std::vector<u8> t; dev->readBuffer(sync, t); }
};

// ---------------- Utilitarios ----------------
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())));
  return v[k];
}

uint64_t fnvFloats(const std::vector<float>& a) {
  uint64_t h = 1469598103934665603ull;
  const u8* p = reinterpret_cast<const u8*>(a.data());
  for (size_t i = 0; i < a.size() * 4; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

// ---------------- Solver da CPU (compartilhado por A e C) ----------------
float3Dummy: ;
