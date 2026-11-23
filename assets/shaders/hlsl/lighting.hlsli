// ToolEngine - iluminacao PBR (Cook-Torrance GGX) compartilhada por todos os backends.
// Espelha engine/render/include/tool/render/lighting.hpp (mesmo layout de GpuLight, mesma atenuacao).
#ifndef TOOL_LIGHTING_HLSLI
#define TOOL_LIGHTING_HLSLI

#define MAX_LIGHTS 64
static const float PI = 3.14159265359;

struct GpuLight {
  float3 position;  float range;
  float3 color;     float intensity;
  float3 direction; float spotCos;
  uint   type;      uint shadowIndex; float spotInnerCos; uint pad;
};

cbuffer LightBuffer : register(b1) {
  GpuLight lights[MAX_LIGHTS];
  float4   ambient;      // rgb; float4 evita float3 desalinhado (std140/GLSL)
  uint     lightCount;
  float4x4 lightViewProj; // sol: mundo -> clip da luz (mesma matriz do passe de sombra e do SoftRenderer)
  float4   shadowParams;  // x = bias, y = 1/resolucao, z = forca da sombra (0 = desligada), w = normal bias em metros
  float4   shadowInfo;    // x = resolucao do mapa, y = raio do filtro PCF (0, 1 ou 2)
};

// Mapa de sombra: profundidade (z da luz, 0..1) escrita pelo passe de sombra num alvo R32F. Lido com Load
// (sem sampler), e a comparacao + filtro PCF sao feitos aqui, entao nao dependem de estados de sampler por API.
Texture2D ShadowMap;

float Attenuation(float dist, float range) {
  if (dist >= range) return 0.0;
  float d2 = max(dist * dist, 1e-4);
  float w = saturate(1.0 - pow(dist / range, 4.0));
  return w * w / d2;
}

float SpotFactor(float3 lightDir, float3 fromLight, float innerCos, float outerCos) {
  float c = dot(normalize(lightDir), normalize(fromLight));
  return saturate((c - outerCos) / max(innerCos - outerCos, 1e-4));
}

float DistributionGGX(float NdotH, float roughness) {
  float a = roughness * roughness, a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float GeometrySmith(float NdotV, float NdotL, float roughness) {
  float r = roughness + 1.0, k = (r * r) / 8.0;
  float gv = NdotV / (NdotV * (1.0 - k) + k), gl = NdotL / (NdotL * (1.0 - k) + k);
  return gv * gl;
}
float3 FresnelSchlick(float cosTheta, float3 F0) { return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0); }

// Fracao de luz (0 = sombra, 1 = iluminado) no ponto P. Espelha SoftRenderer::sunVisibility().
float ShadowFactor(float3 P, float3 N, float ndl) {
  if (shadowParams.z <= 0.0) return 1.0;
  float res = shadowInfo.x;
  float4 c = mul(lightViewProj, float4(P + N * shadowParams.w, 1.0));
  float2 uv = float2(c.x * 0.5 + 0.5, 1.0 - (c.y * 0.5 + 0.5));
  if (c.z > 1.0 || c.z < 0.0 || uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) return 1.0;
  float bias = shadowParams.x * (1.0 + 2.0 * (1.0 - ndl));
  int2 ip = int2(uv * res);
  int k = (int)shadowInfo.y, last = (int)res - 1;
  float lit = 0.0, tot = 0.0;
  for (int dy = -k; dy <= k; ++dy)
    for (int dx = -k; dx <= k; ++dx) {
      int2 sp = clamp(ip + int2(dx, dy), int2(0, 0), int2(last, last));
      lit += (ShadowMap.Load(int3(sp, 0)).r >= c.z - bias) ? 1.0 : 0.0;
      tot += 1.0;
    }
  return lit / tot;
}

float3 EvaluateLight(GpuLight L, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float receive) {
  float3 toLight; float att = 1.0;
  if (L.type == 0) {                 // direcional
    toLight = -normalize(L.direction);
  } else {
    float3 d = L.position - P;
    float dist = length(d);
    toLight = d / max(dist, 1e-4);
    att = Attenuation(dist, L.range);
    if (L.type == 2) att *= SpotFactor(L.direction, -toLight, L.spotInnerCos, L.spotCos);
  }
  float NdotL = saturate(dot(N, toLight));
  if (NdotL <= 0.0 || att <= 0.0) return float3(0, 0, 0);
  float3 H = normalize(V + toLight);
  float NdotV = max(dot(N, V), 1e-4), NdotH = saturate(dot(N, H)), VdotH = saturate(dot(V, H));
  float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
  float3 F = FresnelSchlick(VdotH, F0);
  float D = DistributionGGX(NdotH, roughness), G = GeometrySmith(NdotV, NdotL, roughness);
  float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
  float3 kd = (1.0 - F) * (1.0 - metallic);
  float vis = 1.0;                  // so a luz marcada com shadowIndex = 0 (o sol) projeta sombra
  if (L.shadowIndex == 0u && receive > 0.5) vis = 1.0 - shadowParams.z * (1.0 - ShadowFactor(P, N, NdotL));
  return (kd * albedo / PI + spec) * L.color * L.intensity * att * NdotL * vis;
}

float3 ShadeAll(float3 P, float3 N, float3 camPos, float3 albedo, float metallic, float roughness, float receive) {
  float3 V = normalize(camPos - P);
  float3 c = ambient.rgb * albedo;
  for (uint i = 0; i < lightCount; ++i) c += EvaluateLight(lights[i], P, N, V, albedo, metallic, roughness, receive);
  return c;
}

float3 ToneMapACES(float3 x) { return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)); }

#endif
