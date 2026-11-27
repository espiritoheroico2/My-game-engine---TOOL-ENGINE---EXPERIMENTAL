// Teste com GPU real do FSR 1 (EASU + RCAS em pixel shader, tool/render/fsr1.hpp + assets/shaders/hlsl/fsr1.hlsl).
// Renderiza um padrao conhecido em 64x64 (borda diagonal + gradientes), amplia para 128x128 com EASU+RCAS e confere:
//   - o passe realmente roda (saida de 128x128, nao identica ao upscale bilinear de referencia, mas parecida com ele);
//   - o RCAS muda a imagem do EASU com nitidez 1 e quase nao muda com nitidez 0;
//   - nao ha NaN/Inf nem pixel preto (um NaN escrito num alvo UNORM vira 0; o padrao nao tem valores abaixo de 20).
// Uso: fsr1_test [vulkan|d3d12]   (padrao: vulkan, offscreen sem janela)
#include "tool/render/fsr1.hpp"
#include "tool/render/upscale.hpp"
#include "tool/rhi/diligent_device.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tool;

namespace {

int failures = 0;

// Registra uma verificacao; imprime e conta falhas.
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "[ok]  " : "[FALHA]", what);
  if (!ok) ++failures;
}

// Padrao conhecido: borda diagonal forte (70 / 200) + gradiente em G (por linha) e B (por coluna). Nenhum canal < 60.
std::vector<u8> makePattern(u32 n) {
  std::vector<u8> px(static_cast<size_t>(n) * n * 4);
  for (u32 y = 0; y < n; ++y)
    for (u32 x = 0; x < n; ++x) {
      u8* p = px.data() + (static_cast<size_t>(y) * n + x) * 4;
      p[0] = (x + y) < n ? 200 : 70;
      p[1] = static_cast<u8>(60 + y * 2);
      p[2] = static_cast<u8>(100 + x);
      p[3] = 255;
    }
  return px;
}

