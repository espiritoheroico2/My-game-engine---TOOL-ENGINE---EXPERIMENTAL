// ToolEngine - backend grafico real do RHI via Diligent Engine (Apache-2.0).
// Backends: Vulkan (todas as plataformas), OpenGL (desktop), D3D12 (so Windows).
// Os shaders entram como HLSL; o Diligent compila para SPIR-V/GLSL/DXIL conforme o backend.
#pragma once
#include "tool/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <vector>

namespace LightPrism { class IGraphicsDevice; }

namespace tool::rhi {

// Backends compilados neste binario (depende da plataforma e das opcoes do CMake).
std::vector<Backend> compiledBackends();

// Cria o dispositivo. Em falha devolve nullptr e preenche `error`.
std::unique_ptr<IDevice> createDiligentDevice(const DeviceInfo& info, std::string* error = nullptr);

// ---- Acesso nativo ao Vulkan do Diligent (para bibliotecas externas como o FidelityFX/FSR 4) ----
// Os handles saem como void* para nao vazar vulkan.h neste cabecalho. So funcionam num IDevice Diligent com backend Vulkan.
struct VulkanNative {
  void* instance = nullptr;        // VkInstance        (IRenderDeviceVk::GetVkInstance)
  void* physicalDevice = nullptr;  // VkPhysicalDevice  (IRenderDeviceVk::GetVkPhysicalDevice)
  void* device = nullptr;          // VkDevice          (IRenderDeviceVk::GetVkDevice)
};
// Preenche os handles Vulkan nativos. false se o dispositivo nao for Diligent/Vulkan.
bool vulkanNative(IDevice& dev, VulkanNative& out);
// VkImage da textura (nulo se invalida ou nao for Vulkan). Nao libere o handle.
void* vulkanImage(IDevice& dev, TextureId t);
// Formato VkFormat (como inteiro) da textura, 0 se desconhecido.
unsigned vulkanFormat(IDevice& dev, TextureId t);
// Transita a textura para `vkImageLayout` (VkImageLayout), gravando a barreira no contexto imediato.
bool vulkanTransition(IDevice& dev, TextureId t, int vkImageLayout);
// Envia os comandos pendentes (encerra qualquer render pass) e devolve o VkCommandBuffer limpo em gravacao do contexto
// imediato (nao guarde: vale ate o proximo submit). Nulo se nao for Vulkan.
void* vulkanCommandBuffer(IDevice& dev);
// Depois de gravar comandos por fora do Diligent, para CADA imagem tocada: anota o layout em que ela ficou e, se esse
// layout nao for SHADER_READ_ONLY_OPTIMAL (5), transita para ele (isso tambem faz o Diligent contar um comando e enviar
// o command buffer do FFX). Pelo menos uma imagem (a de saida, em GENERAL) deve passar por aqui.
void vulkanAfterExternal(IDevice& dev, TextureId t, int vkImageLayout);
// Encerra a gravacao externa: envia o command buffer e invalida o cache de estado do Diligent. Chamar depois de todos os
// vulkanAfterExternal.
void vulkanEndExternal(IDevice& dev);

// Dispositivo da Light Prism (clusters, cascatas e upload das luzes) sobre o dispositivo Diligent deste IDevice.
// Nulo se o IDevice nao for Diligent. Chamar no mesmo thread de render; o IDevice deve viver mais que o resultado.
std::unique_ptr<LightPrism::IGraphicsDevice> createLightPrismDevice(IDevice& dev);

} // namespace tool::rhi
