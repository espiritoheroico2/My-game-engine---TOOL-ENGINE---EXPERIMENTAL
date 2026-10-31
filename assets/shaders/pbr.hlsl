// ToolEngine - shader PBR basico (entradas: VSMain, PSMain). Compilado offline por tools/compile_shaders.py.
#include "lighting.hlsli"

cbuffer FrameBuffer : register(b0) {
  float4x4 viewProj;
  float3   cameraPos; float _pad0;
};
cbuffer ObjectBuffer : register(b2) {
  float4x4 world;
  float4   baseColor;
  float    metallic;
  float    roughness;
  float    receiveShadow; // 1 = este objeto recebe sombra
  float    _pad1;
};

struct VSIn  { float3 pos : ATTRIB0; float3 normal : ATTRIB1; float2 uv : ATTRIB2; };
struct VSOut { float4 clip : SV_Position; float3 worldPos : TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; };

VSOut VSMain(VSIn i) {
  VSOut o;
  float4 wp = mul(world, float4(i.pos, 1.0));
  o.worldPos = wp.xyz;
  o.normal = normalize(mul((float3x3)world, i.normal));
  o.uv = i.uv;
  o.clip = mul(viewProj, wp);
  return o;
}

float4 PSMain(VSOut i) : SV_Target {
  float3 color = ShadeAll(i.worldPos, normalize(i.normal), cameraPos, baseColor.rgb, metallic, max(roughness, 0.04), receiveShadow);
  return float4(ToneMapACES(color), baseColor.a);
}
