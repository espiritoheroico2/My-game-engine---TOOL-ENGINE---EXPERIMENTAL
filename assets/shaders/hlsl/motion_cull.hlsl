// Movimento + culling de frustum na GPU para a cena PERSISTENTE (experimento --gpu-motion).
//
// O buffer de instancias (SrcInstances, 160 B por objeto) e gravado UMA vez na GPU; aqui nada o reescreve por quadro.
// Cada instancia tem tambem um float4 de movimento (Motion): x = amplitude (m), y = frequencia, z = fase (rad), w = t0 (deslocamento
// de tempo, o mesmo que o MoverComp.t guarda). A posicao e a MESMA formula do Editor::tickMovers (apps/editor/ecs_test.hpp):
//   a = (T + t0) * freq + fase
//   pos = base + (amp * sin(a), 0.5 * amp * sin(2a), amp * cos(a))
// A matriz gravada ja traz a posicao BASE (mundo) na translacao; o shader soma so o deslocamento. amp == 0 = objeto parado.
//
// Depois o teste de frustum e a compactacao sao os de frustum_cull.hlsl: grupo (x, y) -> y = lote, x*64 + tid = instancia
// do lote; visivel => InterlockedAdd em DrawArgs.instanceCount e copia da instancia (JA movida) para a faixa do lote no buffer
// compactado, que o vertex shader (pbr.hlsl) le por Instances[batchInfo.x + SV_InstanceID].

struct InstanceData {      // igual a InstanceGPU (160 B) / pbr.hlsl
  float4x4 world;
  float4 baseColor;
  float metallic, roughness, receiveShadow, _pad1;
  float4 texInfo;
  float4 emissive;
  float4 fogColor;
  float4 fogParams;
};

struct CullBatch {         // 32 B por lote (gravado uma vez pela CPU)
  uint first;
  uint count;
  uint indexCount;
  uint argsIndex;
  float4 sphere;           // xyz = centro da malha (local), w = raio local; w < 0 = nunca culla
};

cbuffer MotionCullCB {
  float4 Planes[6];        // n.xyz, d: n.p + d >= 0 dentro
  float4 TimeInfo;         // x = tempo uniforme T (s)
};

StructuredBuffer<CullBatch> CullBatches;
StructuredBuffer<InstanceData> SrcInstances;
StructuredBuffer<float4> Motion;
RWStructuredBuffer<InstanceData> DstInstances;
RWByteAddressBuffer DrawArgs;

// Mesma conta de frustum_cull.hlsl (esfera no mundo contra os 6 planos).
bool InstanceVisible(float4x4 w, float4 sphere) {
  if (sphere.w < 0.0) return true;
  if (!all(isfinite(sphere))) return true;
  if (abs(w[3][0]) > 1e-5 || abs(w[3][1]) > 1e-5 || abs(w[3][2]) > 1e-5 || abs(w[3][3] - 1.0) > 1e-5) return true;
  float3 c = mul(w, float4(sphere.xyz, 1.0)).xyz;
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
void MotionCullCS(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
  CullBatch b = CullBatches[gid.y];
  uint i = gid.x * 64 + tid.x;
  if (i >= b.count) return;
  InstanceData inst = SrcInstances[b.first + i];
  float4 m = Motion[b.first + i];
  if (m.x != 0.0) {
    // Translacao fica na ultima COLUNA (a matriz da CPU e column-major na memoria: indices 12..14).
    float a = (TimeInfo.x + m.w) * m.y + m.z;
    inst.world[0][3] += m.x * sin(a);
    inst.world[1][3] += 0.5 * m.x * sin(2.0 * a);
    inst.world[2][3] += m.x * cos(a);
  }
  if (!InstanceVisible(inst.world, b.sphere)) return;
  uint slot = 0u;
  DrawArgs.InterlockedAdd(b.argsIndex * 20u + 4u, 1u, slot);
  DstInstances[b.first + slot] = inst;
}
