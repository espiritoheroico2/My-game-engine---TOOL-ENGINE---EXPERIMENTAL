// Teste de oclusao na GPU contra a piramide HiZ (segundo estagio do culling dos opacos; o primeiro e o frustum_cull.hlsl).
//
// Entrada : instancias que JA passaram no frustum (buffer compactado do estagio 1) e os args desse estagio (instanceCount por lote).
// Saida   : as que tambem passam na oclusao, compactadas na mesma faixa do lote, e os args do desenho indireto final.
// Um dispatch cobre todos os lotes: grupo (x, y) -> y = lote, x*64 + tid = instancia dentro do lote.
//
// TESTE (por objeto): a esfera envolvente vira um retangulo de tela (cubo alinhado a vista que contem a esfera, projetado nas
// faces proxima e distante); o nivel do HiZ e o menor em que o retangulo cabe em 1 texel por eixo (ate 2x2 texels amostrados);
// o objeto so e ESCONDIDO se o ponto mais proximo da esfera (profundidade do centro - raio) esta atras do maximo do HiZ no retangulo.
//
// CONSERVADOR (na duvida, VISIVEL; nunca esconde algo visivel):
//  - bounds/matriz invalidos, esfera que toca o plano near ou a camera, retangulo fora da tela: visivel;
//  - retangulo grande demais para o nivel mais alto (mais de 4 texels por eixo): visivel;
//  - pixel sem oclusor vale um valor enorme no HiZ (maximo): qualquer buraco deixa o objeto visivel;
//  - o retangulo ganha 1 pixel de folga e a comparacao tem uma tolerancia relativa (o proprio objeto, cuja superficie esta
//    dentro da esfera, nunca se esconde sozinho).
#include "occlusion_common.hlsl"

struct CullBatch {         // 32 B por lote (igual a frustum_cull.hlsl)
  uint first;
  uint count;
  uint indexCount;
  uint argsIndex;
  float4 sphere;
};

cbuffer OccCB {
  float4x4 View;           // view da camera (mesma do FrameBuffer)
  float4 Proj;             // x = P00, y = P11, z = near, w = tolerancia relativa da comparacao
  float4 Screen;           // x = largura, y = altura (pixels do nivel 0), z = niveis da piramide
};

StructuredBuffer<CullBatch> CullBatches;
StructuredBuffer<InstanceData> SrcInstances;    // compactado do estagio 1
RWByteAddressBuffer ArgsIn;                     // args do estagio 1 (so lido: instanceCount em +4)
RWStructuredBuffer<InstanceData> DstInstances;  // compactado final
RWByteAddressBuffer DrawArgs;                   // args finais (a CPU grava indexCount e instanceCount = 0 antes)

Texture2D<float> Hiz0; Texture2D<float> Hiz1; Texture2D<float> Hiz2; Texture2D<float> Hiz3;
Texture2D<float> Hiz4; Texture2D<float> Hiz5; Texture2D<float> Hiz6; Texture2D<float> Hiz7;
Texture2D<float> Hiz8; Texture2D<float> Hiz9; Texture2D<float> Hiz10; Texture2D<float> Hiz11;

float HizLoad(uint l, int2 p) {
  int3 q = int3(p, 0);
  if (l == 0u) return Hiz0.Load(q);
  if (l == 1u) return Hiz1.Load(q);
  if (l == 2u) return Hiz2.Load(q);
  if (l == 3u) return Hiz3.Load(q);
  if (l == 4u) return Hiz4.Load(q);
  if (l == 5u) return Hiz5.Load(q);
  if (l == 6u) return Hiz6.Load(q);
  if (l == 7u) return Hiz7.Load(q);
  if (l == 8u) return Hiz8.Load(q);
  if (l == 9u) return Hiz9.Load(q);
  if (l == 10u) return Hiz10.Load(q);
  return Hiz11.Load(q);
}

// Verdadeiro se o objeto PODE aparecer. Falso so quando esta inteiro atras dos oclusores.
bool OcclusionVisible(float4x4 w, float4 sphere) {
  float3 c; float r;
  if (!WorldSphere(w, sphere, c, r)) return true;
  float3 cv = mul(View, float4(c, 1.0)).xyz;
  float d = -cv.z;                         // profundidade de view do centro (convencao do HiZ: linear, maior = mais longe)
  float nearD = d - r;                     // ponto mais proximo da esfera
  if (nearD <= Proj.z * 1.05) return true; // toca/passa do plano near ou envolve a camera: visivel
  float df = d + r;
  // x/profundidade e monotono na profundidade e linear em x: os extremos saem dos 4 cantos do cubo (2 x 2).
  float nx0 = min(Proj.x * (cv.x - r) / nearD, Proj.x * (cv.x - r) / df);
  float nx1 = max(Proj.x * (cv.x + r) / nearD, Proj.x * (cv.x + r) / df);
  float ny0 = min(Proj.y * (cv.y - r) / nearD, Proj.y * (cv.y - r) / df);
  float ny1 = max(Proj.y * (cv.y + r) / nearD, Proj.y * (cv.y + r) / df);
  float W = Screen.x, H = Screen.y;
  // Pixels (origem no canto superior esquerdo, como o alvo) com 1 pixel de folga.
  float u0 = (nx0 * 0.5 + 0.5) * W - 1.0, u1 = (nx1 * 0.5 + 0.5) * W + 1.0;
  float v0 = (0.5 - ny1 * 0.5) * H - 1.0, v1 = (0.5 - ny0 * 0.5) * H + 1.0;
  if (!(u1 >= 0.0 && v1 >= 0.0 && u0 <= W && v0 <= H)) return true;   // fora da tela: o frustum ja decide
  if (!all(isfinite(float4(u0, u1, v0, v1)))) return true;
  int iw = int(W), ih = int(H);
  int x0 = clamp(int(floor(u0)), 0, iw - 1), x1 = clamp(int(floor(u1)), 0, iw - 1);
  int y0 = clamp(int(floor(v0)), 0, ih - 1), y1 = clamp(int(floor(v1)), 0, ih - 1);
  int size = max(x1 - x0, y1 - y0) + 1;
  uint level = size <= 1 ? 0u : uint(ceil(log2(float(size))));
  level = min(level, uint(Screen.z) - 1u);
  int lw = max(1, iw >> level), lh = max(1, ih >> level);
  int tx0 = min(x0 >> level, lw - 1), tx1 = min(x1 >> level, lw - 1);
  int ty0 = min(y0 >> level, lh - 1), ty1 = min(y1 >> level, lh - 1);
  if (tx1 - tx0 > 3 || ty1 - ty0 > 3) return true;   // retangulo grande demais para o nivel mais alto: nao arrisca
  float zmax = 0.0;
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx)
      zmax = max(zmax, HizLoad(level, int2(tx, ty)));
  // Escondido so se TODO texel do retangulo tem oclusor mais perto que o ponto mais proximo da esfera (com folga relativa).
  return !(nearD > zmax * (1.0 + Proj.w) + 1e-3);
}

[numthreads(64, 1, 1)]
void OcclusionCullCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
  CullBatch b = CullBatches[gid.y];
  uint i = gid.x * 64 + tid.x;
  uint visibleIn = ArgsIn.Load(b.argsIndex * 20u + 4u);   // instancias que o frustum deixou passar neste lote
  if (i >= visibleIn) return;
  InstanceData inst = SrcInstances[b.first + i];
  if (!OcclusionVisible(inst.world, b.sphere)) return;
  uint slot = 0u;
  DrawArgs.InterlockedAdd(b.argsIndex * 20u + 4u, 1u, slot);
  DstInstances[b.first + slot] = inst;
}
