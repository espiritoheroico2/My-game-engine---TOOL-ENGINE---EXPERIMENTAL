// ToolEngine - backend grafico real do RHI via Diligent Engine (Apache-2.0).
// Backends: Vulkan (todas as plataformas), OpenGL (desktop), D3D12 (so Windows).
// Os shaders entram como HLSL; o Diligent compila para SPIR-V/GLSL/DXIL conforme o backend.
#pragma once
#include "tool/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tool::rhi {

// Backends compilados neste binario (depende da plataforma e das opcoes do CMake).
std::vector<Backend> compiledBackends();

// Cria o dispositivo. Em falha devolve nullptr e preenche `error`.
std::unique_ptr<IDevice> createDiligentDevice(const DeviceInfo& info, std::string* error = nullptr);

} // namespace tool::rhi
