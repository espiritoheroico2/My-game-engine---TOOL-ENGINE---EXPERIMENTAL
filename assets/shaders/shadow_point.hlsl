// Passe de profundidade para sombras de luzes pontuais e spot (ver lightprism/Common/PointShadow.hlsli).
// Grava a DISTANCIA ao longo do eixo da face (clip.w da perspectiva), nao o z normalizado: o teste de sombra compara
// distancias, igual a SoftRenderer (CPU), e assim o viés em metros fica o mesmo nos dois caminhos.
cbuffer ShadowBuffer : register(b0) { float4x4 lightViewProj; };
struct ShadowInstance { float4x4 world; };
StructuredBuffer<ShadowInstance> Instances : register(t0);
cbuffer BatchBuffer : register(b1) { uint4 batchInfo; };
struct VSOut { float4 clip : SV_Position; float depth : TEXCOORD0; };
VSOut VSMain(float3 pos : ATTRIB0, uint iid : SV_InstanceID) {
  VSOut o;
  float4x4 world = Instances[batchInfo.x + iid].world;
  o.clip = mul(lightViewProj, mul(world, float4(pos, 1.0)));
  o.depth = o.clip.w;
  return o;
}
float PSMain(VSOut i) : SV_Target { return i.depth; }
