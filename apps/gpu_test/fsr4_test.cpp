// Teste com GPU real do FSR 4 (AMD FidelityFX SDK, ffx_api) sobre o device Vulkan do Diligent.
// Cria o contexto ffx, faz a consulta REAL de suporte ao driver e, se houver FSR 4, amplia um padrao 64x64 para 128x128
// (profundidade constante, motion vectors zero, jitter zero, reset=true) e confere:
//   - ffxDispatch devolve FFX_API_RETURN_OK;
//   - a saida nao tem NaN/Inf nem alfa invalido;
//   - a saida difere do upscale bilinear de referencia (o upscaler rodou de fato) mas se parece com ele.
// Se a GPU NAO suportar FSR 4, imprime "SKIP: FSR 4 nao suportado nesta GPU" e sai com 0.
// Uso: fsr4_test [vulkan]   (so Vulkan; offscreen sem janela)
#include "tool/fidelityfx/fidelityfx.hpp"
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

// Mesmo padrao do fsr1_test: borda diagonal forte (70 / 200) + gradientes em G (por linha) e B (por coluna).
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

// Limpa uma textura (render target) com uma cor constante: da estado conhecido ao Diligent e o valor de teste ao FSR.
void clearTexture(rhi::IDevice& dev, rhi::TextureId t, float r, float g, float b, float a) {
  auto cl = dev.beginCommands();
  cl->begin(t, kNullHandle, r, g, b, a);
  cl->end();
  dev.submit(*cl);
}

constexpr int kLayoutGeneral = 1;            // VK_IMAGE_LAYOUT_GENERAL
constexpr int kLayoutShaderReadOnly = 5;     // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

// Monta a NativeImage de uma textura do RHI (VkImage + formato + tamanho).
fidelityfx::NativeImage nativeImage(rhi::IDevice& dev, rhi::TextureId t, u32 w, u32 h) {
  fidelityfx::NativeImage im;
  im.image = rhi::vulkanImage(dev, t);
  im.width = w; im.height = h;
  im.vkFormat = rhi::vulkanFormat(dev, t);
  return im;
}

} // namespace

