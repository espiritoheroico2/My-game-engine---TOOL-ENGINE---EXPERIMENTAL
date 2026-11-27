// Miniatura de material (material_preview.hpp): tamanho, centro diferente do fundo, cache por mtime e limite por quadro.
// Roda no ctest como Editor.MaterialPreview. Sem janela; usa uma pasta temporaria do sistema.
#include "material_preview.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
int run = 0, failed = 0;
void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) { ++failed; std::fprintf(stderr, "[FAIL] linha %d: %s\n", line, expr); }
}
#define CHECK(e) Check((e), #e, __LINE__)
}

int main() {
  namespace mp = editor::material_preview;
  namespace ma = editor::material_asset;
  namespace fs = std::filesystem;

  // Miniatura: 128x128 RGBA; o centro (esfera) difere do canto (fundo xadrez).
  ma::MaterialAsset m;
  m.albedo[0] = 0.9f; m.albedo[1] = 0.2f; m.albedo[2] = 0.1f;
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<std::uint8_t> px = mp::render(m);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("miniatura 128x128: %.3f ms\n", ms);
  CHECK(px.size() == static_cast<std::size_t>(mp::kSize) * mp::kSize * 4);
  const std::size_t c = (static_cast<std::size_t>(mp::kSize / 2) * mp::kSize + mp::kSize / 2) * 4;
  CHECK(px[c + 3] == 255);
  CHECK(px[c] != mp::backgroundGray(mp::kSize / 2, mp::kSize / 2) || px[c + 1] != px[c]);   // centro nao e cinza do fundo
  CHECK(px[c] > px[c + 1] && px[c] > px[c + 2]);                                           // vermelho domina (albedo)
  CHECK(px[0] == mp::backgroundGray(0, 0) && px[1] == px[0] && px[2] == px[0]);            // canto e o fundo

  // Metalico e rugosidade mudam o resultado.
  ma::MaterialAsset metal = m; metal.metallic = 1.f; metal.roughness = 0.1f;
  CHECK(mp::render(metal) != px);

  // Textura amostrada pelo UV esferico: textura 2x2 verde muda a cor do centro.
  tool::Texture tex; tex.w = 2; tex.h = 2; tex.rgba.assign(16, 0);
  for (int i = 0; i < 4; ++i) { tex.rgba[i * 4 + 1] = 255; tex.rgba[i * 4 + 3] = 255; }
  ma::MaterialAsset white; // albedo branco
  const auto withTex = mp::render(white, &tex);
  CHECK(withTex[c + 1] > withTex[c] && withTex[c + 1] > withTex[c + 2]);

  // Cache: grava 6 materiais; o orcamento por quadro limita a kMaxPerFrame; recalcula so quando o arquivo muda.
  const fs::path tmp = fs::temp_directory_path() / "toolengine_material_preview_test";
  std::error_code ec; fs::remove_all(tmp, ec);
  std::string err;
  std::vector<std::string> paths;
  for (int i = 0; i < 6; ++i) {
    ma::MaterialAsset a; a.name = "m" + std::to_string(i); a.roughness = 0.1f * static_cast<float>(i);
    CHECK(ma::save(tmp.string(), a, err));
    paths.push_back(ma::pathFor(tmp.string(), a.name));
  }
  mp::ThumbCache cache;
  int uploads = 0;
  const mp::Uploader up = [&](int, const std::uint8_t*) { ++uploads; };
  cache.beginFrame(0.0);
  int got = 0;
  for (const auto& p : paths) if (cache.get(p, tmp.string(), up) >= 0) ++got;
  CHECK(uploads == mp::kMaxPerFrame);   // so 4 geradas no primeiro quadro
  CHECK(got == mp::kMaxPerFrame);
  cache.beginFrame(0.016);
  for (const auto& p : paths) cache.get(p, tmp.string(), up);
  CHECK(uploads == 6);                  // o resto sai no quadro seguinte
  cache.beginFrame(0.033);
  for (const auto& p : paths) cache.get(p, tmp.string(), up);
  CHECK(uploads == 6);                  // nada mudou: nada recalculado
  // Arquivo alterado (mtime novo): recalcula so esse, depois de 1 s (intervalo de conferencia do mtime).
  fs::last_write_time(paths[2], fs::last_write_time(paths[2], ec) + std::chrono::seconds(5), ec);
  cache.beginFrame(2.0);
  for (const auto& p : paths) cache.get(p, tmp.string(), up);
  CHECK(uploads == 7);
  // Arquivo invalido: nunca vira miniatura e nao tenta de novo a cada quadro.
  { std::ofstream f(paths[5], std::ios::trunc); f << "nao e json"; }
  fs::last_write_time(paths[5], fs::last_write_time(paths[5], ec) + std::chrono::seconds(9), ec);
  cache.beginFrame(4.0);
  CHECK(cache.get(paths[5], tmp.string(), up) == -1);
  fs::remove_all(tmp, ec);

  std::printf("%d verificacoes, %d falhas\n", run, failed);
  return failed ? 1 : 0;
}
