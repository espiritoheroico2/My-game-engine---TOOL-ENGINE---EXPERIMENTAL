#pragma once
// Backend Diligent Engine. Só é compilado com -DLP_ENABLE_DILIGENT=ON.
// Os ponteiros pertencem à engine; devem sobreviver ao IGraphicsDevice retornado.
#include <memory>

#include "LightPrism/Rhi.hpp"

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
} // namespace Diligent

namespace LightPrism {

struct DiligentDeviceOptions {
    // Diligent não distingue GPUs mobile (tile-based) com segurança; GLES é detectado
    // automaticamente. Para Vulkan/Metal em Android/iOS, force aqui.
    bool mobileClass = false;
};

// `context` deve ser o contexto imediato e só pode ser usado na thread de render
// (IGpuBuffer::Update chama IDeviceContext::UpdateBuffer).
std::unique_ptr<IGraphicsDevice> CreateDiligentDevice(Diligent::IRenderDevice* device,
                                                      Diligent::IDeviceContext* context,
                                                      const DiligentDeviceOptions& options = {});

} // namespace LightPrism
