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
// Narrowphase (esfera-esfera sobre a lista de pares) + correcao de posicao (Jacobi, ponto fixo) + chao/paredes por clamp.
// pos: float4 por esfera, alterada no lugar. Tempos de narrowphase (acumulo dos contatos) e solver (aplicar correcoes) separados.
struct CpuSolveTime { double narrow = 0, solve = 0; };

CpuSolveTime solveCpu(const Scene& sc, std::vector<float>& pos, const u32* pairs, u32 count, std::vector<int32_t>& acc) {
  CpuSolveTime t;
  const float dmin2 = kDiam * kDiam;
  for (u32 it = 0; it < kIters; ++it) {
    auto t0 = Clock::now();
    std::fill(acc.begin(), acc.end(), 0);
    for (u32 k = 0; k < count; ++k) {
      const u32 i = pairs[k * 2], j = pairs[k * 2 + 1];
      const float dx = pos[i * 4] - pos[j * 4], dy = pos[i * 4 + 1] - pos[j * 4 + 1], dz = pos[i * 4 + 2] - pos[j * 4 + 2];
      const float d2 = dx * dx + dy * dy + dz * dz;
      if (d2 >= dmin2) continue;
      const float dist = std::sqrt(d2);
      float nx = 1.f, ny = 0.f, nz = 0.f; // i<j sempre na lista
      if (dist > 1e-5f) { nx = dx / dist; ny = dy / dist; nz = dz / dist; }
      const float m = (kDiam - dist) * 0.5f * kRelax * kFix;
      const int32_t cx = static_cast<int32_t>(std::lround(nx * m)), cy = static_cast<int32_t>(std::lround(ny * m)), cz = static_cast<int32_t>(std::lround(nz * m));
      acc[i * 3] += cx; acc[i * 3 + 1] += cy; acc[i * 3 + 2] += cz;
      acc[j * 3] -= cx; acc[j * 3 + 1] -= cy; acc[j * 3 + 2] -= cz;
    }
    t.narrow += msSince(t0);
    t0 = Clock::now();
    const float lo = kRadius;
    const float hx = static_cast<float>(sc.dx) - kRadius, hy = static_cast<float>(sc.dy) - kRadius, hz = static_cast<float>(sc.dz) - kRadius;
    for (u32 i = 0; i < sc.n; ++i) {
      float px = static_cast<float>(acc[i * 3]) / kFix, py = static_cast<float>(acc[i * 3 + 1]) / kFix, pz = static_cast<float>(acc[i * 3 + 2]) / kFix;
      const float l = std::sqrt(px * px + py * py + pz * pz);
      if (l > 0.25f * kDiam) { const float s = 0.25f * kDiam / l; px *= s; py *= s; pz *= s; }
      pos[i * 4] = std::clamp(pos[i * 4] + px, lo, hx);
      pos[i * 4 + 1] = std::clamp(pos[i * 4 + 1] + py, lo, hy); // chao: y >= R (narrowphase esfera-chao por clamp)
      pos[i * 4 + 2] = std::clamp(pos[i * 4 + 2] + pz, lo, hz);
    }
    t.solve += msSince(t0);
  }
  return t;
}

// ---------------- Variante A: CPU pura ----------------
struct CpuState { std::vector<float> pos, vel, prev; std::vector<u32> cellStart, cellOf, cellList, pairs; std::vector<int32_t> acc; };

inline int cellIdxCpu(const Scene& sc, const float* p, int& cx, int& cy, int& cz) {
  cx = std::clamp(static_cast<int>(std::floor(p[0] / kDiam)), 0, static_cast<int>(sc.dx) - 1);
  cy = std::clamp(static_cast<int>(std::floor(p[1] / kDiam)), 0, static_cast<int>(sc.dy) - 1);
  cz = std::clamp(static_cast<int>(std::floor(p[2] / kDiam)), 0, static_cast<int>(sc.dz) - 1);
  return (cz * static_cast<int>(sc.dy) + cy) * static_cast<int>(sc.dx) + cx;
}

