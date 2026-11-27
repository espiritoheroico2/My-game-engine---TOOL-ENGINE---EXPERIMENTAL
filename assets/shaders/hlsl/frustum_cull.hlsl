// Culling de frustum na GPU para instancias opacas (compute) + draw indireto. Fase 1: so o passe opaco principal.
//
// Um dispatch cobre TODOS os lotes do passe: grupo (x, y) -> y = indice do lote, x*64 + tid = instancia dentro do lote.
// Para cada instancia: esfera no mundo (mesma conta de tool::render::transformSphere) testada contra os 6 planos
// (mesma conta de sphereInFrustum). Se visivel: InterlockedAdd em DrawArgs.instanceCount do lote (o contador E o
// argumento do draw indireto) e copia as 160 bytes da instancia para a MESMA FAIXA do lote no buffer compactado
// (base = CullBatch.first, igual ao BatchCB.first do pbr.hlsl), na posicao [first + slot].
//
// A CPU grava DrawArgs {indexCount, 0, 0, 0, 0} de cada lote antes do dispatch (instanceCount zerado = o "clear").
// A ordem das instancias visiveis dentro do lote nao e deterministica (atomico); nao importa para o passe opaco.

struct InstanceData {      // igual a InstanceGPU (160 B) / pbr.hlsl
  float4x4 world;
  float4 baseColor;
  float metallic, roughness, receiveShadow, _pad1;
  float4 texInfo;
  float4 emissive;
  float4 fogColor;
  float4 fogParams;
};

struct CullBatch {         // 32 B por lote (StructuredBuffer, preenchido pela CPU a cada quadro)
  uint first;              // primeira instancia do lote (tanto no buffer de origem quanto no compactado)
  uint count;              // instancias do lote
  uint indexCount;         // (usado so pela CPU ao gravar os args; mantido para depuracao)
  uint argsIndex;          // indice do lote no buffer de args (offset em bytes = argsIndex * 20)
  float4 sphere;           // xyz = centro da malha (espaco local), w = raio local; w < 0 = bounds invalidos (nunca culla)
};

cbuffer CullCB {
  float4 Planes[6];        // n.xyz, d: n.p + d >= 0 dentro (mesmos planos de frustumFromViewProj)
};

StructuredBuffer<CullBatch> CullBatches;
StructuredBuffer<InstanceData> SrcInstances;
RWStructuredBuffer<InstanceData> DstInstances;
RWByteAddressBuffer DrawArgs;

// Verdadeiro se a instancia toca o frustum. Bounds/matriz invalidos contam como visiveis (como na CPU).
bool InstanceVisible(float4x4 w, float4 sphere) {
  if (sphere.w < 0.0) return true;
  if (!all(isfinite(sphere))) return true;
  // Matriz nao afim (ultima linha diferente de 0,0,0,1): a CPU devolve bounds invalidos -> visivel.
  if (abs(w[3][0]) > 1e-5 || abs(w[3][1]) > 1e-5 || abs(w[3][2]) > 1e-5 || abs(w[3][3] - 1.0) > 1e-5) return true;
  float3 c = mul(w, float4(sphere.xyz, 1.0)).xyz;
  // Raio escalado pelo maior ganho: sqrt(maiorSomaDeLinha * maiorSomaDeColuna) (conservador, igual a CPU).
  float3 r0 = abs(float3(w[0][0], w[0][1], w[0][2]));
  float3 r1 = abs(float3(w[1][0], w[1][1], w[1][2]));
  float3 r2 = abs(float3(w[2][0], w[2][1], w[2][2]));
  float maxRow = max(r0.x + r0.y + r0.z, max(r1.x + r1.y + r1.z, r2.x + r2.y + r2.z));
  float3 c0 = abs(float3(w[0][0], w[1][0], w[2][0]));
  float3 c1 = abs(float3(w[0][1], w[1][1], w[2][1]));
  float3 c2 = abs(float3(w[0][2], w[1][2], w[2][2]));
  float maxCol = max(c0.x + c0.y + c0.z, max(c1.x + c1.y + c1.z, c2.x + c2.y + c2.z));
  float radius = sphere.w * sqrt(maxRow * maxCol);
  if (!all(isfinite(c)) || !isfinite(radius)) return true;
  [unroll] for (int i = 0; i < 6; ++i) {
    float dist = dot(Planes[i].xyz, c) + Planes[i].w;
    if (dist < -radius - max(1e-5, radius * 1e-5)) return false;
  }
  return true;
}

[numthreads(64, 1, 1)]
void CullInstancesCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
  CullBatch b = CullBatches[gid.y];
  uint i = gid.x * 64 + tid.x;
  if (i >= b.count) return;
  InstanceData inst = SrcInstances[b.first + i];
  if (!InstanceVisible(inst.world, b.sphere)) return;
  uint slot = 0u;
  DrawArgs.InterlockedAdd(b.argsIndex * 20u + 4u, 1u, slot);   // args.instanceCount; devolve a posicao compactada
  DstInstances[b.first + slot] = inst;
}
