// ToolEngine - RHI: interface unica de GPU. Backends: Diligent (Vulkan/DX12/WebGPU/GLES)
// e Null (testes/servidor). Shaders sao HLSL, compilados offline para cada alvo.
#pragma once
#include "tool/core/handle.hpp"
#include "tool/core/types.hpp"
#include "tool/debug/debug.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tool::rhi {

enum class Backend : u8 { Null, Vulkan, D3D12, WebGPU, GLES, Count };
enum class Platform : u8 { Windows, Linux, Android, Web, UWP, MacOS, Count };

inline const char* toString(Backend b) {
  static const char* n[] = {"null", "vulkan", "d3d12", "webgpu", "gles"};
  return n[static_cast<int>(b)];
}

// Ordem de preferencia por plataforma (secao de multi-API). `available` diz o que o dispositivo suporta.
inline std::vector<Backend> backendPriority(Platform p) {
  switch (p) {
  case Platform::Windows: return {Backend::D3D12, Backend::Vulkan, Backend::GLES};
  case Platform::UWP: return {Backend::D3D12};
  case Platform::Linux: return {Backend::Vulkan, Backend::GLES};
  case Platform::Android: return {Backend::Vulkan, Backend::GLES};
  case Platform::Web: return {Backend::WebGPU, Backend::GLES}; // GLES == WebGL2
  case Platform::MacOS: return {Backend::Vulkan}; // via MoltenVK
  default: return {};
  }
}

inline Backend chooseBackend(Platform p, const std::vector<Backend>& available, Backend forced = Backend::Count) {
  if (forced != Backend::Count) {
    for (Backend a : available)
      if (a == forced) return forced;
    debug().report("TOOL-W120", Severity::Warning, std::string("backend forcado indisponivel: ") + toString(forced));
  }
  for (Backend b : backendPriority(p))
    for (Backend a : available)
      if (a == b) return b;
  return Backend::Null; // fail-safe: roda sem GPU
}

enum class Format : u8 { RGBA8, RGBA8_SRGB, BGRA8, RGBA16F, R32F, D32F, D24S8 };
enum class CullMode : u8 { None, Back, Front };
enum class BufferUsage : u8 { Vertex, Index, Uniform, Storage };
enum class ShaderStage : u8 { Vertex, Pixel, Compute };

struct BufferDesc { size_t size = 0; BufferUsage usage = BufferUsage::Vertex; bool dynamic = false; };
struct TextureDesc { u32 width = 1, height = 1, mips = 1; Format format = Format::RGBA8; bool renderTarget = false; };
// `source` = HLSL (backends reais compilam para SPIR-V/GLSL/DXIL); `bytecode` = binario ja compilado (opcional).
enum class ShaderLang : u8 { HLSL, GLSL };
struct ShaderDesc { ShaderLang lang = ShaderLang::HLSL; ShaderStage stage = ShaderStage::Vertex; std::string entry = "main"; std::string name; std::string source; std::vector<u8> bytecode; };
// Atributo de vertice na ordem da entrada do vertex shader (location 0, 1, 2...). `components` = numero de floats.
struct VertexAttrib { u32 components = 3; u32 offset = 0; };
struct PipelineDesc {
  Handle vs, ps;
  bool depthTest = true, depthWrite = true, blend = false;
  Format color = Format::RGBA8_SRGB, depth = Format::D32F;
  CullMode cull = CullMode::Back;
  std::vector<VertexAttrib> layout; u32 vertexStride = 0;
  std::vector<std::string> uniformNames; // slot i -> nome do cbuffer no HLSL
  std::vector<std::string> textureNames; // slot i -> nome da textura (Texture2D) no shader de pixel; lida com Load/texelFetch (sem sampler)
  std::string name;
};

using BufferId = Handle;
using TextureId = Handle;
using ShaderId = Handle;
using PipelineId = Handle;

struct DrawStats { u32 draws = 0, triangles = 0, pipelineSwitches = 0, bufferUploads = 0; };

// Lista de comandos gravavel; o backend a executa. Mantida simples de proposito.
class ICommandList {
public:
  virtual ~ICommandList() = default;
  virtual void begin(TextureId target, TextureId depth, f32 r, f32 g, f32 b, f32 a) = 0; // depth pode ser nulo
  virtual void setPipeline(PipelineId p) = 0;
  virtual void setVertexBuffer(BufferId b) = 0;
  virtual void setIndexBuffer(BufferId b) = 0;
  virtual void setUniform(u32 slot, BufferId b) = 0;
  virtual void setTexture(u32 slot, TextureId t) = 0; // textura criada com renderTarget=true pode ser lida no shader (ex.: mapa de sombra)
  virtual void drawIndexed(u32 indexCount, u32 instances) = 0;
  virtual void end() = 0;
};

class IDevice {
public:
  virtual ~IDevice() = default;
  virtual Backend backend() const = 0;
  virtual BufferId createBuffer(const BufferDesc&, const void* data) = 0;
  virtual TextureId createTexture(const TextureDesc&, const void* data) = 0;
  virtual ShaderId createShader(const ShaderDesc&) = 0;
  virtual PipelineId createPipeline(const PipelineDesc&) = 0;
  virtual void destroy(Handle h) = 0;
  virtual void updateBuffer(BufferId b, const void* data, size_t size) = 0;
  virtual std::unique_ptr<ICommandList> beginCommands() = 0;
  virtual void submit(ICommandList& cl) = 0;
  virtual void present() = 0;
  // Backbuffer da janela (nulo se o dispositivo for offscreen) e leitura de textura para a CPU (RGBA8, origem no topo-esquerdo).
  virtual TextureId backbuffer() { return kNullHandle; }
  virtual bool resizeSwapchain(u32, u32) { return false; }
  virtual bool readTexture(TextureId, std::vector<u8>&, u32&, u32&) { return false; }
  virtual std::string deviceName() const { return "null"; }
  virtual DrawStats frameStats() const = 0;
};

// Parametros de criacao de um dispositivo real. nativeWindow = HWND / X11 Window (+ display) / nullptr (offscreen).
struct DeviceInfo {
  Backend backend = Backend::Null;
  void* nativeWindow = nullptr;   // janela nativa (Windows: HWND; X11: Window id como ponteiro)
  void* nativeDisplay = nullptr;  // X11: Display*
  u32 width = 1280, height = 720;
  bool validation = true;
};

} // namespace tool::rhi
