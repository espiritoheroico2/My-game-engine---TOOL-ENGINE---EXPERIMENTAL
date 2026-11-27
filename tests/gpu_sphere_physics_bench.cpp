// PROTOTIPO: fisica de esferas inteiramente em compute (RHI/Diligent, Vulkan offscreen). So mede viabilidade e custo.
// NAO substitui o Newtoon e nao toca em engine/physics. Ver docs/planos/26_FISICA_GPU_PROTOTIPO.md.
//
// Passo (por quadro de 1/60 s), tudo em compute, uma thread por esfera (ou por celula):
//   1. integrar     : prev = pos; vel.y -= g*dt; pos += vel*dt; chao y=0 e paredes da caixa (clamp).
//   2. limpar grade : cellCount[c] = 0
//   3. contar       : cell = f(pos); slot = InterlockedAdd(cellCount[cell], 1)
//   4. prefixo      : cellStart = soma exclusiva de cellCount (1 grupo de 1024 threads, cada uma com um trecho)
//   5. espalhar     : cellList[cellStart[cell] + slot] = i
//   6. solver x N   : Jacobi de posicao (le PosA, escreve PosB, ping-pong), pares nas 27 celulas vizinhas
//   7. velocidade   : vel = (pos - prev)/dt (PBD), amortecimento e atrito de chao simples
// Grade uniforme: tamanho da celula = diametro maximo das esferas (aqui todas tem o mesmo raio, D = 2R).
// Determinismo: as correcoes de cada esfera sao acumuladas em ponto fixo (int), entao a ordem dos vizinhos na celula
// (que depende da ordem dos atomicos) nao altera o resultado bit a bit.
// Uso: tool_gpu_sphere_physics_bench [vulkan|d3d12]
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
  u32 n = 0;          // numero de esferas
  u32 nx = 0, nz = 0; // esferas por camada
  u32 layers = 0;
  u32 dx = 0, dy = 0, dz = 0; // dimensoes da grade (celulas)
  u32 ncells = 0;
  float spacing = 1.05f;   // espaco inicial entre centros (> diametro: elas caem e se assentam)
  float offsetY = 2.0f;    // altura da base da pilha acima do chao
  float margin = 4.0f;     // folga lateral da caixa
};

constexpr float kRadius = 0.5f;
constexpr float kDiam = 1.0f;     // = tamanho da celula (diametro maximo)
constexpr float kDt = 1.0f / 60.f;
constexpr float kGravity = 9.81f;
constexpr u32 kIters = 4;         // iteracoes de Jacobi por passo
constexpr float kRelax = 0.5f;    // fator de relaxamento
constexpr u32 kSteps = 300;
constexpr u32 kRuns = 3;

Scene makeScene(u32 nx, u32 nz, u32 layers) {
  Scene s;
  s.nx = nx; s.nz = nz; s.layers = layers; s.n = nx * nz * layers;
  s.dx = static_cast<u32>(std::ceil(nx * s.spacing + 2.f * s.margin));
  s.dz = static_cast<u32>(std::ceil(nz * s.spacing + 2.f * s.margin));
  s.dy = static_cast<u32>(std::ceil(s.offsetY + layers * s.spacing + 8.f));
  s.ncells = s.dx * s.dy * s.dz;
  return s;
}

// ---------------- Shaders (HLSL) ----------------
// Cabecalho gerado com as constantes do cenario (evita cbuffer: tudo e constante de compilacao).
std::string makeHeader(const Scene& s) {
  char b[1024];
  std::snprintf(b, sizeof b,
    "static const uint N = %uu;\nstatic const int DX = %d;\nstatic const int DY = %d;\nstatic const int DZ = %d;\n"
    "static const uint NCELLS = %uu;\nstatic const float R = %.6f;\nstatic const float D = %.6f;\n"
    "static const float DT = %.9f;\nstatic const float G = %.6f;\nstatic const float RELAX = %.6f;\n"
    "static const float FIX = 65536.0;\nstatic const float3 DOM = float3(%d.0, %d.0, %d.0);\n",
    s.n, static_cast<int>(s.dx), static_cast<int>(s.dy), static_cast<int>(s.dz), s.ncells, kRadius, kDiam, kDt, kGravity, kRelax,
    static_cast<int>(s.dx), static_cast<int>(s.dy), static_cast<int>(s.dz));
  return b;
}