// Grade por ordenacao por contagem + lista de pares (mesmo criterio da GPU: i<j, d < corte).
void buildGridPairsCpu(const Scene& sc, CpuState& s) {
  std::fill(s.cellStart.begin(), s.cellStart.end(), 0u);
  for (u32 i = 0; i < sc.n; ++i) { int a, b, c; s.cellOf[i] = static_cast<u32>(cellIdxCpu(sc, &s.pos[i * 4], a, b, c)); ++s.cellStart[s.cellOf[i] + 1]; }
  for (size_t c = 0; c < sc.ncells; ++c) s.cellStart[c + 1] += s.cellStart[c];
  std::vector<u32>& cur = s.cellList; // reutiliza: cellList = lista ordenada; cursor em vetor temporario
  static std::vector<u32> cursor;
  cursor.assign(s.cellStart.begin(), s.cellStart.end() - 1);
  for (u32 i = 0; i < sc.n; ++i) cur[cursor[s.cellOf[i]]++] = i;
  s.pairs.clear();
  const float cut2 = kPairCut * kDiam * kPairCut * kDiam;
  for (u32 i = 0; i < sc.n; ++i) {
    int cx, cy, cz; cellIdxCpu(sc, &s.pos[i * 4], cx, cy, cz);
    for (int oz = -1; oz <= 1; ++oz) for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
      const int x = cx + ox, y = cy + oy, z = cz + oz;
      if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(sc.dx) || y >= static_cast<int>(sc.dy) || z >= static_cast<int>(sc.dz)) continue;
      const size_t c = (static_cast<size_t>(z) * sc.dy + y) * sc.dx + x;
      for (u32 k = s.cellStart[c]; k < s.cellStart[c + 1]; ++k) {
        const u32 j = cur[k]; if (j <= i) continue;
        const float dx = s.pos[i * 4] - s.pos[j * 4], dy = s.pos[i * 4 + 1] - s.pos[j * 4 + 1], dz = s.pos[i * 4 + 2] - s.pos[j * 4 + 2];
        if (dx * dx + dy * dy + dz * dz >= cut2) continue;
        s.pairs.push_back(i); s.pairs.push_back(j);
      }
    }
  }
}

// ---------------- Estatisticas por fase ----------------
struct Phases {
  std::vector<double> gpu, down, narrow, solve, up, total, cpuGrid;
  double bytesDown = 0, bytesUp = 0, pairsUsed = 0;
  bool overflow = false;
  std::vector<float> finalPos;
  uint64_t hash = 0;
};
void pool(std::vector<double>& dst, const std::vector<double>& src) { dst.insert(dst.end(), src.begin(), src.end()); }

bool runCpu(const Scene& sc, Phases& ph) {
  CpuState s;
  initialPositions(sc, s.pos);
  s.vel.assign(s.pos.size(), 0.f); s.prev = s.pos;
  s.cellStart.assign(sc.ncells + 1, 0); s.cellOf.assign(sc.n, 0); s.cellList.assign(sc.n, 0); s.acc.assign(static_cast<size_t>(sc.n) * 3, 0);
  const float hx = static_cast<float>(sc.dx) - kRadius, hy = static_cast<float>(sc.dy) - kRadius, hz = static_cast<float>(sc.dz) - kRadius;
  for (u32 st = 0; st < kSteps; ++st) {
    const auto t0 = Clock::now();
    // integrar
    for (u32 i = 0; i < sc.n; ++i) {
      for (int k = 0; k < 3; ++k) s.prev[i * 4 + k] = s.pos[i * 4 + k];
      s.vel[i * 4 + 1] -= kGravity * kDt;
      s.pos[i * 4] = std::clamp(s.pos[i * 4] + s.vel[i * 4] * kDt, kRadius, hx);
      s.pos[i * 4 + 1] = std::clamp(s.pos[i * 4 + 1] + s.vel[i * 4 + 1] * kDt, kRadius, hy);
      s.pos[i * 4 + 2] = std::clamp(s.pos[i * 4 + 2] + s.vel[i * 4 + 2] * kDt, kRadius, hz);
    }
    auto tg = Clock::now();
    buildGridPairsCpu(sc, s);
    const double gridMs = msSince(tg);
    const u32 cnt = static_cast<u32>(s.pairs.size() / 2);
    const CpuSolveTime t = solveCpu(sc, s.pos, s.pairs.data(), cnt, s.acc);
    // velocidade PBD
    for (u32 i = 0; i < sc.n; ++i) {
      float vx = (s.pos[i * 4] - s.prev[i * 4]) / kDt, vy = (s.pos[i * 4 + 1] - s.prev[i * 4 + 1]) / kDt, vz = (s.pos[i * 4 + 2] - s.prev[i * 4 + 2]) / kDt;
      vx *= 0.995f; vy *= 0.995f; vz *= 0.995f;
      if (s.pos[i * 4 + 1] <= kRadius + 1e-3f) { vy = std::max(vy, 0.f); vx *= 0.9f; vz *= 0.9f; }
      const float sp = std::sqrt(vx * vx + vy * vy + vz * vz);
      if (sp > 30.f) { const float f = 30.f / sp; vx *= f; vy *= f; vz *= f; }
      s.vel[i * 4] = vx; s.vel[i * 4 + 1] = vy; s.vel[i * 4 + 2] = vz;
    }
    ph.total.push_back(msSince(t0)); ph.cpuGrid.push_back(gridMs); ph.narrow.push_back(t.narrow); ph.solve.push_back(t.solve);
    ph.pairsUsed = cnt;
  }
  ph.finalPos = s.pos; ph.hash = fnvFloats(s.pos);
  return true;
}

