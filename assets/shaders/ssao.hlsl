// SSAO em espaco de tela, portado do compute da Light Prism (shaders/PostProcess/Ssao.compute.hlsl) para pixel shader,
// porque o RHI da engine nao tem compute. Mesmo algoritmo, mesmas constantes (SsaoGPU, 576 bytes):
//   SsaoMain : AO bruto (GBuf -> RawAo)        SsaoBlur : bilateral 4x4 (RawAo + GBuf -> Ao)
// GBuf (RGBA16F, do prepass): xyz = normal de view, w = distancia linear de view (0 = sem geometria).
// Diferenca: a profundidade ja vem linear do prepasso (a Light Prism linearizava o depth buffer reverso-Z).
cbuffer SsaoCB : register(b4) {
  float4 Kernel[32];
  float  Proj00, Proj11, Proj22, Proj23;
  float2 InvSize;
  uint2  Size;
  float  Radius, Bias, Power, BlurDepthRel;
  uint   KernelSize;
  float  BiasPerMeter, FadeStart, FadeEnd;
};
Texture2D<float4> GBuf  : register(t5);
Texture2D<float>  RawAo : register(t6);

struct VSOut { float4 clip : SV_Position; };
VSOut VSMain(float3 pos : ATTRIB0) { VSOut o; o.clip = float4(pos, 1.0); return o; } // triangulo de tela cheia

static const float PI = 3.14159265359;
float LinearDepth(int2 p) { return GBuf.Load(int3(p, 0)).w; }
float3 ViewPos(int2 p, float d) {
  float2 ndc = (float2(p) + 0.5) * InvSize * 2.0 - 1.0;
  ndc.y = -ndc.y;
  return float3(ndc.x * d / Proj00, ndc.y * d / Proj11, -d);
}
float Rotation(int2 p) {
  static const uint pattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  return float(pattern[(p.y & 3) * 4 + (p.x & 3)]) * (2.0 * PI / 16.0);
}
float SmoothStep01(float t) { t = saturate(t); return t * t * (3.0 - 2.0 * t); }

float SsaoMain(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy);
  float d = LinearDepth(p);
  if (d <= 0.0) return 1.0;
  float3 pv = ViewPos(p, d);
  float3 N = normalize(GBuf.Load(int3(p, 0)).xyz);
  float3 up = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
  float3 t0 = normalize(cross(up, N));
  float3 b0 = cross(N, t0);
  float rot = Rotation(p);
  float cr = cos(rot), sr = sin(rot);
  float3 T = t0 * cr + b0 * sr;
  float3 B = b0 * cr - t0 * sr;
  float biasEff = Bias + BiasPerMeter * d;
  float occ = 0.0;
  for (uint k = 0; k < KernelSize; ++k) {
    float3 kv = Kernel[k].xyz;
    float3 sp = pv + (T * kv.x + B * kv.y + N * kv.z) * Radius;
    if (sp.z >= -1e-4) continue;
    float2 uv = float2(sp.x * Proj00 / -sp.z * 0.5 + 0.5, 0.5 - sp.y * Proj11 / -sp.z * 0.5);
    if (any(uv < 0.0) || any(uv >= 1.0)) continue;
    float sd = LinearDepth(int2(uv * float2(Size)));
    if (sd <= 0.0) continue;
    float sceneZ = -sd;
    if (sceneZ >= sp.z + biasEff) occ += SmoothStep01(Radius / max(abs(pv.z - sceneZ), 1e-4));
  }
  float ao = 1.0 - occ / float(KernelSize);
  float shaped = Power == 1.0 ? ao : pow(ao, Power);
  float fade = SmoothStep01((d - FadeStart) / max(FadeEnd - FadeStart, 1e-3));
  return lerp(shaped, 1.0, fade);
}

float SsaoBlur(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy);
  float d = LinearDepth(p);
  if (d <= 0.0) return 1.0;
  float sum = 0.0, wsum = 0.0;
  for (int dy = -2; dy <= 1; ++dy)
    for (int dx = -2; dx <= 1; ++dx) {
      int2 q = p + int2(dx, dy);
      if (any(q < 0) || any(q >= int2(Size))) continue;
      float dj = LinearDepth(q);
      if (dj <= 0.0 || abs(dj - d) > BlurDepthRel * d) continue;
      sum += RawAo.Load(int3(q, 0));
      wsum += 1.0;
    }
  return wsum > 0.0 ? sum / wsum : RawAo.Load(int3(p, 0));
}