const char* kCommon = R"(
// Celula da esfera (tamanho da celula = D = 1.0, dominio comeca em 0).
int3 cellCoord(float3 p) {
  int3 c = int3(floor(p / D));
  return clamp(c, int3(0, 0, 0), int3(DX - 1, DY - 1, DZ - 1));
}
uint cellIndex(int3 c) { return (uint(c.z) * uint(DY) + uint(c.y)) * uint(DX) + uint(c.x); }
// Chao em y=0 e paredes da caixa: mantem o centro a pelo menos R de cada limite.
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

// Prefixo exclusivo: 1 grupo de 1024 threads; cada uma soma um trecho contiguo, scan nos totais, depois escreve o trecho.
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
  uint run = gs[tid] - sum; // exclusivo do trecho
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

// Jacobi de posicao: cada esfera soma (em ponto fixo, independente da ordem) a correcao de cada vizinho sobreposto.
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
  if (l > 0.25 * D) dp *= (0.25 * D / l); // limita a correcao por iteracao (estabilidade)
  PosOut[i] = float4(clampDomain(p + dp), 0);
}
)";

// Velocidade PBD: v = (x - prev)/dt; amortecimento leve; atrito simples no chao; limite de velocidade.
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
  if (p.y <= R + 1e-3) { v.y = max(v.y, 0.0); v.xz *= 0.9; } // chao: sem velocidade para baixo, atrito tangencial
  float sp = length(v);
  if (sp > 30.0) v *= 30.0 / sp;
  Vel[i] = float4(v, 0);
}
)";

// ---------------- Estado de GPU de um cenario ----------------
struct Kernel { rhi::PipelineId pso; };

