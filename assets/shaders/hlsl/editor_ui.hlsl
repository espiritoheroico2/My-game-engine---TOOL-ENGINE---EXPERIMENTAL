// UI do editor (SpriteUI) desenhada pelo Diligent: quads/triangulos ja expandidos pela CPU (apps/editor/ui_gpu_backend.hpp).
// Tudo em espaco sRGB e mistura premultiplicada (ONE, INV_SRC_ALPHA) num backbuffer UNORM: o resultado e o mesmo do
// SDL_Renderer (que mistura em sRGB). A cor (tint) chega ja convertida para sRGB reto e multiplicada pelo alfa.
// UV em PIXELS da textura. mode.y = 0 vizinho mais proximo (Load), 1 bilinear manual (4 Loads, borda repetida).
// Cantos arredondados: radius > 0 usa o SDF de caixa do SpriteUI (1 px de anti-aliasing); radius <= 0 = retangulo/triangulo.
cbuffer EditorUiParams : register(b0) { float2 viewportSize; float2 padding; };
Texture2D uiTex : register(t0);

struct VSIn {
  float2 pos : ATTRIB0;      // pixels do backbuffer, origem no canto superior esquerdo
  float2 uv : ATTRIB1;       // pixels da textura
  float4 tint : ATTRIB2;     // sRGB reto * alfa, e alfa
  float4 shape : ATTRIB3;    // xy = posicao local centrada (px), zw = meia largura/altura (px)
  float2 mode : ATTRIB4;     // x = raio do canto (px), y = filtro (0 nearest, 1 linear)
};
struct VSOut {
  float4 clip : SV_Position;
  float2 uv : TEXCOORD0;
  float4 tint : TEXCOORD1;
  float4 shape : TEXCOORD2;
  float2 mode : TEXCOORD3;
};

VSOut VSMain(VSIn i) {
  VSOut o;
  float2 ndc = float2(i.pos.x / viewportSize.x * 2.0 - 1.0, 1.0 - i.pos.y / viewportSize.y * 2.0);
  o.clip = float4(ndc, 0.0, 1.0);
  o.uv = i.uv;
  o.tint = i.tint;
  o.shape = i.shape;
  o.mode = i.mode;
  return o;
}

float4 PSMain(VSOut i) : SV_Target {
  float cov = 1.0;
  if (i.mode.x > 0.0) {
    float2 q = abs(i.shape.xy) - (i.shape.zw - i.mode.x);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - i.mode.x;
    cov = saturate(0.5 - d);
  }
  uint tw, th;
  uiTex.GetDimensions(tw, th);
  int2 mx = int2(int(tw) - 1, int(th) - 1);
  float4 t;
  if (i.mode.y > 0.5) {
    float2 p = i.uv - 0.5;           // centro do texel 0 em 0.5
    float2 f = frac(p);
    int2 b = int2(floor(p));
    float4 t00 = uiTex.Load(int3(clamp(b, int2(0, 0), mx), 0));
    float4 t10 = uiTex.Load(int3(clamp(b + int2(1, 0), int2(0, 0), mx), 0));
    float4 t01 = uiTex.Load(int3(clamp(b + int2(0, 1), int2(0, 0), mx), 0));
    float4 t11 = uiTex.Load(int3(clamp(b + int2(1, 1), int2(0, 0), mx), 0));
    t = lerp(lerp(t00, t10, f.x), lerp(t01, t11, f.x), f.y);
  } else {
    t = uiTex.Load(int3(clamp(int2(floor(i.uv)), int2(0, 0), mx), 0));
  }
  t.rgb *= t.a;                      // premultiplica depois de filtrar (como o SDL: filtra o reto e mistura depois)
  return t * i.tint * cov;
}
