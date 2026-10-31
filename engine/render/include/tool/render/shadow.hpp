// ToolEngine - sombras em tempo real da luz direcional ("sol"): configuracao e matriz de luz.
//
// TECNICA: shadow mapping com projecao ortografica. A cena e desenhada UMA vez do ponto de vista do sol, guardando
// a profundidade (z da luz, 0..1) numa textura quadrada. Ao sombrear um pixel, o ponto e levado ao espaco da luz e
// comparado com a profundidade guardada: se houver algo mais perto do sol, o pixel esta na sombra. O filtro PCF
// (media de varias comparacoes vizinhas) suaviza a borda.
// CPU (SoftRenderer) e GPU (GpuRenderer + shaders) usam EXATAMENTE esta matriz, entao o resultado e o mesmo.
//
// Custo: 1 passe extra de profundidade (so objetos com "Cast Shadow") + 1 leitura (x kernel PCF) por pixel de objeto
// com "Receive Shadow". Desligue em Settings > Graphics se precisar de mais FPS.
#pragma once
#include "tool/math/math.hpp"

#include <algorithm>
#include <cmath>

namespace tool {

struct ShadowSettings {
  bool enabled = true;
  int resolution = 1024;      // lado do mapa em texels (512/1024/2048/4096)
  float distance = 40.f;      // raio (m) ao redor do centro da cena coberto pelo mapa; menor = sombras mais nitidas
  float bias = 0.0025f;       // desloca a comparacao (em profundidade 0..1) para evitar "acne" (listras na superficie)
  float normalBias = 1.5f;    // desloca o ponto ao longo da normal, em texels (reduz acne em superficies inclinadas)
  int pcf = 1;                // 0 = bordas duras; 1 = filtro 3x3; 2 = filtro 5x5 (mais macio e mais caro)
  float strength = 0.85f;     // 0 = sombra invisivel; 1 = totalmente escura (o ambiente ainda ilumina)
};

struct ShadowFrustum {
  Mat4 viewProj;      // espaco do mundo -> clip da luz (ortografico, z em 0..1)
  float texelWorld = 0.f; // tamanho de um texel em metros (para o normal bias)
  bool valid = false;
};

// Matriz do sol cobrindo uma esfera (centro, raio). `dir` = direcao em que a luz viaja.
// O centro e "grudado" na grade de texels: sem isso as bordas das sombras tremem quando a camera se mexe.
inline ShadowFrustum makeSunShadow(Vec3 dir, Vec3 center, f32 radius, int resolution) {
  ShadowFrustum f;
  dir = normalize(dir);
  if (length(dir) < 0.5f || radius < 0.01f || resolution < 16) return f;
  const Vec3 up = std::abs(dir.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  const Vec3 right = normalize(cross(dir, up)), upv = cross(right, dir);
  const f32 texel = 2.f * radius / static_cast<f32>(resolution);
  const f32 cx = dot(center, right), cy = dot(center, upv);
  center = center + right * (std::floor(cx / texel) * texel - cx) + upv * (std::floor(cy / texel) * texel - cy);
  const Mat4 view = Mat4::lookAt(center - dir * (radius * 2.f), center, up);
  const Mat4 proj = Mat4::ortho(-radius, radius, -radius, radius, 0.05f, radius * 4.f);
  f.viewProj = proj * view; f.texelWorld = texel; f.valid = true;
  return f;
}

} // namespace tool
