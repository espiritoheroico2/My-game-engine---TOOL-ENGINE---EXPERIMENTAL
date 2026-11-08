// ToolEngine - ponte entre o RHI Diligent e o backend Diligent da Light Prism (third_party/LightPrism).
// Compila so com TOOL_WITH_DILIGENT. A Light Prism usa o MESMO dispositivo e o MESMO contexto imediato do RHI,
// entao as dispatches de clusters e as leituras dos shaders de iluminacao ficam em ordem na mesma fila.
#include "tool/rhi/diligent_device.hpp"

#include "LightPrism/Backends/DiligentDevice.hpp"

namespace tool::rhi {

std::unique_ptr<LightPrism::IGraphicsDevice> createLightPrismDevice(IDevice& dev) {
  auto* device = static_cast<Diligent::IRenderDevice*>(dev.nativeDevice());
  auto* context = static_cast<Diligent::IDeviceContext*>(dev.nativeContext());
  if (!device || !context) return nullptr; // backend sem Diligent (Null): a iluminacao GPU nao existe
  return LightPrism::CreateDiligentDevice(device, context);
}

} // namespace tool::rhi
