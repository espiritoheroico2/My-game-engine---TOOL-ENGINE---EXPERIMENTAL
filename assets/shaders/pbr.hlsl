// ToolEngine - shader PBR com a iluminacao da Light Prism (entradas: VSMain, PSMain).
// Os modulos da LightPrism estao em assets/shaders/lightprism (copia de third_party/LightPrism/shaders; Shadow.hlsli adaptado).
// Registradores (unicos por tipo): t0 AlbedoMap; t1/t2 luzes pontuais/spot; t3..t6 mapas de sombra das cascatas;
// t8/t9 clusters (LightGrid, LightIndexList); b0 Globals; b1 sol; b2 CSM; b3 ambiente (IBL SH9); b4 FrameBuffer (so VS);
// b5 ClusterGrid; b6 ObjectBuffer (so VS); b7 PointShadowCB; t10..t13 mapas de sombra de pontuais/spot.
// LIMITE DE BUFFERS: o Vulkan conta cada cbuffer por estagio (VS e PS). Com b4 e b6 no PS, o pipeline somava 10 buffers
// dinamicos e o driver (8) recusava. Por isso o PS nao le b4/b6: o VS calcula o que ele precisa e passa por interpolacao
// (valores por objeto com nointerpolation). Assim o total e 2 (VS) + 6 (PS) = 8.
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
  float4   emissive;      // rgb = radiancia emissiva (cor x intensidade), linear; soma antes do tone mapping (bloom)
  float4   fogColor;      // rgb = cor da neblina (linear)
  float4   fogParams;     // x = inicio (m), y = fim (m), z = quantidade maxima (0 = sem neblina)
};
Texture2D AlbedoMap : register(t0);
Texture2D<float> AoMap : register(t14); // SSAO (ssao.hlsl): 1 = sem oclusao; so afeta a luz ambiente, como na Light Prism  // Load, sem sampler: amostragem "mais proximo" com repeticao

struct VSIn  { float3 pos : ATTRIB0; float3 normal : ATTRIB1; float2 uv : ATTRIB2; };
struct VSOut {
  float4 clip : SV_Position;
  float3 worldPos : TEXCOORD0;
  float3 normal : TEXCOORD1;
  float2 uv : TEXCOORD2;
  float3 toCam : TEXCOORD3;     // cameraPos - worldPos (linear no mundo: a interpolacao e exata; normalizado no PS)
  float  viewDepth : TEXCOORD4; // -view.z do ponto (tambem linear no mundo)
  nointerpolation float4 baseColor : TEXCOORD5;
  nointerpolation float4 material  : TEXCOORD6; // x = metallic, y = roughness, z = receiveShadow
  nointerpolation float4 texInfo   : TEXCOORD7;
  nointerpolation float3 emissive  : TEXCOORD8;
};

VSOut VSMain(VSIn i) {
  VSOut o;
  float4 wp = mul(world, float4(i.pos, 1.0));
  o.worldPos = wp.xyz;
  o.normal = normalize(mul((float3x3)world, i.normal));
  o.uv = i.uv;
  o.clip = mul(viewProj, wp);
  o.toCam = cameraPos - wp.xyz;
  o.viewDepth = -mul(view, wp).z;
  o.baseColor = baseColor;
  o.material = float4(metallic, roughness, receiveShadow, 0.0f);
  o.texInfo = texInfo;
  o.emissive = emissive.rgb;
  return o;
}

// ACES (Narkowicz). O alvo final e sRGB: o hardware converte; por isso so o ACES aqui (como pede a LightPrism).
float3 ToneMapACESFilm(float3 x) { return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)); }