struct GpuSim {
  rhi::IDevice* dev = nullptr;
  Scene sc;
  rhi::BufferId posA, posB, vel, prev, cellCount, cellStart, cellOf, slotIn, cellList, sync;
  Kernel kIntegrate, kClear, kCount, kScan, kScatter, kSolve, kFinish;
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
    kIntegrate.pso = makePso("integrate", ::kIntegrate, {"Pos", "Vel", "Prev"});
    kClear.pso = makePso("clear", ::kClear, {"CellCount"});
    kCount.pso = makePso("count", ::kCount, {"Pos", "CellCount", "CellOf", "SlotIn"});
    kScan.pso = makePso("scan", ::kScan, {"CellCount", "CellStart"});
    kScatter.pso = makePso("scatter", ::kScatter, {"CellOf", "SlotIn", "CellStart", "CellList"});
    kSolve.pso = makePso("solve", ::kSolve, {"PosIn", "PosOut", "CellCount", "CellStart", "CellList"});
    kFinish.pso = makePso("finish", ::kFinish, {"Pos", "Vel", "Prev"});
    const bool ok = !kIntegrate.pso.isNull() && !kClear.pso.isNull() && !kCount.pso.isNull() && !kScan.pso.isNull() &&
                    !kScatter.pso.isNull() && !kSolve.pso.isNull() && !kFinish.pso.isNull();
    if (!ok) return false;
    auto mk = [&](size_t count, u32 stride) { return dev->createBuffer({count * stride, rhi::BufferUsage::StorageRW, false, stride}, nullptr); };
    posA = mk(s.n, 16); posB = mk(s.n, 16); vel = mk(s.n, 16); prev = mk(s.n, 16);
    cellCount = mk(s.ncells, 4); cellStart = mk(s.ncells, 4); cellOf = mk(s.n, 4); slotIn = mk(s.n, 4); cellList = mk(s.n, 4);
    sync = mk(4, 4);
    return !posA.isNull() && !posB.isNull() && !vel.isNull() && !prev.isNull() && !cellCount.isNull() && !cellStart.isNull() &&
           !cellOf.isNull() && !slotIn.isNull() && !cellList.isNull() && !sync.isNull();
  }

  // Estado inicial: pilha em grade com pequeno jitter deterministico; velocidade zero.
  void reset(std::vector<float>& hostPos) {
    hostPos.assign(static_cast<size_t>(sc.n) * 4, 0.f);
    std::vector<float> zero(static_cast<size_t>(sc.n) * 4, 0.f);
    for (u32 i = 0; i < sc.n; ++i) {
      const u32 x = i % sc.nx, z = (i / sc.nx) % sc.nz, y = i / (sc.nx * sc.nz);
      u32 h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
      const float jx = (static_cast<float>(h & 255u) / 255.f - 0.5f) * 0.04f;
      const float jz = (static_cast<float>((h >> 8) & 255u) / 255.f - 0.5f) * 0.04f;
      hostPos[i * 4 + 0] = sc.margin + (x + 0.5f) * sc.spacing + jx;
      hostPos[i * 4 + 1] = sc.offsetY + kRadius + y * sc.spacing;
      hostPos[i * 4 + 2] = sc.margin + (z + 0.5f) * sc.spacing + jz;
    }
    dev->updateBuffer(posA, hostPos.data(), hostPos.size() * 4);
    dev->updateBuffer(vel, zero.data(), zero.size() * 4);
    dev->updateBuffer(prev, zero.data(), zero.size() * 4);
  }

  // Grava os dispatches de um passo (varios passos podem ir na mesma lista). Termina com o resultado em posA.
  void recordStep(rhi::ICommandList& cl) {
    cl.setPipeline(kIntegrate.pso); cl.setStorageRW(0, posA); cl.setStorageRW(1, vel); cl.setStorageRW(2, prev); cl.dispatch(groupsN, 1, 1);
    cl.setPipeline(kClear.pso); cl.setStorageRW(0, cellCount); cl.dispatch(groupsC, 1, 1);
    cl.setPipeline(kCount.pso); cl.setStorageRW(0, posA); cl.setStorageRW(1, cellCount); cl.setStorageRW(2, cellOf); cl.setStorageRW(3, slotIn); cl.dispatch(groupsN, 1, 1);
    cl.setPipeline(kScan.pso); cl.setStorageRW(0, cellCount); cl.setStorageRW(1, cellStart); cl.dispatch(1, 1, 1);
    cl.setPipeline(kScatter.pso); cl.setStorageRW(0, cellOf); cl.setStorageRW(1, slotIn); cl.setStorageRW(2, cellStart); cl.setStorageRW(3, cellList); cl.dispatch(groupsN, 1, 1);
    rhi::BufferId in = posA, out = posB;
    for (u32 it = 0; it < kIters; ++it) {
      cl.setPipeline(kSolve.pso);
      cl.setStorageRW(0, in); cl.setStorageRW(1, out); cl.setStorageRW(2, cellCount); cl.setStorageRW(3, cellStart); cl.setStorageRW(4, cellList);
      cl.dispatch(groupsN, 1, 1);
      std::swap(in, out);
    }
    // kIters par: o resultado esta em posA (in == posA).
    cl.setPipeline(kFinish.pso); cl.setStorageRW(0, posA); cl.setStorageRW(1, vel); cl.setStorageRW(2, prev); cl.dispatch(groupsN, 1, 1);
  }

  // Espera a GPU terminar tudo (readBuffer de um buffer minusculo faz WaitForIdle).
  void waitGpu() { std::vector<u8> t; dev->readBuffer(sync, t); }
};

