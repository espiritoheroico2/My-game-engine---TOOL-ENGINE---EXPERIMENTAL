// Passe de profundidade dos OCLUSORES (oclusao por GPU, nivel 0 da piramide HiZ).
// Desenha as instancias opacas que passaram no frustum (buffer compactado do culling) num alvo R32F com a profundidade LINEAR
// de view (-z de view = clip.w), limpo com um valor enorme ("sem oclusor"). O teste de profundidade do hardware guarda o mais proximo.
//
// Filtro de oclusor (igual ao da CPU em editor_state.hpp: kOccluderMinPx): so entra quem tem raio projetado >= params.y pixels,
// medido pela distancia do olho ao centro menos o raio. As demais instancias saem degeneradas (fora do clip) e nao desenham nada.
// Bounds invalidos nao sao oclusores. Menos oclusores so deixa mais objetos visiveis: nunca esconde algo visivel.
#include "occlusion_common.hlsl"

cbuffer FrameBuffer : register(b4) { float4x4 viewProj; float4x4 view; float4 camPos; };
StructuredBuffer<InstanceData> Instances : register(t0);
cbuffer OccBatchBuffer : register(b6) {
  uint4 batchInfo;     // x = primeira instancia do lote no buffer compactado
  float4 sphere;       // esfera local da malha (xyz centro, w raio; w < 0 = invalida)
  float4 params;       // x = 0.5 * altura * P11 (pixels por unidade a distancia 1), y = tamanho minimo em pixels, z = near
};

struct VSIn { float3 pos : ATTRIB0; float3 normal : ATTRIB1; float2 uv : ATTRIB2; uint iid : SV_InstanceID; };
struct VSOut { float4 clip : SV_Position; float depthView : TEXCOORD0; };

VSOut VSMain(VSIn i) {
  VSOut o;
  InstanceData inst = Instances[batchInfo.x + i.iid];
  float4x4 world = inst.world;
  float3 c; float r;
  bool ok = WorldSphere(world, sphere, c, r);
  float dist = ok ? length(c - camPos.xyz) - r : 0.0;
  // Alpha cutoff (empacotado em fogParams.w: forcaQ*1024 + cutoffQ): material com recorte tem buracos, nao e oclusor.
  bool cutout = (uint(inst.fogParams.w) & 1023u) != 0u;
  // px = raio * (0.5*altura) / (tan(fov/2) * dist), com 1/tan(fov/2) = P11 (mesma conta da CPU).
  bool occluder = ok && !cutout && dist > params.z && (r * params.x / dist) >= params.y;
  float4 wp = mul(world, float4(i.pos, 1.0));
  o.clip = occluder ? mul(viewProj, wp) : float4(2.0, 2.0, 2.0, 1.0);   // fora do clip: o triangulo some
  o.depthView = -mul(view, wp).z;
  return o;
}

float PSMain(VSOut i) : SV_Target { return i.depthView; }
