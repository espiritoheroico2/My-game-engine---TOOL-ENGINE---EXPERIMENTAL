// ToolEngine Editor - miniatura de material para a janela Assets.
// Renderiza em CPU (sem GPU e sem RHI) uma esfera de 128x128 com a cor, rugosidade, metalico e emissivo do material,
// por intersecao analitica raio-esfera (ortografica) + Lambert + Blinn-Phong + reflexo falso de ceu. Se o material tem
// textura, ela e amostrada pelo UV esferico. Sem janela e sem SDL: roda em teste (Editor.MaterialPreview).
// Tambem guarda o cache nome -> {mtime, miniatura}: so recalcula quando o arquivo muda e gera no maximo
// kMaxPerFrame miniaturas por quadro. O envio dos pixels para a textura de UI fica a cargo do host (callback).
#pragma once
#include "material_asset.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor::material_preview {

inline constexpr int kSize = 128;                 // lado da miniatura em pixels
inline constexpr int kAtlasCols = 8;              // o atlas de UI guarda kAtlasCols x kAtlasCols miniaturas
inline constexpr int kSlots = kAtlasCols * kAtlasCols;
inline constexpr int kMaxPerFrame = 4;            // limite de miniaturas geradas por quadro (nao trava o editor)

namespace detail {
inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
}  // namespace detail

