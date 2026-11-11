// SpriteUI: quads ja expandidos pela CPU (um quad = 4 vertices). Cores e alfa em premultiplicado linear.
// O atlas e RGBA8 com RGB = 1 e A = cobertura (texto) ou 1 (retangulos). O pixel (0,0) do atlas e branco solido.
// UV e em PIXELS do atlas (nao normalizada): o PS le com Load, sem sampler, como o resto do RHI.
// Cantos arredondados (radius > 0) usam o mesmo SDF de caixa arredondada do SpriteUI (PanelSdf.hpp).
cbuffer SpriteUiParams : register(b0) { float2 viewportSize; float2 atlasSize; };
Texture2D atlasTex : register(t0);

struct VSIn {
  float2 pos : ATTRIB0;      // pixels da tela, origem no canto superior esquerdo
  float2 uv : ATTRIB1;       // pixels do atlas
  float4 tint : ATTRIB2;     // premultiplicada linear
  float2 local : ATTRIB3;    // posicao local do canto, centrada no retangulo (pixels)
  float2 halfSize : ATTRIB4; // meia largura/altura do retangulo (pixels)
  float radius : ATTRIB5;    // raio do canto (0 = retangulo comum)
};
struct VSOut {
  float4 clip : SV_Position;
  float2 uv : TEXCOORD0;
  float4 tint : TEXCOORD1;
  float2 local : TEXCOORD2;
  float2 halfSize : TEXCOORD3;
  float radius : TEXCOORD4;
};

VSOut VSMain(VSIn i) {
  VSOut o;
  float2 ndc = float2(i.pos.x / viewportSize.x * 2.0 - 1.0, 1.0 - i.pos.y / viewportSize.y * 2.0);
  o.clip = float4(ndc, 0.0, 1.0);
  o.uv = i.uv;
  o.tint = i.tint;
  o.local = i.local;
  o.halfSize = i.halfSize;
  o.radius = i.radius;
  return o;
}

float4 PSMain(VSOut i) : SV_Target {
  float cov = 1.0;
  if (i.radius > 0.0) {
    float2 q = abs(i.local) - (i.halfSize - i.radius);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - i.radius;
    cov = saturate(0.5 - d);  // 1 px de anti-aliasing
  }
  float4 t = atlasTex.Load(int3(int2(floor(i.uv)), 0));
  t.rgb *= t.a;               // premultiplica a cobertura do atlas
  return t * i.tint * cov;
}