// ---------------- Variante B: GPU pura ----------------
bool runGpu(GpuSim& g, Phases& ph) {
  std::vector<float> host; initialPositions(g.sc, host);
  g.reset(host, false); g.waitGpu();
  for (u32 st = 0; st < kSteps; ++st) {
    const auto t0 = Clock::now();
    auto cl = g.dev->beginCommands();
    g.recordFull(*cl);
    g.dev->submit(*cl);
    g.waitGpu();
    ph.total.push_back(msSince(t0));
  }
  std::vector<u8> pb;
  if (!g.dev->readBuffer(g.posA, pb) || pb.size() < static_cast<size_t>(g.sc.n) * 16) return false;
  ph.finalPos.resize(static_cast<size_t>(g.sc.n) * 4);
  std::memcpy(ph.finalPos.data(), pb.data(), ph.finalPos.size() * 4);
  ph.hash = fnvFloats(ph.finalPos);
  return true;
}

// ---------------- Variante C: hibrida ----------------
bool runHybrid(GpuSim& g, Phases& ph, double syncBase) {
  const Scene& sc = g.sc;
  std::vector<float> pos; initialPositions(sc, pos);
  g.reset(pos, true); g.waitGpu();
  std::vector<int32_t> acc(static_cast<size_t>(sc.n) * 3, 0);
  std::vector<u8> cb, pb, qb;
  for (u32 st = 0; st < kSteps; ++st) {
    const auto t0 = Clock::now();
    // 1. GPU: velocidade, integracao, grade, pares. Espera = readBuffer do contador (WaitForIdle + 4 bytes).
    auto cl = g.dev->beginCommands();
    g.recordHybrid(*cl);
    g.dev->submit(*cl);
    if (!g.dev->readBuffer(g.pairCount, cb) || cb.size() < 4) return false;
    const double tGpu = msSince(t0);
    // 2. GPU->CPU: posicoes e pares (readBuffer sincrono le o buffer inteiro).
    auto t1 = Clock::now();
    if (!g.dev->readBuffer(g.posA, pb) || pb.size() < static_cast<size_t>(sc.n) * 16) return false;
    std::memcpy(pos.data(), pb.data(), static_cast<size_t>(sc.n) * 16);
    if (!g.dev->readBuffer(g.pairs, qb)) return false;
    const double tDown = msSince(t1);
    u32 cnt; std::memcpy(&cnt, cb.data(), 4);
    if (cnt > sc.cap) { ph.overflow = true; cnt = sc.cap; }
    // 3. CPU: narrowphase + solver.
    const CpuSolveTime t = solveCpu(sc, pos, reinterpret_cast<const u32*>(qb.data()), cnt, acc);
    // 4. CPU->GPU: posicoes corrigidas (mais a espera pela copia, descontada a sincronizacao fixa).
    auto t2 = Clock::now();
    g.dev->updateBuffer(g.posA, pos.data(), static_cast<size_t>(sc.n) * 16);
    g.waitGpu();
    const double tUp = std::max(0.0, msSince(t2) - syncBase);
    ph.gpu.push_back(tGpu); ph.down.push_back(tDown); ph.narrow.push_back(t.narrow); ph.solve.push_back(t.solve); ph.up.push_back(tUp);
    ph.total.push_back(msSince(t0));
    ph.pairsUsed = cnt;
  }
  ph.bytesDown = 4.0 + static_cast<double>(sc.n) * 16 + static_cast<double>(sc.cap) * 8; // o que o readBuffer realmente copia
  ph.bytesUp = static_cast<double>(sc.n) * 16;
  ph.finalPos = pos; ph.hash = fnvFloats(pos);
  return true;
}

