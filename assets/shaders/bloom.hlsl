// Bloom da engine (pos-processamento em tela cheia). Qualquer superficie muito clara vira halo, e os objetos
// emissivos (MeshRenderer > Emission) ficam acima do limiar. Passes:
//   BloomThreshold : cor (linear) -> meia resolucao, so o que passa do limiar (media 2x2)
//   BloomBlur      : gaussiana 9 taps separavel (chamada duas vezes: horizontal e vertical)
//   BloomComposite : cor + halo * forca (o alvo final e sRGB: o hardware converte)
// Entrada e saida em linear. O triangulo de tela cheia e o mesmo do ssao.hlsl.
cbuffer BloomCB : register(b4) {
  float2 SrcSize;     // tamanho da cor de origem (pixels)
  float2 HaloSize;    // tamanho do halo (meia resolucao)
  float2 Dir;         // blur: (1,0) horizontal, (0,1) vertical
  float  Threshold;   // limiar de luminancia linear
  float  Strength;    // composicao: quanto do halo soma na cor
};
Texture2D<float4> Src  : register(t0);   // BloomThreshold e BloomComposite (cor)
Texture2D<float4> Halo : register(t1);   // BloomBlur e BloomComposite (halo)

struct VSOut { float4 clip : SV_Position; };
VSOut VSMain(float3 pos : ATTRIB0) { VSOut o; o.clip = float4(pos, 1.0); return o; }

float Luminance3(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float4 BloomThreshold(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy) * 2;                     // pixel da meia resolucao -> bloco 2x2 da origem
  int2 maxP = int2(SrcSize) - 1;
  float3 sum = 0.0;
  [unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x) {
    float3 c = Src.Load(int3(min(p + int2(x, y), maxP), 0)).rgb;
    float l = Luminance3(c);
    sum += c * (max(l - Threshold, 0.0) / max(l, 1e-4)); // so a parte acima do limiar, sem mudar a cor
  }
  return float4(sum * 0.25, 1.0);
}

static const float W[5] = {0.227027, 0.194595, 0.121622, 0.054054, 0.016216};

float4 BloomBlur(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy);
  int2 maxP = int2(HaloSize) - 1;
  int2 d = int2(Dir);
  float3 acc = Halo.Load(int3(p, 0)).rgb * W[0];
  [unroll] for (int k = 1; k < 5; ++k) {
    acc += (Halo.Load(int3(clamp(p + d * k, 0, maxP), 0)).rgb + Halo.Load(int3(clamp(p - d * k, 0, maxP), 0)).rgb) * W[k];
  }
  return float4(acc, 1.0);
}

float4 BloomComposite(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy);
  float3 c = Src.Load(int3(p, 0)).rgb;
  int2 hp = min(int2(float2(p) * (HaloSize / SrcSize)), int2(HaloSize) - 1);
  float3 h = Halo.Load(int3(hp, 0)).rgb;
  return float4(c + h * Strength, 1.0);
}
