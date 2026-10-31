// Passe de profundidade para sombras: desenha so a posicao vista do sol e grava o z da luz (0..1) num alvo R32F.
cbuffer ShadowBuffer : register(b0) { float4x4 lightViewProj; float4x4 world; };
struct VSOut { float4 clip : SV_Position; float depth : TEXCOORD0; };
VSOut VSMain(float3 pos : ATTRIB0) {
  VSOut o;
  o.clip = mul(lightViewProj, mul(world, float4(pos, 1.0)));
  o.depth = o.clip.z;
  return o;
}
float PSMain(VSOut i) : SV_Target { return i.depth; }