// ---------------- Estatisticas / validacao ----------------
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())));
  return v[k];
}

uint64_t fnv(const std::vector<u8>& a, const std::vector<u8>& b) {
  uint64_t h = 1469598103934665603ull;
  for (u8 c : a) { h ^= c; h *= 1099511628211ull; }
  for (u8 c : b) { h ^= c; h *= 1099511628211ull; }
  return h;
}

struct Result {
  std::vector<double> stepMs; // por passo, sincronizado
  double pipelinedPerStep = 0; // 300 passos enfileirados e uma unica espera
  uint64_t hash = 0;
  std::vector<float> pos, vel;
};

bool readState(GpuSim& g, Result& r) {
  std::vector<u8> pb, vb;
  if (!g.dev->readBuffer(g.posA, pb) || !g.dev->readBuffer(g.vel, vb)) return false;
  const size_t cnt = static_cast<size_t>(g.sc.n) * 4;
  if (pb.size() < cnt * 4 || vb.size() < cnt * 4) return false;
  r.pos.resize(cnt); r.vel.resize(cnt);
  std::memcpy(r.pos.data(), pb.data(), cnt * 4);
  std::memcpy(r.vel.data(), vb.data(), cnt * 4);
  r.hash = fnv(pb, vb);
  return true;
}

// Execucao com espera por passo (mede ms por passo incluindo sincronizacao).
bool runSynced(GpuSim& g, Result& r) {
  std::vector<float> host;
  g.reset(host);
  g.waitGpu();
  r.stepMs.clear();
  for (u32 s = 0; s < kSteps; ++s) {
    const auto t0 = Clock::now();
    auto cl = g.dev->beginCommands();
    g.recordStep(*cl);
    g.dev->submit(*cl);
    g.waitGpu();
    r.stepMs.push_back(msSince(t0));
  }
  return readState(g, r);
}

// Execucao enfileirada: 300 passos sem esperar entre eles, uma unica espera no fim (tempo medio por passo).
bool runPipelined(GpuSim& g, Result& r) {
  std::vector<float> host;
  g.reset(host);
  g.waitGpu();
  const auto t0 = Clock::now();
  for (u32 s = 0; s < kSteps; ++s) {
    auto cl = g.dev->beginCommands();
    g.recordStep(*cl);
    g.dev->submit(*cl);
  }
  g.waitGpu();
  r.pipelinedPerStep = msSince(t0) / kSteps;
  return readState(g, r);
}

// Sobrecarga da sincronizacao sozinha (readBuffer do buffer pequeno, sem trabalho): descontavel dos ms por passo.
double syncBaselineMs(GpuSim& g) {
  std::vector<double> v;
  for (u32 i = 0; i < 100; ++i) {
    const auto t0 = Clock::now();
    auto cl = g.dev->beginCommands();
    g.dev->submit(*cl);
    g.waitGpu();
    v.push_back(msSince(t0));
  }
  return percentile(v, 0.5);
}

// Metricas de qualidade/validacao do estado final (CPU, grade hash simples).
struct Quality {
  bool finite = true; float minY = 1e30f, maxY = -1e30f, meanY = 0; double meanSpeed = 0; float maxSpeed = 0;
  double meanOverlap = 0; float maxOverlap = 0; u32 overlapping = 0; // penetracao em fracao do diametro
};

