// Prepasso de SSAO: grava, por pixel, a normal em VIEW space (xyz) e a distancia linear de view (w).
// Usa os mesmos cbuffers do pbr.hlsl (FrameBuffer b4, ObjectBuffer b6) para o mesmo layout de CPU.
// Alvo RGBA16F limpo com (0,0,0,0): w = 0 significa "sem geometria" (igual a Light Prism).
cbuffer FrameBuffer : register(b4) { float4x4 viewProj; float4x4 view; };
cbuffer ObjectBuffer : register(b6) { float4x4 world; };
struct VSIn { float3 pos : ATTRIB0; float3 normal : ATTRIB1; float2 uv : ATTRIB2; };
struct VSOut { float4 clip : SV_Position; float3 nView : TEXCOORD0; float depthView : TEXCOORD1; };
VSOut VSMain(VSIn i) {
  VSOut o;
  float4 wp = mul(world, float4(i.pos, 1.0));
  float4 vp = mul(view, wp);
  o.clip = mul(viewProj, wp);
  o.nView = mul((float3x3)view, mul((float3x3)world, i.normal));
  o.depthView = -vp.z;
  return o;
}
float4 PSMain(VSOut i) : SV_Target { return float4(normalize(i.nView), i.depthView); }