// LUT da BRDF (split-sum) gerada pela LightPrism (Ibl::BuildBrdfLut, 32x32): x = NoV, y = rugosidade; R = A, G = B.
// Interpolacao bilinear igual a Ibl::SampleBrdfLut (feita com Load, pois o RHI da engine nao tem amostrador).
static const float LUT_SIZE = 32.0f;
float2 BrdfLutBilinear(float NoV, float rough) {
  float fx = clamp(saturate(NoV) * LUT_SIZE - 0.5f, 0.0f, LUT_SIZE - 1.0f);
  float fy = clamp(saturate(rough) * LUT_SIZE - 0.5f, 0.0f, LUT_SIZE - 1.0f);
  int x0 = int(fx), y0 = int(fy);
  int x1 = min(x0 + 1, int(LUT_SIZE) - 1), y1 = min(y0 + 1, int(LUT_SIZE) - 1);
  float tx = fx - float(x0), ty = fy - float(y0);
  float2 p00 = IblBrdfLut.Load(int3(x0, y0, 0)), p10 = IblBrdfLut.Load(int3(x1, y0, 0));
  float2 p01 = IblBrdfLut.Load(int3(x0, y1, 0)), p11 = IblBrdfLut.Load(int3(x1, y1, 0));
  return lerp(lerp(p00, p10, tx), lerp(p01, p11, tx), ty);
}

float4 PSMain(VSOut i) : SV_Target {
  float3 albedo = i.baseColor.rgb;
  if (i.texInfo.z > 0.5) {
    int2 tp = int2(clamp(int(frac(i.uv.x) * i.texInfo.x), 0, int(i.texInfo.x) - 1), clamp(int(frac(i.uv.y) * i.texInfo.y), 0, int(i.texInfo.y) - 1));
    albedo *= AlbedoMap.Load(int3(tp, 0)).rgb;
  }
  float metal = i.material.x;
  float rough = i.material.y;
  float receive = i.material.z;
  float3 N = normalize(i.normal);
  SurfaceData s;
  s.N = N;
  s.V = normalize(i.toCam);
  s.albedo = albedo;
  s.metallic = metal;
  s.roughness = max(rough, LP_MIN_ROUGHNESS);

  // Luz ambiente (IBL, como Ibl::IblEvaluate da LightPrism). O ambiente atual e uniforme: a irradiancia SH9 e a radiancia
  // refletida sao a mesma cor, entao o especular e exato. Com cubemap, a radiancia de R vira o cubemap pre-filtrado.
  float ao = AoMap.Load(int3(int2(i.clip.xy), 0));
  float NoV = max(dot(N, s.V), 1e-4f);
  float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metal);
  float2 ab = BrdfLutBilinear(NoV, s.roughness);            // A = escala de F0, B = vies (split-sum)
  float3 Fss = F0 * ab.x + ab.y;
  float3 kD = (1.0f - min(Fss, 1.0f)) * (1.0f - metal);     // energia que nao vira especular
  float3 R = normalize(lerp(N, N * (2.0f * dot(N, s.V)) - s.V, IblDominantDirFactor(s.roughness)));
  float3 ambient = kD * albedo * IblEvaluateIrradiance(N) + Fss * IblEvaluateIrradiance(R);
  float3 color = ambient * ao;
  // Luzes do sol, pontuais e spot: pelos clusters (e pelas cascatas do sol).
  color += EvaluateClusteredLights(s, i.worldPos, i.clip.xy, i.viewDepth, receive);
  color += i.emissive;   // emissao do proprio objeto: nao recebe AO nem sombra; o bloom usa o brilho depois do tone mapping
  if (fogParams.z > 0.0) { // neblina por distancia: mistura com a cor da neblina, linear, antes do tone mapping
    float d = length(i.toCam);
    float f = saturate((d - fogParams.x) / max(fogParams.y - fogParams.x, 0.001)) * fogParams.z;
    color = lerp(color, fogColor.rgb, f);
  }
  return float4(ToneMapACESFilm(color), i.baseColor.a);   // alfa so da cor do material (a mascara do ceu depende disso)
}

// Passe de emissao (bloom): so a radiancia emissiva do objeto, sem luz nem tone mapping. Usa o mesmo VS do PBR, para a
// profundidade ser identica a do desenho principal (o brilho so aparece onde o objeto e visivel). Alvo: brilho linear.
float4 EmissionPS(VSOut i) : SV_Target { return float4(i.emissive, 1.0); }