// ---------------- Validacao ----------------
struct Quality { bool finite = true; float minY = 1e30f, maxY = -1e30f; double meanOverlap = 0; float maxOverlap = 0; u32 overlapping = 0; };

Quality evaluate(const Scene& sc, const std::vector<float>& pos) {
  Quality q;
  for (u32 i = 0; i < sc.n; ++i) {
    for (int k = 0; k < 3; ++k) if (!std::isfinite(pos[i * 4 + k])) q.finite = false;
    q.minY = std::min(q.minY, pos[i * 4 + 1]); q.maxY = std::max(q.maxY, pos[i * 4 + 1]);
  }
  if (!q.finite) return q;
  CpuState s; s.pos = pos;
  s.cellStart.assign(sc.ncells + 1, 0); s.cellOf.assign(sc.n, 0); s.cellList.assign(sc.n, 0);
  buildGridPairsCpu(sc, s);
  std::vector<char> bad(sc.n, 0); double sum = 0; u64 np = 0;
  for (size_t k = 0; k + 1 < s.pairs.size(); k += 2) {
    const u32 i = s.pairs[k], j = s.pairs[k + 1];
    const float dx = pos[i * 4] - pos[j * 4], dy = pos[i * 4 + 1] - pos[j * 4 + 1], dz = pos[i * 4 + 2] - pos[j * 4 + 2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 >= kDiam * kDiam) continue;
    const float pen = (kDiam - std::sqrt(d2)) / kDiam;
    sum += pen; ++np; q.maxOverlap = std::max(q.maxOverlap, pen);
    if (pen > 0.1f) bad[i] = bad[j] = 1;
  }
  q.meanOverlap = np ? sum / static_cast<double>(np) : 0;
  for (char b : bad) q.overlapping += b;
  return q;
}

void reportQuality(const Scene& sc, const char* tag, const Phases* runs) {
  const Quality q = evaluate(sc, runs[0].finalPos);
  std::printf("  [%s] y min=%.4f max=%.3f | sobreposicao media=%.4f max=%.3f, esferas >10%%: %u (%.1f%%)\n", tag, q.minY, q.maxY, q.meanOverlap, q.maxOverlap, q.overlapping, 100.0 * q.overlapping / sc.n);
  char m[160];
  std::snprintf(m, sizeof m, "%s (b) sem NaN/inf", tag); check(q.finite, m);
  std::snprintf(m, sizeof m, "%s (a) nenhuma esfera atravessa o chao", tag); check(q.minY >= kRadius - 1e-3f, m);
  bool det = true;
  for (u32 r = 1; r < kRuns; ++r) det = det && runs[r].hash == runs[0].hash;
  std::printf("  [%s] hashes %016llx %016llx %016llx\n", tag, static_cast<unsigned long long>(runs[0].hash), static_cast<unsigned long long>(runs[1].hash), static_cast<unsigned long long>(runs[2].hash));
  std::snprintf(m, sizeof m, "%s (c) deterministico entre as 3 execucoes", tag); check(det, m);
}