// Diferenca entre duas imagens RGBA8 do mesmo tamanho: media absoluta e quantidade de pixels com algum canal acima de `tol`.
void diff(const std::vector<u8>& a, const std::vector<u8>& b, int tol, double& mean, size_t& over, int& maxd) {
  mean = 0; over = 0; maxd = 0;
  const size_t n = a.size() / 4;
  for (size_t i = 0; i < n; ++i) {
    int m = 0;
    for (int c = 0; c < 3; ++c) { const int d = std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c])); mean += d; m = std::max(m, d); }
    if (m > tol) ++over;
    maxd = std::max(maxd, m);
  }
  mean /= static_cast<double>(n * 3);
}

} // namespace

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  rhi::DeviceInfo info;
  info.width = 64; info.height = 64;
  if (which == "vulkan") info.backend = rhi::Backend::Vulkan;
  else if (which == "d3d12") info.backend = rhi::Backend::D3D12;
  else { std::fprintf(stderr, "backend nao suportado por este teste: %s\n", which.c_str()); return 2; }

  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar o dispositivo: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s\n", rhi::toString(dev->backend()), dev->deviceName().c_str());

  constexpr u32 kIn = 64, kOut = 128;
  const std::vector<u8> pattern = makePattern(kIn);
  const rhi::TextureId src = dev->createTexture({kIn, kIn, 1, rhi::Format::RGBA8, false}, pattern.data());
  check(!src.isNull(), "textura de origem 64x64 criada");

  render::Fsr1Pass fsr;
  const bool ok = fsr.init(*dev, std::string(TOOL_SOURCE_DIR) + "/assets/shaders/hlsl", rhi::Format::RGBA8);
  if (!ok) std::printf("  motivo: %s\n", fsr.whyNot().c_str());
  check(ok && fsr.ready(), "FSR 1 inicializado (shaders EASU/RCAS e pipelines)");
  if (!ok) return 1;

  // Referencia: upscale bilinear de software do mesmo padrao.
  std::vector<u8> bilinear(static_cast<size_t>(kOut) * kOut * 4);
  upscaleRgba(pattern.data(), kIn, kIn, bilinear.data(), kOut, kOut, UpscaleFilter::Bilinear);

  // Nitidez 1 (RCAS no maximo).
  rhi::TextureId outTex = fsr.run(src, kIn, kIn, kOut, kOut, 1.0f);
  check(!outTex.isNull(), "EASU + RCAS rodaram e devolveram a textura final");
  if (outTex.isNull()) return 1;
  std::vector<u8> out, easu; u32 w = 0, h = 0, w2 = 0, h2 = 0;
  const bool rd = dev->readTexture(outTex, out, w, h) && dev->readTexture(fsr.easuTexture(), easu, w2, h2);
  check(rd && w == kOut && h == kOut && w2 == kOut && h2 == kOut && out.size() == bilinear.size(), "leitura da saida: 128x128");
  if (!rd || out.size() != bilinear.size()) return 1;

  // 1) Nao identica ao bilinear (o passe realmente roda), mas parecida (orientacao e cores certas).
  double mean = 0; size_t over = 0; int maxd = 0;
  diff(out, bilinear, 2, mean, over, maxd);
  std::printf("  FSR x bilinear: media abs=%.2f, pixels com diferenca >2: %zu de %u, maximo=%d\n", mean, over, kOut * kOut, maxd);
  check(over > 100 && mean > 0.3, "saida do FSR 1 NAO e identica ao upscale bilinear");
  check(mean < 25.0, "saida do FSR 1 e parecida com o bilinear (orientacao e cores corretas)");

  // 2) Sem NaN/Inf nem preto: nenhum canal abaixo de 20 e alfa 255 em todos os pixels.
  size_t bad = 0;
  for (size_t i = 0; i < out.size(); i += 4) if (out[i] < 20 || out[i + 1] < 20 || out[i + 2] < 20 || out[i + 3] != 255) ++bad;
  std::printf("  pixels invalidos (NaN/preto/alfa): %zu\n", bad);
  check(bad == 0, "sem NaN: nenhum pixel preto nem com alfa errado");
  // brilho medio coerente com o do bilinear (EASU/RCAS preservam a energia)
  double sumO = 0, sumB = 0;
  for (size_t i = 0; i < out.size(); ++i) if ((i & 3) != 3) { sumO += out[i]; sumB += bilinear[i]; }
  std::printf("  brilho medio: FSR=%.2f bilinear=%.2f\n", sumO / (out.size() * 0.75), sumB / (bilinear.size() * 0.75));
  check(std::abs(sumO - sumB) / sumB < 0.03, "brilho medio do FSR 1 coerente com o do bilinear (+-3%)");

  // 3) RCAS: com nitidez 1 a imagem final difere da saida do EASU.
  diff(out, easu, 0, mean, over, maxd);
  std::printf("  RCAS(1.0) x EASU: media abs=%.3f, pixels diferentes=%zu, maximo=%d\n", mean, over, maxd);
  check(over > 0, "RCAS com nitidez 1 altera a imagem do EASU");

  // 4) Nitidez 0: o RCAS vira (quase) identidade.
  outTex = fsr.run(src, kIn, kIn, kOut, kOut, 0.0f);
  std::vector<u8> out0, easu0;
  const bool rd0 = !outTex.isNull() && dev->readTexture(outTex, out0, w, h) && dev->readTexture(fsr.easuTexture(), easu0, w2, h2);
  check(rd0, "segunda execucao (nitidez 0)");
  if (rd0) {
    diff(out0, easu0, 0, mean, over, maxd);
    std::printf("  RCAS(0.0) x EASU: maximo=%d\n", maxd);
    check(maxd <= 2, "RCAS com nitidez 0 nao altera o EASU (diferenca <= 2)");
  }

  std::printf(failures == 0 ? "RESULTADO: OK\n" : "RESULTADO: %d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
