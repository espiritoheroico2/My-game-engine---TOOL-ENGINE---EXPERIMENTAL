// ToolEngine Editor - Skybox de 6 imagens (um slot por lado do cubo: +X, -X, +Y, -Y, +Z, -Z).
//
// Cada slot guarda o caminho de uma imagem da pasta Assets (PNG, JPG, BMP, TGA), igual ao campo Texture do
// Mesh Renderer. Slot vazio = cor solida (SkyboxSettings::solid). Nada e gerado por codigo.
//
// Como funciona: o renderizador limpa o fundo com alfa 0 (SkyboxSettings::enabled). Depois de desenhar a cena,
// applySkybox preenche SO os pixels de alfa 0 (sem geometria) e os deixa com alfa 255. Vale para a CPU e a GPU
// (a GPU volta os pixels para a CPU antes disso). Cameras 2D (ortograficas) nao usam skybox.
//
// Amostragem: para cada pixel de fundo calculamos a direcao do raio, escolhemos o lado pelo eixo dominante e
// lemos a imagem do lado com o vizinho mais proximo (tool::Texture::texel). As coordenadas seguem a tabela de
// cube map do OpenGL, com o topo da imagem voltado para +Y do lado. Se algum lado aparecer girado ou espelhado,
// a correcao e trocar o sinal de s ou de t na tabela de skydetail::faceOf.
#pragma once
#include "tool/core/parallel.hpp"
#include "tool/render/soft_renderer.hpp"
#include "tool/render/texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace editor {

inline constexpr std::array<const char*, 6> kSkyFaceNames = {"+X (right)", "-X (left)", "+Y (top)", "-Y (bottom)", "+Z (front)", "-Z (back)"};

struct SkyboxSettings {
  bool enabled = false;                       // cenas antigas ficam sem skybox; cena nova liga (ver createDefaultContent)
  char face[6][160] = {};                     // imagem de cada lado (caminho em Assets); vazio = cor solida
  float solid[3] = {0.25f, 0.40f, 0.60f};     // cor dos lados sem imagem
};

// Imagens dos 6 lados ja carregadas (nullptr = usa a cor solida). Ordem igual a kSkyFaceNames.
using SkyTextures = std::array<const tool::Texture*, 6>;

namespace skydetail {
struct Rgb { float r, g, b; };

// Lado do cubo de uma direcao (unitaria) e coordenadas (s, t) ja divididas pelo eixo dominante, em [-1, 1].
inline int faceOf(float x, float y, float z, float& s, float& t) {
  const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
  if (ax >= ay && ax >= az) {
    const float m = ax;
    if (x >= 0.f) { s = -z / m; t = -y / m; return 0; }   // +X
    s = z / m; t = -y / m; return 1;                       // -X
  }
  if (ay >= az) {
    const float m = ay;
    if (y >= 0.f) { s = x / m; t = z / m; return 2; }     // +Y
    s = x / m; t = -z / m; return 3;                       // -Y
  }
  const float m = az;
  if (z >= 0.f) { s = x / m; t = -y / m; return 4; }      // +Z
  s = -x / m; t = -y / m; return 5;                        // -Z
}

// Cor de um pixel do skybox: imagem do lado (se houver) ou a cor solida.
inline void colorOf(const SkyboxSettings& s, const SkyTextures& tex, float x, float y, float z, tool::u8 out[3]) {
  float sc = 0.f, tc = 0.f;
  const int face = faceOf(x, y, z, sc, tc);
  const tool::Texture* img = tex[static_cast<size_t>(face)];
  if (img && img->valid()) {
    tool::u8 px[4];
    img->texel((sc + 1.f) * 0.5f, 1.f - (tc + 1.f) * 0.5f, px); // v = 0 no topo da imagem
    out[0] = px[0]; out[1] = px[1]; out[2] = px[2];
    return;
  }
  for (int i = 0; i < 3; ++i) out[i] = static_cast<tool::u8>(std::clamp(s.solid[i], 0.f, 1.f) * 255.f + 0.5f);
}
} // namespace skydetail

// Preenche os pixels de fundo (alfa 0) de uma imagem RGBA w x h com o skybox, vista pela camera `cam`.
inline void applySkybox(tool::u8* px, int w, int h, const tool::Camera& cam, const SkyboxSettings& s, const SkyTextures& tex) {
  if (!px || !s.enabled || cam.ortho || w <= 0 || h <= 0) return;
  auto sub = [](tool::Vec3 a, tool::Vec3 b) { return tool::Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
  auto cross = [](tool::Vec3 a, tool::Vec3 b) { return tool::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
  const tool::Vec3 f0 = sub(cam.target, cam.eye);
  const float fl = std::sqrt(f0.x * f0.x + f0.y * f0.y + f0.z * f0.z);
  const tool::Vec3 f{f0.x / fl, f0.y / fl, f0.z / fl};
  const tool::Vec3 r0 = cross(f, cam.up);
  const float rl = std::sqrt(r0.x * r0.x + r0.y * r0.y + r0.z * r0.z);
  const tool::Vec3 r{r0.x / rl, r0.y / rl, r0.z / rl};
  const tool::Vec3 u = cross(r, f);   // base da camera, igual a do lookAt
  const float t = std::tan(cam.fovY * 0.5f), aspect = static_cast<float>(w) / static_cast<float>(h);
  const size_t grain = std::max<size_t>(1, static_cast<size_t>(h) / 64);
  tool::parallelFor(static_cast<size_t>(h), grain, [&](size_t y0, size_t y1) {
    for (size_t y = y0; y < y1; ++y) {
      const float ndcY = 1.f - 2.f * (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
      for (int x = 0; x < w; ++x) {
        tool::u8* p = px + (y * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
        if (p[3] != 0) continue;   // ja tem geometria
        const float ndcX = 2.f * (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 1.f;
        const float dx = f.x + r.x * ndcX * t * aspect + u.x * ndcY * t;
        const float dy = f.y + r.y * ndcX * t * aspect + u.y * ndcY * t;
        const float dz = f.z + r.z * ndcX * t * aspect + u.z * ndcY * t;
        const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
        tool::u8 c[3];
        skydetail::colorOf(s, tex, dx / dl, dy / dl, dz / dl, c);
        p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        p[3] = 255;
      }
    }
  });
}

} // namespace editor