double med(const std::vector<double>& v) { return percentile(v, 0.5); }
double p95(const std::vector<double>& v) { return percentile(v, 0.95); }

} // namespace

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  rhi::DeviceInfo info; info.width = 64; info.height = 64;
  if (which == "vulkan") info.backend = rhi::Backend::Vulkan;
  else if (which == "d3d12") info.backend = rhi::Backend::D3D12;
  else { std::fprintf(stderr, "backend nao suportado: %s\n", which.c_str()); return 2; }
  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar o dispositivo: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s compute=%d\n", rhi::toString(dev->backend()), dev->deviceName().c_str(), dev->supportsCompute() ? 1 : 0);
  if (!dev->supportsCompute()) return 1;
  std::printf("config: passos=%u iteracoes=%u relax=%.2f corte de par=%.2f D execucoes=%u (mediana/p95 sobre os %u passos das execucoes)\n", kSteps, kIters, kRelax, kPairCut, kRuns, kSteps * kRuns);

  const Scene scenes[2] = {makeScene(25, 25, 16), makeScene(50, 50, 40)};
  for (const Scene& sc : scenes) {
    std::printf("\n=== %u esferas, grade %ux%ux%u = %u celulas, capacidade de pares %u ===\n", sc.n, sc.dx, sc.dy, sc.dz, sc.ncells, sc.cap);
    GpuSim g;
    if (!g.init(dev.get(), sc)) { check(false, "pipelines/buffers criados"); continue; }
    check(true, "pipelines/buffers criados");
    std::vector<double> sb;
    for (u32 i = 0; i < 50; ++i) { const auto t0 = Clock::now(); g.waitGpu(); sb.push_back(msSince(t0)); }
    const double syncBase = med(sb);
    std::printf("sincronizacao vazia (readBuffer 16 B): %.3f ms\n", syncBase);

    Phases A[kRuns], B[kRuns], C[kRuns];
    bool ok = true;
    for (u32 r = 0; r < kRuns; ++r) ok = runCpu(sc, A[r]) && ok;
    for (u32 r = 0; r < kRuns; ++r) ok = runGpu(g, B[r]) && ok;
    for (u32 r = 0; r < kRuns; ++r) ok = runHybrid(g, C[r], syncBase) && ok;
    check(ok, "execucoes concluidas");
    if (!ok) continue;
    Phases a, b, c;
    for (u32 r = 0; r < kRuns; ++r) {
      pool(a.total, A[r].total); pool(a.cpuGrid, A[r].cpuGrid); pool(a.narrow, A[r].narrow); pool(a.solve, A[r].solve);
      pool(b.total, B[r].total);
      pool(c.total, C[r].total); pool(c.gpu, C[r].gpu); pool(c.down, C[r].down); pool(c.narrow, C[r].narrow); pool(c.solve, C[r].solve); pool(c.up, C[r].up);
    }
    std::printf("A CPU pura : total mediana=%.3f p95=%.3f ms | integ+grade+pares=%.3f, narrowphase=%.3f, solver=%.3f | pares=%.0f\n",
                med(a.total), p95(a.total), med(a.cpuGrid), med(a.narrow), med(a.solve), A[0].pairsUsed);
    std::printf("B GPU pura : total mediana=%.3f p95=%.3f ms (sincronizado)\n", med(b.total), p95(b.total));
    std::printf("C Hibrida  : total mediana=%.3f p95=%.3f ms\n", med(c.total), p95(c.total));
    std::printf("   GPU=%.3f/%.3f | GPU->CPU=%.3f/%.3f (%.2f MB copiados, %.2f MB uteis) | narrowphase=%.3f/%.3f | solver=%.3f/%.3f | CPU->GPU=%.3f/%.3f (%.2f MB)  [mediana/p95 ms]\n",
                med(c.gpu), p95(c.gpu), med(c.down), p95(c.down), C[0].bytesDown / 1e6, (4.0 + sc.n * 16.0 + C[0].pairsUsed * 8.0) / 1e6,
                med(c.narrow), p95(c.narrow), med(c.solve), p95(c.solve), med(c.up), p95(c.up), C[0].bytesUp / 1e6);
    std::printf("   pares usados=%.0f de %u; overflow=%d\n", C[0].pairsUsed, sc.cap, C[0].overflow ? 1 : 0);
    check(!C[0].overflow && !C[1].overflow && !C[2].overflow, "buffer de pares nao estourou");
    const double g_ = med(c.gpu), d_ = med(c.down), cp = med(c.narrow) + med(c.solve), u_ = med(c.up);
    std::printf("   projecao (nao medida) readback assincrono: 1 quadro ~ %.3f ms (max(GPU+up, down+CPU)); 2 quadros ~ %.3f ms (max(GPU, down+up, CPU))\n",
                std::max(g_ + u_, d_ + cp), std::max({g_, d_ + u_, cp}));
    reportQuality(sc, "A", A); reportQuality(sc, "B", B); reportQuality(sc, "C", C);
  }
  std::printf(failures == 0 ? "\nRESULTADO: OK\n" : "\nRESULTADO: %d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
