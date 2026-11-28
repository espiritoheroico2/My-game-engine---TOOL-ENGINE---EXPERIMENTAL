// Skybox de 6 imagens na GPU (substitui o applySkybox da CPU, apps/editor/skybox.hpp, quando a GPU esta ativa).
//
// Passe de tela cheia sobre o alvo de cor, com blend "por baixo" (src * (1 - alfa do destino) + destino * alfa do destino):
// os pixels que a cena deixou com alfa 0 (fundo) viram o ceu (a cor de limpeza some), e saem com alfa 1; a geometria
// (alfa 1) fica como esta. Mesmo resultado da CPU, sem ler a imagem.
//
// Igual a CPU, pixel a pixel:
//  - direcao do raio pela base da camera (frente, direita, cima), tan(fovY/2) e aspecto do alvo;
//  - lado do cubo pelo eixo dominante, com a tabela de skydetail::faceOf (s, t em [-1, 1]);
//  - vizinho mais proximo: texel = clamp(int(frac(u) * largura)), como tool::Texture::texel;
//  - as imagens sobem como RGBA8 (bytes sRGB) e o alvo e RGBA8_SRGB: o shader decodifica sRGB -> linear e o hardware
//    codifica de volta ao gravar, entao o byte final e o byte da imagem.
// O VS e o do SSAO (triangulo de tela cheia, ssao.hlsl VSMain): este arquivo so tem o pixel shader.
cbuffer SkyCB : register(b0) {
  float4 Fwd;      // xyz = frente da camera (unitario); w = tan(fovY / 2)
  float4 Right;    // xyz = direita (unitario);         w = aspecto (largura / altura do alvo)
  float4 Up;       // xyz = cima da camera (unitario);  w = sem uso
  float4 Solid;    // cor dos lados sem imagem (bytes sRGB / 255, como SkyboxSettings::solid)
  float4 Size;     // xy = tamanho do alvo em pixels
  uint4  HasA;     // lados 0..3 tem imagem (1) ou usam a cor solida (0)
  uint4  HasB;     // xy = lados 4 e 5
};
Texture2D<float4> Face0 : register(t0);   // +X
Texture2D<float4> Face1 : register(t1);   // -X
Texture2D<float4> Face2 : register(t2);   // +Y
Texture2D<float4> Face3 : register(t3);   // -Y
Texture2D<float4> Face4 : register(t4);   // +Z
Texture2D<float4> Face5 : register(t5);   // -Z

struct VSOut { float4 clip : SV_Position; };

// sRGB (0..1) -> linear, componente a componente (formula oficial, igual a usada pela GPU ao ler texturas _SRGB).
float SrgbToLinear1(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 SrgbToLinear(float3 c) { return float3(SrgbToLinear1(c.x), SrgbToLinear1(c.y), SrgbToLinear1(c.z)); }

// Texel mais proximo de (u, v) na imagem (v = 0 no topo), como tool::Texture::texel. E uma macro (nao uma funcao com
// Texture2D de parametro) para o GLSL gerado (tools/shaders/build_glsl.py) manter o nome de cada textura (Face0..Face5):
// com parametro, o spirv-cross renomeia as texturas para "param", "param_1"... e o OpenGL nao as acha pelo nome.
#define SKY_NEAREST(TEX, OUT)                                         \
  {                                                                   \
    uint w_, h_;                                                      \
    TEX.GetDimensions(w_, h_);                                        \
    const int x_ = clamp(int(frac(u) * float(w_)), 0, int(w_) - 1);   \
    const int y_ = clamp(int(frac(v) * float(h_)), 0, int(h_) - 1);   \
    OUT = TEX.Load(int3(x_, y_, 0)).rgb;                              \
  }

float4 PSMain(VSOut i) : SV_Target {
  // Pixel -> NDC (y para cima), como em applySkybox: clip.xy ja e o centro do pixel (x + 0,5).
  const float ndcX = 2.0 * i.clip.x / Size.x - 1.0;
  const float ndcY = 1.0 - 2.0 * i.clip.y / Size.y;
  float3 d = Fwd.xyz + Right.xyz * (ndcX * Fwd.w * Right.w) + Up.xyz * (ndcY * Fwd.w);
  d = normalize(d);
  // Lado do cubo e (s, t): tabela de skydetail::faceOf.
  const float ax = abs(d.x), ay = abs(d.y), az = abs(d.z);
  int face; float s, t;
  if (ax >= ay && ax >= az) {
    if (d.x >= 0.0) { face = 0; s = -d.z / ax; t = -d.y / ax; } else { face = 1; s = d.z / ax; t = -d.y / ax; }
  } else if (ay >= az) {
    if (d.y >= 0.0) { face = 2; s = d.x / ay; t = d.z / ay; } else { face = 3; s = d.x / ay; t = -d.z / ay; }
  } else {
    if (d.z >= 0.0) { face = 4; s = d.x / az; t = -d.y / az; } else { face = 5; s = -d.x / az; t = -d.y / az; }
  }
  const float u = (s + 1.0) * 0.5, v = 1.0 - (t + 1.0) * 0.5;
  const uint has = face < 4 ? HasA[face] : HasB[face - 4];
  float3 c = Solid.rgb;
  if (has != 0u) {
    // Um ramo por lado (indexar Texture2D por variavel nao e portavel entre as APIs).
    if (face == 0) SKY_NEAREST(Face0, c)
    else if (face == 1) SKY_NEAREST(Face1, c)
    else if (face == 2) SKY_NEAREST(Face2, c)
    else if (face == 3) SKY_NEAREST(Face3, c)
    else if (face == 4) SKY_NEAREST(Face4, c)
    else SKY_NEAREST(Face5, c)
  }
  return float4(SrgbToLinear(c), 1.0);
}