Quality evaluate(const Scene& sc, const std::vector<float>& pos, const std::vector<float>& vel) {
  Quality q; double sy = 0, ss = 0;
  for (u32 i = 0; i < sc.n; ++i) {
    for (int k = 0; k < 3; ++k) if (!std::isfinite(pos[i * 4 + k]) || !std::isfinite(vel[i * 4 + k])) q.finite = false;
    const float y = pos[i * 4 + 1];
    q.minY = std::min(q.minY, y); q.maxY = std::max(q.maxY, y); sy += y;
    const float sp = std::sqrt(vel[i * 4] * vel[i * 4] + vel[i * 4 + 1] * vel[i * 4 + 1] + vel[i * 4 + 2] * vel[i * 4 + 2]);
    ss += sp; q.maxSpeed = std::max(q.maxSpeed, sp);
  }
  q.meanY = static_cast<float>(sy / sc.n); q.meanSpeed = ss / sc.n;
  if (!q.finite) return q;
  // Contagem de sobreposicao por grade CPU (so para informar a qualidade do empilhamento).
  const size_t nc = sc.ncells;
  std::vector<u32> start(nc + 1, 0), cell(sc.n), list(sc.n);
  auto cidx = [&](u32 i) {
    int cx = std::clamp(static_cast<int>(std::floor(pos[i * 4])), 0, static_cast<int>(sc.dx) - 1);
    int cy = std::clamp(static_cast<int>(std::floor(pos[i * 4 + 1])), 0, static_cast<int>(sc.dy) - 1);
    int cz = std::clamp(static_cast<int>(std::floor(pos[i * 4 + 2])), 0, static_cast<int>(sc.dz) - 1);
    return (static_cast<size_t>(cz) * sc.dy + cy) * sc.dx + cx;
  };
  for (u32 i = 0; i < sc.n; ++i) { cell[i] = static_cast<u32>(cidx(i)); ++start[cell[i] + 1]; }
  for (size_t c = 0; c < nc; ++c) start[c + 1] += start[c];
  std::vector<u32> cur(start.begin(), start.end() - 1);
  for (u32 i = 0; i < sc.n; ++i) list[cur[cell[i]]++] = i;
  double sumO = 0; u64 pairs = 0;
  for (u32 i = 0; i < sc.n; ++i) {
    const int cx = static_cast<int>(cell[i] % sc.dx), cy = static_cast<int>((cell[i] / sc.dx) % sc.dy), cz = static_cast<int>(cell[i] / (sc.dx * sc.dy));
    bool any = false;
    for (int oz = -1; oz <= 1; ++oz) for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
      const int x = cx + ox, y = cy + oy, z = cz + oz;
      if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(sc.dx) || y >= static_cast<int>(sc.dy) || z >= static_cast<int>(sc.dz)) continue;
      const size_t c = (static_cast<size_t>(z) * sc.dy + y) * sc.dx + x;
      for (u32 k = start[c]; k < start[c + 1]; ++k) {
        const u32 j = list[k]; if (j <= i) continue;
        const float dx = pos[i * 4] - pos[j * 4], dy = pos[i * 4 + 1] - pos[j * 4 + 1], dz = pos[i * 4 + 2] - pos[j * 4 + 2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= kDiam * kDiam) continue;
        const float pen = (kDiam - std::sqrt(d2)) / kDiam;
        sumO += pen; ++pairs; q.maxOverlap = std::max(q.maxOverlap, pen);
        if (pen > 0.1f) any = true;
      }
    }
    if (any) ++q.overlapping;
  }
  q.meanOverlap = pairs ? sumO / static_cast<double>(pairs) : 0;
  return q;
}

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
  std::printf("config: raio=%.2f celula=diametro=%.2f dt=1/60 passos=%u iteracoes=%u relax=%.2f execucoes=%u\n", kRadius, kDiam, kSteps, kIters, kRelax, kRuns);

  const Scene scenes[2] = {makeScene(25, 25, 16), makeScene(50, 50, 40)};
  for (const Scene& sc : scenes) {
    std::printf("\n=== %u esferas (%ux%ux%u camadas) grade %ux%ux%u = %u celulas ===\n", sc.n, sc.nx, sc.nz, sc.layers, sc.dx, sc.dy, sc.dz, sc.ncells);
    GpuSim g;
    if (!g.init(dev.get(), sc)) { check(false, "pipelines/buffers criados"); continue; }
    check(true, "pipelines/buffers criados");
    const double base = syncBaselineMs(g);
    std::printf("sobrecarga de sincronizacao (submit vazio + readBuffer): %.3f ms\n", base);

    std::vector<Result> synced(kRuns), piped(kRuns);
    bool ok = true;
    for (u32 r = 0; r < kRuns; ++r) ok = runSynced(g, synced[r]) && ok;
    for (u32 r = 0; r < kRuns; ++r) ok = runPipelined(g, piped[r]) && ok;
    check(ok, "execucoes concluidas e estado lido de volta");
    if (!ok) continue;

    std::vector<double> all, medians, p95s;
    for (u32 r = 0; r < kRuns; ++r) {
      const double med = percentile(synced[r].stepMs, 0.5), p95 = percentile(synced[r].stepMs, 0.95);
      double mean = 0; for (double v : synced[r].stepMs) mean += v; mean /= synced[r].stepMs.size();
      std::printf("  execucao %u (sincronizada): media=%.3f mediana=%.3f p95=%.3f ms/passo | enfileirada: %.3f ms/passo\n", r + 1, mean, med, p95, piped[r].pipelinedPerStep);
      medians.push_back(med); p95s.push_back(p95);
      all.insert(all.end(), synced[r].stepMs.begin(), synced[r].stepMs.end());
    }
    std::printf("RESUMO %u esferas: mediana=%.3f ms/passo (das 3 execucoes: %.3f), p95=%.3f ms/passo (das 3: %.3f), enfileirado=%.3f ms/passo\n",
                sc.n, percentile(all, 0.5), percentile(medians, 0.5), percentile(all, 0.95), percentile(p95s, 0.5),
                percentile({piped[0].pipelinedPerStep, piped[1].pipelinedPerStep, piped[2].pipelinedPerStep}, 0.5));

    // Validacoes
    const Result& r0 = synced[0];
    const Quality q = evaluate(sc, r0.pos, r0.vel);
    const float yMax0 = sc.offsetY + kRadius + (sc.layers - 1) * sc.spacing;
    std::printf("estado final: y min=%.4f max=%.3f media=%.3f (inicial max=%.3f) | vel media=%.4f max=%.3f | sobreposicao: media=%.4f max=%.3f do diametro, esferas com >10%%: %u (%.1f%%)\n",
                q.minY, q.maxY, q.meanY, yMax0, q.meanSpeed, q.maxSpeed, q.meanOverlap, q.maxOverlap, q.overlapping, 100.0 * q.overlapping / sc.n);
    check(q.finite, "(b) sem NaN/inf em posicao e velocidade");
    check(q.minY >= kRadius - 1e-3f, "(a) nenhuma esfera atravessa o chao (y >= raio - tolerancia)");
    check(q.finite && q.maxY <= yMax0 + 0.01f && q.meanY < yMax0 && q.maxSpeed <= 30.01f, "(b) altura/velocidade nao explodem (maxY <= inicial, vel <= limite)");
    bool det = true;
    for (u32 r = 1; r < kRuns; ++r) det = det && synced[r].hash == synced[0].hash;
    for (u32 r = 0; r < kRuns; ++r) det = det && piped[r].hash == synced[0].hash;
    std::printf("hashes: sinc=%016llx %016llx %016llx | enfil=%016llx %016llx %016llx\n", static_cast<unsigned long long>(synced[0].hash),
                static_cast<unsigned long long>(synced[1].hash), static_cast<unsigned long long>(synced[2].hash), static_cast<unsigned long long>(piped[0].hash),
                static_cast<unsigned long long>(piped[1].hash), static_cast<unsigned long long>(piped[2].hash));
    check(det, "(c) deterministico: estado final identico bit a bit em todas as execucoes");
  }
  std::printf(failures == 0 ? "\nRESULTADO: OK\n" : "\nRESULTADO: %d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