// Renderiza a miniatura (kSize x kSize x 4 bytes, RGBA, linha de cima para baixo). `tex` pode ser nulo.
// As cores sao usadas como estao (sem sRGB), igual ao resto do renderer do editor.
inline std::vector<std::uint8_t> render(const material_asset::MaterialAsset& mat, const tool::Texture* tex = nullptr) {
  using detail::clamp01;
  using detail::lerp;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(kSize) * kSize * 4, 255);
  const material_asset::MaterialAsset m = material_asset::clamped(mat);
  const bool useTex = tex && tex->valid();

  constexpr float kPi = 3.14159265f;
  constexpr float kRadius = 0.86f;   // raio da esfera no quadrado [-1,1]; sobra margem para o contorno
  // Luz direcional (de cima, a esquerda e a frente), ja normalizada.
  const float lx = -0.45f, ly = 0.62f, lz = 0.64f;
  const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  const float Lx = lx / ll, Ly = ly / ll, Lz = lz / ll;
  // Meio-vetor entre a luz e a visao (0,0,1).
  float hx = Lx, hy = Ly, hz = Lz + 1.f;
  const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
  hx /= hl; hy /= hl; hz /= hl;

  const float shin = lerp(256.f, 6.f, m.roughness);            // brilho do especular
  const float specNorm = (shin + 8.f) / (8.f * kPi) * 0.25f;   // normalizacao aproximada do Blinn-Phong
  const float invSize = 2.f / static_cast<float>(kSize);

  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      // Fundo neutro: xadrez cinza (mostra a transparencia do material) em blocos de 8 px.
      const float chk = (((x >> 3) + (y >> 3)) & 1) ? 0.27f : 0.21f;
      float r = chk, g = chk, b = chk;

      const float nx = ((static_cast<float>(x) + 0.5f) * invSize - 1.f) / kRadius;
      const float ny = -((static_cast<float>(y) + 0.5f) * invSize - 1.f) / kRadius;
      const float d2 = nx * nx + ny * ny;
      // Cobertura do contorno (anti-aliasing): 1 dentro, cai a 0 em ~1 px na borda.
      const float d = std::sqrt(d2);
      const float cover = clamp01((1.f - d) * kRadius * static_cast<float>(kSize) * 0.5f + 0.5f);
      if (cover > 0.f) {
        const float nz = std::sqrt(std::max(0.f, 1.f - std::min(d2, 1.f)));
        const float inv = d2 > 1.f ? 1.f / d : 1.f;   // pixel de borda: projeta a normal na esfera
        const float Nx = nx * inv, Ny = ny * inv, Nz = d2 > 1.f ? 0.f : nz;

        // Cor base: albedo vezes a textura (UV esferico; a face visivel cobre a textura toda em U).
        float br = m.albedo[0], bg = m.albedo[1], bb = m.albedo[2];
        if (useTex) {
          // Tiling e offset do material (uvTiling/uvOffset) valem tambem na miniatura.
          const float u = (0.5f + std::atan2(Nx, Nz) / kPi) * m.uvTiling[0] + m.uvOffset[0];
          const float v = (0.5f - std::asin(std::clamp(Ny, -1.f, 1.f)) / kPi) * m.uvTiling[1] + m.uvOffset[1];
          std::uint8_t t[4];
          tex->texel(u, v, t);
          br *= static_cast<float>(t[0]) / 255.f;
          bg *= static_cast<float>(t[1]) / 255.f;
          bb *= static_cast<float>(t[2]) / 255.f;
        }

        const float ndl = std::max(0.f, Nx * Lx + Ny * Ly + Nz * Lz);
        const float ndh = std::max(0.f, Nx * hx + Ny * hy + Nz * hz);
        // Ambiente hemisferico simples (ceu em cima, chao embaixo).
        const float amb = lerp(0.10f, 0.28f, Ny * 0.5f + 0.5f);
        const float diff = ndl * 0.9f + amb;
        // Reflectancia de Fresnel base: 4% nos dieletricos, a cor do material nos metais.
        const float f0r = lerp(0.04f, br, m.metallic), f0g = lerp(0.04f, bg, m.metallic), f0b = lerp(0.04f, bb, m.metallic);
        const float spec = std::pow(ndh, shin) * specNorm * ndl;
        // Reflexo falso do ambiente (gradiente ceu/chao): da cara aos metais, que nao tem difuso.
        // Componente Y do vetor refletido de V=(0,0,1): R = 2(N.V)N - V  =>  Ry = 2*Nz*Ny.
        const float ry = 2.f * Nz * Ny;
        const float envY = clamp01(ry * 0.5f + 0.5f);
        const float env = lerp(0.12f, 0.95f, envY) * (1.f - m.roughness * 0.75f);
        const float kd = 1.f - m.metallic;

        float cr = br * kd * diff + f0r * (spec + env * 0.5f);
        float cg = bg * kd * diff + f0g * (spec + env * 0.5f);
        float cb = bb * kd * diff + f0b * (spec + env * 0.5f);
        // Brilho especular branco nos dieletricos (a cor do reflexo nao e a do material).
        const float whiteSpec = spec * 0.8f * kd;
        cr += whiteSpec; cg += whiteSpec; cb += whiteSpec;
        // Emissivo soma por cima da luz.
        cr += m.emissive[0] * m.emissiveStrength;
        cg += m.emissive[1] * m.emissiveStrength;
        cb += m.emissive[2] * m.emissiveStrength;
        cr = clamp01(cr); cg = clamp01(cg); cb = clamp01(cb);

        // Mistura com o fundo: opaco usa so a cobertura; alfa mistura pelo albedo.a; aditivo soma.
        float a = cover;
        if (m.blend == material_asset::Blend::Alpha) {
          // alphaCutoff > 0: pixel com alfa abaixo do corte e descartado (mostra o fundo).
          a = (m.alphaCutoff > 0.f && m.albedo[3] < m.alphaCutoff) ? 0.f : a * m.albedo[3];
        }
        if (m.blend == material_asset::Blend::Additive) {
          r = clamp01(r + cr * a); g = clamp01(g + cg * a); b = clamp01(b + cb * a);
        } else {
          r = lerp(r, cr, a); g = lerp(g, cg, a); b = lerp(b, cb, a);
        }
      }
      std::uint8_t* p = &out[(static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4];
      p[0] = static_cast<std::uint8_t>(clamp01(r) * 255.f + 0.5f);
      p[1] = static_cast<std::uint8_t>(clamp01(g) * 255.f + 0.5f);
      p[2] = static_cast<std::uint8_t>(clamp01(b) * 255.f + 0.5f);
      p[3] = 255;
    }
  }
  return out;
}

// Cor do fundo (xadrez) no pixel (x,y), em 0..255 - usada por testes para comparar "centro != fundo".
inline std::uint8_t backgroundGray(int x, int y) {
  return static_cast<std::uint8_t>((((x >> 3) + (y >> 3)) & 1) ? 0.27f * 255.f + 0.5f : 0.21f * 255.f + 0.5f);
}

// Funcao que carrega uma imagem de disco (injetada pelo host, que tem o image_io). Nula = materiais sem textura.
using TextureLoader = std::function<bool(const std::string& absPath, tool::Texture& out)>;
// Envia os pixels (kSize x kSize RGBA) de uma miniatura para o slot `slot` do atlas de UI (feito pelo host).
using Uploader = std::function<void(int slot, const std::uint8_t* rgba)>;

// Cache das miniaturas: chave = caminho absoluto do .json. Recalcula so quando o mtime do arquivo muda e gera no
// maximo kMaxPerFrame por quadro. Cada miniatura ocupa um slot do atlas (kSlots no total; o menos usado e reciclado).
class ThumbCache {
 public:
  // Chamar uma vez por quadro, antes de pedir miniaturas: zera o orcamento e guarda o relogio (segundos).
  void beginFrame(double nowSeconds) {
    budget_ = kMaxPerFrame;
    now_ = nowSeconds;
    ++frame_;
  }

