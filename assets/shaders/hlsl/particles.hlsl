// Particulas (biblioteca v2): quads de billboard instanciados, lidos de um StructuredBuffer (QuadInstance, 32 bytes).
// Desenhados depois da cena opaca e transparente, com teste de profundidade LIGADO e escrita DESLIGADA.
// Entradas de pixel:
//   PSAlpha    : blend alfa (fumaca, poeira); as particulas chegam ordenadas de tras para frente (CPU).
//   PSAdditive : soma a cor ao alvo (fogo, faiscas); nao precisa de ordem.
//   PSGlow     : soma cor * emissive em um alvo de brilho (RGBA16F), que o bloom da engine borra e soma a imagem.
// "Soft particles": se SoftInfo.z > 0, a profundidade linear da cena (prepasso, GBuf.w) e usada para apagar a particula
// perto de superficies (sem aresta dura onde o quad corta a geometria).
cbuffer FrameBuffer : register(b4) { float4x4 viewProj; float4x4 view; float3 camPos; float pad0; };
// first = indice do primeiro quad do lote; flags.x = 1 no modo 2D (plano XY, sem camera).
// soft.x = distancia do fade suave (m, 0 = desligado); soft.y = ganho emissivo; soft.z = 1 se GBuf vale neste quadro.
cbuffer PartBuffer : register(b6) { uint4 flags; float4 soft; };

struct QuadInstance { float3 center; float size; float rot; uint color; uint material; float temp; };
StructuredBuffer<QuadInstance> Quads : register(t0);
Texture2D<float4> GBuf : register(t5);   // xyz = normal de view, w = distancia linear de view (0 = sem geometria)

struct VSIn { float3 corner : ATTRIB0; uint iid : SV_InstanceID; };
struct VSOut { float4 clip : SV_Position; float2 uv : TEXCOORD0; float4 col : TEXCOORD1; float depth : TEXCOORD2; };

float3 SrgbToLinear(float3 c) { return pow(max(c, 0.0), 2.2); }

VSOut VSMain(VSIn i) {
  QuadInstance q = Quads[flags.x + i.iid];
  float3 R = float3(1, 0, 0), U = float3(0, 1, 0);
  if (flags.y == 0u) { R = view[0].xyz; U = view[1].xyz; }   // linhas da matriz de view = eixos da camera no mundo
  float cs = cos(q.rot), sn = sin(q.rot);
  float2 c = i.corner.xy;
  float lx = c.x * cs - c.y * sn, ly = c.x * sn + c.y * cs;
  float3 wp = q.center + (R * lx + U * ly) * (q.size * 0.5);
  VSOut o;
  o.clip = mul(viewProj, float4(wp, 1.0));
  o.uv = c;
  o.col = float4((q.color & 255u), (q.color >> 8) & 255u, (q.color >> 16) & 255u, (q.color >> 24) & 255u) / 255.0;
  o.depth = -mul(view, float4(q.center, 1.0)).z;
  return o;
}

// Forma do sprite: disco com borda suave (gaussiana recortada), 1 no centro e 0 na borda.
float Shape(float2 uv) {
  float r2 = dot(uv, uv);
  float e = exp(-3.0);
  return saturate((exp(-3.0 * r2) - e) / (1.0 - e)) * step(r2, 1.0);
}

// Fade suave perto de superficies: 1 longe, 0 encostado na geometria.
float SoftFade(float4 clip, float depth) {
  if (soft.x <= 0.0 || soft.z <= 0.0) return 1.0;
  float sceneD = GBuf.Load(int3(int2(clip.xy), 0)).w;
  if (sceneD <= 0.0) return 1.0;                       // sem geometria (ceu): nada a evitar
  return saturate((sceneD - depth) / soft.x);
}

float4 PSAlpha(VSOut i) : SV_Target {
  float a = Shape(i.uv) * i.col.a * SoftFade(i.clip, i.depth);
  return float4(SrgbToLinear(i.col.rgb), a);
}

float4 PSAdditive(VSOut i) : SV_Target {
  float a = Shape(i.uv) * i.col.a * SoftFade(i.clip, i.depth);
  return float4(SrgbToLinear(i.col.rgb) * a, 0.0);
}

float4 PSGlow(VSOut i) : SV_Target {
  float a = Shape(i.uv) * i.col.a * SoftFade(i.clip, i.depth);
  return float4(SrgbToLinear(i.col.rgb) * a * soft.y, 0.0);
}