int main(int argc, char** argv) {
  const std::string which = argc > 1 ? argv[1] : "vulkan";
  if (which != "vulkan") { std::fprintf(stderr, "o FSR 4 deste teste so roda em Vulkan: %s\n", which.c_str()); return 2; }
  rhi::DeviceInfo info;
  info.width = 64; info.height = 64;
  info.backend = rhi::Backend::Vulkan;
  info.validation = argc > 2 && std::string(argv[2]) == "validate"; // 2o argumento opcional: liga as camadas de validacao do Vulkan

  std::string err;
  auto dev = rhi::createDiligentDevice(info, &err);
  if (!dev) { std::fprintf(stderr, "FALHA ao criar o dispositivo: %s\n", err.c_str()); return 1; }
  std::printf("backend=%s adaptador=%s\n", rhi::toString(dev->backend()), dev->deviceName().c_str());

  // ---- Etapa 1: handles nativos, contexto ffx e consulta real de suporte ----
  rhi::VulkanNative vn;
  if (!rhi::vulkanNative(*dev, vn)) { std::fprintf(stderr, "FALHA: nao consegui os handles Vulkan nativos do Diligent\n"); return 1; }
  std::printf("VkInstance=%p VkPhysicalDevice=%p VkDevice=%p\n", vn.instance, vn.physicalDevice, vn.device);

  constexpr u32 kIn = 64, kOut = 128;
  fidelityfx::VulkanHandles vh;
  vh.instance = vn.instance; vh.physicalDevice = vn.physicalDevice; vh.device = vn.device;
  const bool ready = fidelityfx::initVulkan(vh, kIn, kIn, kOut, kOut);
  std::printf("versoes de upscaler listadas pelo driver: %s\n", fidelityfx::allUpscalerVersions().empty() ? "(nenhuma)" : fidelityfx::allUpscalerVersions().c_str());
  std::printf("%s\n", fidelityfx::fsr4Status().c_str());
  std::printf("supportsFsr4()=%d\n", fidelityfx::supportsFsr4() ? 1 : 0);
  if (!ready || !fidelityfx::supportsFsr4()) {
    std::printf("SKIP: FSR 4 nao suportado nesta GPU\n");
    fidelityfx::shutdown();
    return 0;
  }

  // ---- Etapa 2: texturas de entrada e saida ----
  const std::vector<u8> pattern = makePattern(kIn);
  const rhi::TextureId color = dev->createTexture({kIn, kIn, 1, rhi::Format::RGBA8, false, false}, pattern.data());
  const rhi::TextureId depth = dev->createTexture({kIn, kIn, 1, rhi::Format::R32F, true, false}, nullptr);       // profundidade constante
  const rhi::TextureId mv = dev->createTexture({kIn, kIn, 1, rhi::Format::RGBA16F, true, false}, nullptr);      // motion vectors zero
  const rhi::TextureId out = dev->createTexture({kOut, kOut, 1, rhi::Format::RGBA16F, true, true}, nullptr);    // saida HDR (formato recomendado pelo FSR) com uso STORAGE
  check(!color.isNull() && !depth.isNull() && !mv.isNull() && !out.isNull(), "texturas de cor, profundidade, MV e saida criadas");
  if (color.isNull() || depth.isNull() || mv.isNull() || out.isNull()) { fidelityfx::shutdown(); return 1; }
  clearTexture(*dev, depth, 0.5f, 0.f, 0.f, 0.f);
  clearTexture(*dev, mv, 0.f, 0.f, 0.f, 0.f);
  clearTexture(*dev, out, 1.f, 0.f, 1.f, 1.f); // magenta: se a saida continuar magenta, o FSR nao escreveu nada

  {
    std::vector<u8> pre; u32 pw = 0, ph = 0;
    if (dev->readTexture(out, pre, pw, ph) && pre.size() >= 4) std::printf("  saida antes do FSR (pixel 0): %d %d %d %d\n", pre[0], pre[1], pre[2], pre[3]);
  }
  // Layouts que o FFX espera (o campo de estado de cada recurso diz isso a ele).
  const bool tr = rhi::vulkanTransition(*dev, color, kLayoutShaderReadOnly) && rhi::vulkanTransition(*dev, depth, kLayoutShaderReadOnly) &&
                  rhi::vulkanTransition(*dev, mv, kLayoutShaderReadOnly) && rhi::vulkanTransition(*dev, out, kLayoutGeneral);
  check(tr, "transicoes de layout (leitura nas entradas, GENERAL na saida)");

  fidelityfx::UpscalerConfig cfg;
  cfg.commandBuffer = rhi::vulkanCommandBuffer(*dev); // envia o pendente e entrega um command buffer limpo
  cfg.color = nativeImage(*dev, color, kIn, kIn);
  cfg.depth = nativeImage(*dev, depth, kIn, kIn);
  cfg.motionVectors = nativeImage(*dev, mv, kIn, kIn);
  cfg.output = nativeImage(*dev, out, kOut, kOut);
  cfg.renderWidth = kIn; cfg.renderHeight = kIn;
  cfg.outputWidth = kOut; cfg.outputHeight = kOut;
  cfg.jitterX = 0.f; cfg.jitterY = 0.f;
  cfg.reset = true; // primeiro quadro
  check(cfg.commandBuffer && cfg.color.image && cfg.depth.image && cfg.motionVectors.image && cfg.output.image, "handles nativos (VkCommandBuffer e 4 VkImage) obtidos");

  // Um quadro do upscaler: grava o ffxDispatch no command buffer do Diligent e devolve o controle ao Diligent.
  auto runFrame = [&](bool reset, bool first) {
    if (!first) { rhi::vulkanTransition(*dev, out, kLayoutGeneral); cfg.commandBuffer = rhi::vulkanCommandBuffer(*dev); }
    cfg.reset = reset;
    const std::uint32_t code = (argc > 2 && std::string(argv[2]) == "nodispatch") ? 0u : fidelityfx::dispatchUpscale(cfg); // "nodispatch": diagnostico do encanamento sem o FSR
    // O Diligent nao viu o que o FFX gravou: anota os layouts finais das imagens, envia o command buffer e invalida o cache.
    rhi::vulkanAfterExternal(*dev, color, kLayoutShaderReadOnly);
    rhi::vulkanAfterExternal(*dev, depth, kLayoutShaderReadOnly);
    rhi::vulkanAfterExternal(*dev, mv, kLayoutShaderReadOnly);
    rhi::vulkanAfterExternal(*dev, out, kLayoutGeneral);
    rhi::vulkanEndExternal(*dev);
    return code;
  };
  const std::uint32_t rc = runFrame(true, true); // primeiro quadro: reset
  std::printf("  ffxDispatch devolveu codigo %u\n", rc);
  check(rc == 0, "ffxDispatch retornou FFX_API_RETURN_OK");
  // Quadros extras (sem reset): o resultado precisa estar estavel; o primeiro quadro de um upscaler temporal pode vir incompleto.
  for (int f = 1; f < 4 && rc == 0; ++f) {
    const std::uint32_t c2 = runFrame(false, false);
    std::vector<u8> tmp; u32 tw = 0, th = 0;
    if (c2 == 0 && dev->readTexture(out, tmp, tw, th) && tmp.size() >= 4) std::printf("  quadro %d: codigo %u, saida (pixel 0): %d %d %d %d\n", f + 1, c2, tmp[0], tmp[1], tmp[2], tmp[3]);
    else std::printf("  quadro %d: codigo %u\n", f + 1, c2);
  }

  // ---- Verificacoes da saida ----
  std::vector<u8> res; u32 w = 0, h = 0;
  {
    // Diagnostico: a entrada de cor precisa ter chegado intacta a GPU (se vier preta, o problema e o upload, nao o FSR).
    std::vector<u8> cin; u32 cw = 0, ch = 0;
    if (dev->readTexture(color, cin, cw, ch) && cin.size() >= 4)
      std::printf("  entrada de cor (pixel 0): %d %d %d %d\n", cin[0], cin[1], cin[2], cin[3]);
  }
  const bool rd = dev->readTexture(out, res, w, h);
  check(rd && w == kOut && h == kOut, "leitura da saida: 128x128");
  if (rd && res.size() >= 4) std::printf("  saida (pixel 0): %d %d %d %d\n", res[0], res[1], res[2], res[3]);
  if (rd && w == kOut && h == kOut) {
    std::vector<u8> bilinear(static_cast<size_t>(kOut) * kOut * 4);
    upscaleRgba(pattern.data(), kIn, kIn, bilinear.data(), kOut, kOut, UpscaleFilter::Bilinear);
    double mean = 0; size_t over = 0; int maxd = 0;
    const size_t n = res.size() / 4;
    for (size_t i = 0; i < n; ++i) {
      int m = 0;
      for (int c = 0; c < 3; ++c) { const int d = std::abs(int(res[i * 4 + c]) - int(bilinear[i * 4 + c])); mean += d; m = std::max(m, d); }
      if (m > 2) ++over;
      maxd = std::max(maxd, m);
    }
    mean /= static_cast<double>(n * 3);
    std::printf("  FSR 4 x bilinear: media abs=%.2f, pixels com diferenca >2: %zu de %u, maximo=%d\n", mean, over, kOut * kOut, maxd);
    // Alfa e pixels pretos (um NaN escrito num alvo UNORM vira 0; o padrao de entrada nao tem canal abaixo de 60).
    // O FSR 4 nao escreve alfa util (medido: 0 em todos os pixels); o que importa e o alfa ser uniforme (sem lixo/NaN).
    size_t badAlpha = 0, black = 0;
    for (size_t i = 0; i < res.size(); i += 4) {
      if (res[i + 3] != res[3]) ++badAlpha;
      if (res[i] < 20 && res[i + 1] < 20 && res[i + 2] < 20) ++black;
    }
    std::printf("  alfa da saida: %d (uniforme: %s), pixels pretos: %zu\n", res[3], badAlpha == 0 ? "sim" : "NAO", black);
    check(black == 0, "sem NaN: nenhum pixel preto na saida");
    check(badAlpha == 0, "alfa uniforme em todos os pixels (sem lixo)");
    check(over > 100 && mean > 0.3, "saida do FSR 4 NAO e identica ao upscale bilinear (o upscaler rodou)");
    check(mean < 40.0, "saida do FSR 4 e parecida com o bilinear (orientacao e cores corretas)");
  }

  fidelityfx::shutdown();
  std::printf(failures == 0 ? "RESULTADO: OK\n" : "RESULTADO: %d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