  void setTextureLoader(TextureLoader loader) { loader_ = std::move(loader); }

  // Slot do atlas com a miniatura do material em `jsonPath`, ou -1 (ainda nao gerada neste quadro, ou arquivo ruim).
  // `projectDir` resolve o caminho relativo da textura. `upload` recebe os pixels quando uma miniatura e (re)gerada.
  int get(const std::string& jsonPath, const std::string& projectDir, const Uploader& upload) {
    Entry& e = map_[jsonPath];
    e.lastUsed = frame_;
    // Confere o mtime no maximo uma vez por segundo por entrada (evita uma chamada de disco por item por quadro).
    if (!e.checked || now_ - e.checkedAt >= 1.0) {
      std::error_code ec;
      const auto mt = std::filesystem::last_write_time(jsonPath, ec);
      e.checkedAt = now_;
      if (ec) { e.ready = false; e.failed = true; e.checked = true; return -1; }   // arquivo sumiu
      if (!e.checked || mt != e.mtime) { e.mtime = mt; e.ready = false; e.failed = false; }
      e.checked = true;
    }
    if (e.failed) return -1;
    if (e.ready) return e.slot;
    if (budget_ <= 0) return e.slot >= 0 ? e.slot : -1;   // sem orcamento: mostra a miniatura antiga (ou o icone) por ora
    --budget_;

    material_asset::MaterialAsset mat;
    std::string err;
    if (!material_asset::load(jsonPath, mat, err)) { e.failed = true; return -1; }
    tool::Texture tex;
    const tool::Texture* texPtr = nullptr;
    if (!mat.texture.empty() && loader_) {
      const std::filesystem::path tp = std::filesystem::path(projectDir) / mat.texture;
      if (loader_(tp.string(), tex) && tex.valid()) texPtr = &tex;
    }
    if (e.slot < 0) e.slot = allocSlot(jsonPath);
    const std::vector<std::uint8_t> px = render(mat, texPtr);
    if (upload) upload(e.slot, px.data());
    e.ready = true;
    return e.slot;
  }

  // Numero de entradas guardadas (para teste e depuracao).
  std::size_t size() const { return map_.size(); }
  // Miniaturas geradas desde o inicio (para teste).
  std::uint64_t generated() const { return generated_; }

 private:
  struct Entry {
    std::filesystem::file_time_type mtime{};
    double checkedAt = 0.0;
    std::uint64_t lastUsed = 0;
    int slot = -1;
    bool checked = false, ready = false, failed = false;
  };

  // Slot livre; se todos estao em uso, recicla o da entrada menos usada recentemente.
  int allocSlot(const std::string& forKey) {
    std::vector<char> used(kSlots, 0);
    for (auto& [k, v] : map_) if (v.slot >= 0) used[static_cast<std::size_t>(v.slot)] = 1;
    for (int s = 0; s < kSlots; ++s) if (!used[static_cast<std::size_t>(s)]) { ++generated_; return s; }
    Entry* victim = nullptr;
    for (auto& [k, v] : map_) {
      if (k == forKey || v.slot < 0) continue;
      if (!victim || v.lastUsed < victim->lastUsed) victim = &v;
    }
    ++generated_;
    if (!victim) return 0;
    const int s = victim->slot;
    victim->slot = -1; victim->ready = false;   // quem perdeu o slot regenera quando for visto de novo
    return s;
  }

  std::unordered_map<std::string, Entry> map_;
  TextureLoader loader_;
  int budget_ = kMaxPerFrame;
  double now_ = 0.0;
  std::uint64_t frame_ = 0;
  std::uint64_t generated_ = 0;
};

// Retangulo UV (u, v, largura, altura) do slot no atlas, recuado 1 texel para o filtro linear nao vazar o vizinho.
inline void slotUv(int slot, float uv[4]) {
  const float cell = 1.f / static_cast<float>(kAtlasCols);
  const float texel = 1.f / static_cast<float>(kAtlasCols * kSize);
  const int col = slot % kAtlasCols, row = slot / kAtlasCols;
  uv[0] = static_cast<float>(col) * cell + texel;
  uv[1] = static_cast<float>(row) * cell + texel;
  uv[2] = cell - 2.f * texel;
  uv[3] = cell - 2.f * texel;
}

}  // namespace editor::material_preview
