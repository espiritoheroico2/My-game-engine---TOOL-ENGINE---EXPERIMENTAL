// ToolEngine - shader PBR com a iluminacao da Light Prism (entradas: VSMain, PSMain).
// Os modulos da LightPrism estao em assets/shaders/lightprism (copia de third_party/LightPrism/shaders; Shadow.hlsli adaptado).
// Registradores (unicos por tipo): t0 AlbedoMap; t1/t2 luzes pontuais/spot; t3..t6 mapas de sombra das cascatas;
// t8/t9 clusters (LightGrid, LightIndexList); b0 Globals; b1 sol; b2 CSM; b3 ambiente (IBL SH9); b4 FrameBuffer;
// b5 ClusterGrid; b6 ObjectBuffer; b7 PointShadowCB; t10..t13 mapas de sombra de pontuais/spot.
#define LP_ENABLE_CSM
#define LP_ENABLE_POINT_SHADOWS // sombras de pontuais/spot da engine (lightprism/Common/PointShadow.hlsli)
#include "lightprism/Common/ClusterLighting.hlsli"
#include "lightprism/Common/Ibl.hlsli"

cbuffer FrameBuffer : register(b4) {
  float4x4 viewProj;
  float4x4 view;        // mundo -> camera (profundidade de view para escolher a cascata)
  float3   cameraPos; float _pad0;
};
cbuffer ObjectBuffer : register(b6) {
  float4x4 world;
  float4   baseColor;
  float    metallic;
  float    roughness;
  float    receiveShadow; // 1 = este objeto recebe sombra
  float    _pad1;
  float4   texInfo;       // largura, altura, 1 = tem textura de albedo (AlbedoMap)
};
Texture2D AlbedoMap : register(t0);  // Load, sem sampler: amostragem "mais proximo" com repeticao

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

// ACES (Narkowicz). O alvo final e sRGB: o hardware converte; por isso so o ACES aqui (como pede a LightPrism).
float3 ToneMapACESFilm(float3 x) { return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)); }

float4 PSMain(VSOut i) : SV_Target {
  float3 albedo = baseColor.rgb;
  if (texInfo.z > 0.5) {
    int2 tp = int2(clamp(int(frac(i.uv.x) * texInfo.x), 0, int(texInfo.x) - 1), clamp(int(frac(i.uv.y) * texInfo.y), 0, int(texInfo.y) - 1));
    albedo *= AlbedoMap.Load(int3(tp, 0)).rgb;
  }
  float3 N = normalize(i.normal);
  SurfaceData s;
  s.N = N;
  s.V = normalize(cameraPos - i.worldPos);
  s.albedo = albedo;
  s.metallic = metallic;
  s.roughness = max(roughness, LP_MIN_ROUGHNESS);

  // Luz ambiente: irradiancia SH9 da LightPrism (IblEvaluateIrradiance), difusa, sem especular ambiente.
  float3 color = (1.0f - metallic) * albedo * IblEvaluateIrradiance(N);
  // Luzes do sol, pontuais e spot: pelos clusters (e pelas cascatas do sol).
  float viewDepth = -mul(view, float4(i.worldPos, 1.0f)).z;
  color += EvaluateClusteredLights(s, i.worldPos, i.clip.xy, viewDepth, receiveShadow);
  return float4(ToneMapACESFilm(color), baseColor.a);   // alfa so da cor do material (a mascara do ceu depende disso)
}
