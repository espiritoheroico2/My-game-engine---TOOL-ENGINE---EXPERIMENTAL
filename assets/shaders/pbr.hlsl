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
  float4   texInfo;       // largura, altura, 1 = tem textura de albedo (AlbedoMap)
};
Texture2D AlbedoMap;     // slot 1 do pipeline (Load, sem sampler: amostragem "mais proximo" com repeticao)

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
  float3 albedo = baseColor.rgb;
  if (texInfo.z > 0.5) {
    int2 tp = int2(clamp(int(frac(i.uv.x) * texInfo.x), 0, int(texInfo.x) - 1), clamp(int(frac(i.uv.y) * texInfo.y), 0, int(texInfo.y) - 1));
    albedo *= AlbedoMap.Load(int3(tp, 0)).rgb;
  }
  float3 color = ShadeAll(i.worldPos, normalize(i.normal), cameraPos, albedo, metallic, max(roughness, 0.04), receiveShadow);
  return float4(ToneMapACES(color), baseColor.a);   // alfa so da cor do material (a textura nao mexe nele: a mascara do ceu depende disso)
}
