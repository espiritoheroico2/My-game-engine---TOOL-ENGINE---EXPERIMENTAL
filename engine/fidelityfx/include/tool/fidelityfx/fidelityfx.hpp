#pragma once
// FidelityFX opcional (FSR 4 e frame generation do FSR 3.1).
//
// O SDK da AMD fica em third_party/fidelityfx/ (fora do git). Se a pasta existir com
// include/ffx_api.hpp, o CMake define TOOL_WITH_FIDELITYFX=1; senao define 0 e a build
// segue normalmente. Nenhum include de ffx_* pode aparecer fora de #if TOOL_WITH_FIDELITYFX.
//
// ESTADO (rodada 2): contexto ffx_api de upscale sobre o device Vulkan nativo do Diligent + consulta real de
// suporte ao driver (ffxQuery GET_VERSIONS) + dispatch do upscaler. NAO esta ligado no renderer/editor ainda.
//
// Caminho dos handles Vulkan (documentado aqui de proposito):
//   tool::rhi::vulkanNative(IDevice&, VulkanNative&)   [tool/rhi/diligent_device.hpp]
//     -> IRenderDevice::QueryInterface(IID_RenderDeviceVk) -> IRenderDeviceVk::GetVkInstance/GetVkPhysicalDevice/GetVkDevice
//   VkImage de cada textura: tool::rhi::vulkanImage()  (ITextureVk::GetVkImage)
//   VkCommandBuffer:         tool::rhi::vulkanCommandBuffer() (IDeviceContextVk::GetVkCommandBuffer)
// vkGetDeviceProcAddr vem de vulkan-1.dll (o mesmo loader que o Diligent usa).
//
// Uso tipico (um quadro):
//   initVulkan(handles, ...)                       // uma vez; consulta o driver e cria o contexto se houver FSR 4
//   if (supportsFsr4()) { ...transitar imagens para os layouts certos...; dispatchUpscale(d); }
//   shutdown();
//
// PENDENTE (proxima rodada): motion vectors reais, jitter Halton (ver queryJitterOffset), reset em corte de camera,
// frame generation (FSR 3.1 FG, outro contexto) e ligar no renderer/editor.

#ifndef TOOL_WITH_FIDELITYFX
#define TOOL_WITH_FIDELITYFX 0
#endif

#include <cstdint>
#include <string>

namespace tool::fidelityfx {

// Handles Vulkan nativos (VkInstance / VkPhysicalDevice / VkDevice) como ponteiros opacos.
struct VulkanHandles {
  void* instance = nullptr;
  void* physicalDevice = nullptr;
  void* device = nullptr;
};

// Imagem Vulkan que entra ou sai do upscaler.
struct NativeImage {
  void* image = nullptr;          // VkImage
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t vkFormat = 0;     // VkFormat como inteiro (ex.: 37 = R8G8B8A8_UNORM, 100 = R32_SFLOAT)
};

// Um despacho do upscaler. As imagens ja devem estar nos layouts: color/depth/motionVectors em
// SHADER_READ_ONLY_OPTIMAL e output em GENERAL (o FFX trata estas transicoes pelo campo de estado).
struct UpscalerConfig {
  void* commandBuffer = nullptr;  // VkCommandBuffer em gravacao
  NativeImage color;              // cor da cena em resolucao de render (antes do UI)
  NativeImage depth;              // profundidade (R32_FLOAT) em resolucao de render
  NativeImage motionVectors;      // vetores de movimento em resolucao de render
  NativeImage output;             // alvo em resolucao de saida (precisa de uso STORAGE)
  float jitterX = 0.f;            // jitter sub-pixel da camera neste quadro (em pixels)
  float jitterY = 0.f;
  float motionScaleX = 1.f;       // fator para levar o MV a pixels (1,1 = MV ja em pixels; renderSize = MV em UV)
  float motionScaleY = 1.f;
  std::uint32_t renderWidth = 0;  // resolucao interna
  std::uint32_t renderHeight = 0;
  std::uint32_t outputWidth = 0;  // resolucao de saida
  std::uint32_t outputHeight = 0;
  float deltaTimeMs = 16.6f;      // tempo do quadro
  float cameraNear = 0.1f;
  float cameraFar = 100.f;
  float cameraFovY = 1.0f;        // radianos
  bool reset = false;             // true no primeiro quadro / corte de camera
};

// Carrega o loader (amd_fidelityfx_loader_vk.dll), consulta o driver e, se houver FSR 4, cria o contexto de upscale.
// Devolve true se o FSR 4 esta pronto. Em false, fsr4Status() traz o motivo exato. Pode ser chamado de novo (recria).
// nonLinearColor = a cor de entrada e perceptual (sRGB, ex.: buffer LDR RGBA8). IMPORTANTE (medido na RX 6500 XT): com
// cor LDR declarada como linear o FSR 4 devolve tudo zero; passe false so se a cor for linear/HDR (ex.: RGBA16F).
bool initVulkan(const VulkanHandles& h, std::uint32_t maxRenderW, std::uint32_t maxRenderH,
                std::uint32_t maxOutputW, std::uint32_t maxOutputH, bool nonLinearColor = true);
// Destroi o contexto e zera o estado (supportsFsr4() volta a false).
void shutdown();

// true somente se a consulta real ao driver devolveu pelo menos uma versao de FSR 4 E o contexto foi criado.
// Sem contexto (initVulkan nao chamado ou falhou) devolve false.
bool supportsFsr4();
// Texto de uma linha com o resultado: "FSR 4: suportado nesta GPU (versao X)" ou o motivo.
const std::string& fsr4Status();
// Nome da versao de upscaler em uso (vazio se nao ha contexto).
const std::string& fsr4Version();
// Todas as versoes de upscaler que o driver listou, separadas por virgula (diagnostico; vazio se a consulta nao rodou).
const std::string& allUpscalerVersions();
// true se o frame generation esta disponivel. AINDA NAO CONSULTADO nesta fase: sempre false.
bool supportsFrameGen();

// Offset de jitter para o indice da sequencia (consulta do contexto; false sem contexto).
bool queryJitterOffset(int index, int phaseCount, float& x, float& y);
// Quantidade de fases de jitter para a razao render->saida (false sem contexto).
bool queryJitterPhaseCount(std::uint32_t renderWidth, std::uint32_t outputWidth, int& count);

// Grava o upscale no VkCommandBuffer dado. Devolve o codigo ffx (0 = FFX_API_RETURN_OK); 0xFFFFFFFF sem contexto.
std::uint32_t dispatchUpscale(const UpscalerConfig& c);

} // namespace tool::fidelityfx
