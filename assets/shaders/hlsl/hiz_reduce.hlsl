// Piramide HiZ: reduz o nivel anterior pelo MAXIMO (profundidade linear de view: maior = mais longe).
// Maximo porque o teste e conservador: o objeto so some se esta atras da superficie MAIS DISTANTE do retangulo, ou seja,
// se em todo pixel coberto existe um oclusor mais perto que o ponto mais proximo do objeto. Pixel sem oclusor vale um
// valor enorme, entao qualquer buraco no retangulo deixa o maximo enorme e o objeto fica visivel.
//
// Cada nivel e uma textura R32F propria (o RHI nao expoe vista de mip de textura nem UAV de textura no compute).
// Texel (x,y) do destino cobre [2x, 2x+1] da origem; o ultimo texel de cada eixo tambem absorve a sobra de dimensao impar.
cbuffer HizCB : register(b4) { uint4 dims; };   // x,y = tamanho da origem; z,w = tamanho do destino
Texture2D<float> Src : register(t5);

struct VSOut { float4 clip : SV_Position; };

float HizReduce(VSOut i) : SV_Target {
  int2 p = int2(i.clip.xy);
  int2 s0 = p * 2;
  int2 s1 = int2(p.x == int(dims.z) - 1 ? int(dims.x) - 1 : s0.x + 1, p.y == int(dims.w) - 1 ? int(dims.y) - 1 : s0.y + 1);
  s1 = min(s1, int2(int(dims.x) - 1, int(dims.y) - 1));
  float m = 0.0;
  for (int y = s0.y; y <= s1.y; ++y)
    for (int x = s0.x; x <= s1.x; ++x)
      m = max(m, Src.Load(int3(x, y, 0)));
  return m;
}
