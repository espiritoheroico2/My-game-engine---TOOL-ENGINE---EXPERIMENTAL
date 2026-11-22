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

// Backend grafico concreto escolhido em tempo de execucao (Null = sem GPU).
enum class Backend : u8 { Null, Vulkan, D3D12, WebGPU, GLES, D3D11, Count };
// Plataforma-alvo; define a ordem de preferencia dos backends (ver backendPriority).
enum class Platform : u8 { Windows, Linux, Android, Web, UWP, MacOS, Count };

// Nome curto do backend (vulkan, d3d12, ...), para logs e diagnosticos.
inline const char* toString(Backend b) {
  static const char* n[] = {"null", "vulkan", "d3d12", "webgpu", "gles", "d3d11"};
  return n[static_cast<int>(b)];
}

// Ordem de preferencia por plataforma (secao de multi-API). `available` diz o que o dispositivo suporta.
inline std::vector<Backend> backendPriority(Platform p) {
  switch (p) {
  case Platform::Windows: return {Backend::D3D12, Backend::D3D11, Backend::Vulkan, Backend::GLES};
  case Platform::UWP: return {Backend::D3D12, Backend::D3D11};
  case Platform::Linux: return {Backend::Vulkan, Backend::GLES};
  case Platform::Android: return {Backend::Vulkan, Backend::GLES};
  case Platform::Web: return {Backend::WebGPU, Backend::GLES}; // GLES == WebGL2
  case Platform::MacOS: return {Backend::Vulkan}; // via MoltenVK
  default: return {};
  }
}

// Escolhe o backend: se forced estiver em available, usa-o; senao segue backendPriority(p).
// forced = Count significa sem preferencia. Forcado indisponivel gera aviso (TOOL-W120). Sem nada, devolve Null.
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

// Formatos de textura do RHI: cor (RGBA/BGRA), ponto flutuante e profundidade.
enum class Format : u8 { RGBA8, RGBA8_SRGB, BGRA8, RGBA16F, R32F, D32F, D24S8 };
// Descarte de faces: None, Back (padrao) ou Front.
enum class CullMode : u8 { None, Back, Front };
// Uso de um buffer: vertices, indices, uniform (cbuffer) ou storage (StructuredBuffer).
enum class BufferUsage : u8 { Vertex, Index, Uniform, Storage };
// Estagio do shader: vertex, pixel (fragment) ou compute.
enum class ShaderStage : u8 { Vertex, Pixel, Compute };

// Descricao de um buffer: tamanho em bytes, uso, se muda por quadro e stride (storage).
struct BufferDesc { size_t size = 0; BufferUsage usage = BufferUsage::Vertex; bool dynamic = false; u32 elementStride = 0; };
// Descricao de uma textura 2D: dimensoes, niveis de mip, formato e se e alvo de renderizacao.
struct TextureDesc { u32 width = 1, height = 1, mips = 1; Format format = Format::RGBA8; bool renderTarget = false; };
// `source` = HLSL (backends reais compilam para SPIR-V/GLSL/DXIL); `bytecode` = binario ja compilado (opcional).
enum class ShaderLang : u8 { HLSL, GLSL };
// Descricao de um shader: codigo HLSL/GLSL (ou bytecode), estagio e nome da funcao de entrada.
struct ShaderDesc { ShaderLang lang = ShaderLang::HLSL; ShaderStage stage = ShaderStage::Vertex; std::string entry = "main"; std::string name; std::string source; std::vector<u8> bytecode; };
// Atributo de vertice na ordem da entrada do vertex shader (location 0, 1, 2...). `components` = numero de floats.
struct VertexAttrib { u32 components = 3; u32 offset = 0; };
// Estado fixo do pipeline grafico: shaders vs/ps, depth, blend, cull, layout de vertices e nomes dos slots.
struct PipelineDesc {
  Handle vs, ps;
  bool depthTest = true, depthWrite = true, blend = false;
  // Blend premultiplicado (ONE, INV_SRC_ALPHA) para UI e sprites com cor ja multiplicada por alfa. Tem prioridade sobre `blend`.
  bool blendPremultiplied = false;
  Format color = Format::RGBA8_SRGB, depth = Format::D32F;
  CullMode cull = CullMode::Back;
  std::vector<VertexAttrib> layout; u32 vertexStride = 0;
  std::vector<std::string> uniformNames; // slot i -> nome do cbuffer no HLSL
  std::vector<std::string> textureNames; // slot i -> nome da textura (Texture2D) no shader de pixel; lida com Load/texelFetch (sem sampler)
  std::vector<std::string> storageNames; // slot i -> nome de um StructuredBuffer somente leitura (SRV) em VS e PS (setStorage)
  std::string name;
};

using BufferId = Handle;
using TextureId = Handle;
using ShaderId = Handle;
using PipelineId = Handle;

// Contadores de um quadro (draws, triangulos, trocas de pipeline, uploads, submits). Lidos por frameStats().
struct DrawStats {
  u32 draws = 0, triangles = 0, pipelineSwitches = 0, bufferUploads = 0;
  u32 submitCount = 0, flushCount = 0;
};

// Diferenca campo a campo entre dois instantaneos de DrawStats (after - before).
inline DrawStats drawStatsDelta(const DrawStats& after, const DrawStats& before) {
  return {
    after.draws - before.draws,
    after.triangles - before.triangles,
    after.pipelineSwitches - before.pipelineSwitches,
    after.bufferUploads - before.bufferUploads,
    after.submitCount - before.submitCount,
    after.flushCount - before.flushCount
  };
}

// Lista de comandos gravavel; o backend a executa. Mantida simples de proposito.
class ICommandList {
public:
  // Destrutor virtual: a lista e destruida pela interface.
  virtual ~ICommandList() = default;
  // Abre a lista limpando o alvo com a cor (r, g, b, a). Depth pode ser nulo.
  virtual void begin(TextureId target, TextureId depth, f32 r, f32 g, f32 b, f32 a) = 0; // depth pode ser nulo
  // Troca o alvo sem limpar nada (para passes que reaproveitam a profundidade). depth pode ser nulo.
  virtual void setTargets(TextureId, TextureId) {}
  // Abre a lista SEM limpar o alvo (passes extras, como o transparente): equivale a begin() sem clear.
  virtual void beginKeep(TextureId target, TextureId depth) = 0;
  // Limpa somente o depth target atual, preservando a cor. depth pode ser nulo apenas se este metodo nao for chamado.
  virtual void clearDepth(f32 depth = 1.f) = 0;
  // Define o pipeline (shaders e estado fixo) para os proximos draws.
  virtual void setPipeline(PipelineId p) = 0;
  // Liga o buffer de vertices (slot 0).
  virtual void setVertexBuffer(BufferId b) = 0;
  // Liga o buffer de indices (inteiros de 32 bits).
  virtual void setIndexBuffer(BufferId b) = 0;
  // Liga um buffer uniforme ao slot; o nome vem de PipelineDesc::uniformNames (chamar apos setPipeline).
  virtual void setUniform(u32 slot, BufferId b) = 0;
  // Liga uma textura ao slot de PipelineDesc::textureNames.
  virtual void setTexture(u32 slot, TextureId t) = 0; // textura criada com renderTarget=true pode ser lida no shader (ex.: mapa de sombra)
  // Viewport (e scissor) dentro do alvo atual, em pixels. begin() ja define o alvo inteiro; use isto para desenhar
  // varias faces num alvo empilhado (mapa de sombra de cubo em uma textura R x 6R). Chamar depois de begin().
  virtual void setViewport(u32 x, u32 y, u32 w, u32 h) = 0;
  // Buffer estruturado somente leitura (StructuredBuffer) no slot de PipelineDesc::storageNames, ligado em VS e PS.
  virtual void setStorage(u32 /*slot*/, BufferId /*b*/) {}
  // Desenha indexCount indices com `instances` instancias, a partir do inicio do index buffer.
  virtual void drawIndexed(u32 indexCount, u32 instances) = 0;
  // Recorte (scissor) em pixels do alvo atual. Padrao depois de begin()/setViewport: alvo inteiro.
  virtual void setScissor(u32 x, u32 y, u32 w, u32 h) = 0;
  // Desenha a partir de um deslocamento do index buffer (firstIndex), sem base de vertice. Usado para desenhar
  // lotes diferentes de um mesmo buffer dentro de um quadro.
  virtual void drawIndexedRange(u32 firstIndex, u32 indexCount, u32 instances = 1) = 0;
  // Fecha a lista. Depois disso ela pode ser enviada com IDevice::submit().
  virtual void end() = 0;
};

// Interface do dispositivo de GPU: cria recursos, grava listas de comandos, controla quadros e estatisticas.
// Implementacoes: DiligentDevice (GPU real) e NullDevice (testes e servidor).
class IDevice {
public:
  // Destrutor virtual: libera os recursos do backend.
  virtual ~IDevice() = default;
  // Backend concreto deste dispositivo.
  virtual Backend backend() const = 0;
  // Cria um buffer; data pode ser nulo. Devolve handle nulo em falha.
  virtual BufferId createBuffer(const BufferDesc&, const void* data) = 0;
  // Cria uma textura; data (RGBA8, nivel 0) pode ser nulo. Devolve handle nulo em falha.
  virtual TextureId createTexture(const TextureDesc&, const void* data) = 0;
  // Compila um shader a partir do codigo ou do bytecode. Handle nulo se a compilacao falhar.
  virtual ShaderId createShader(const ShaderDesc&) = 0;
  // Cria o pipeline a partir dos shaders vs e ps ja criados. Handle nulo se algum for invalido.
  virtual PipelineId createPipeline(const PipelineDesc&) = 0;
  // Libera o recurso do handle. Handle obsoleto gera aviso e nao faz nada.
  virtual void destroy(Handle h) = 0;
  // Escreve `size` bytes de data no inicio do buffer. size nao pode passar do tamanho do buffer.
  virtual void updateBuffer(BufferId b, const void* data, size_t size) = 0;
  // Cria uma lista de comandos nova para gravar um lote de draws.
  virtual std::unique_ptr<ICommandList> beginCommands() = 0;
  // Envia para a GPU uma lista ja fechada com end().
  virtual void submit(ICommandList& cl) = 0;
  // Delimita o estatistico de um frame real. beginFrame() inicia a acumulacao; endFrame() finaliza,
  // apresenta (quando aplicavel) e publica frameStats() sem apagar os dados das vistas.
  virtual void beginFrame() {}
  // Finaliza o quadro. Por padrao so chama present().
  virtual void endFrame() { present(); }
  // Apresenta o quadro (swapchain) e fecha as estatisticas do frame.
  virtual void present() = 0;
  // Backbuffer da janela (nulo se o dispositivo for offscreen) e leitura de textura para a CPU (RGBA8, origem no topo-esquerdo).
  virtual TextureId backbuffer() { return kNullHandle; }
  // Redimensiona a swapchain. Devolve false se o dispositivo for offscreen.
  virtual bool resizeSwapchain(u32, u32) { return false; }
  // Le a textura para a CPU como RGBA8, esperando a GPU. rgba recebe os pixels; w e h, as dimensoes.
  // Devolve false em falha.
  virtual bool readTexture(TextureId, std::vector<u8>&, u32&, u32&) { return false; }
  // Leitura sem esperar a GPU (para o editor): copia o alvo para um slot e devolve o quadro mais recente que a GPU
  // ja terminou. Retorna 1 = `rgba` recebeu um quadro novo (w, h preenchidos); 0 = ainda nao ha quadro novo, `rgba`
  // fica como estava; -1 = falha. Sem suporte, cai na leitura sincrona.
  virtual int readTextureAsync(TextureId t, int slot, std::vector<u8>& rgba, u32& w, u32& h) {
    (void)slot; return readTexture(t, rgba, w, h) ? 1 : -1;
  }
  // Nome do adaptador (GPU) em uso, para diagnostico.
  virtual std::string deviceName() const { return "null"; }
  // Importa um buffer criado por outra biblioteca na mesma GPU (ex.: IBuffer do Diligent da Light Prism). O handle NAO
  // e dono do recurso: o chamador continua responsavel pelo buffer original. Nulo se o backend nao suporta.
  virtual BufferId importBuffer(void* /*nativeBuffer*/) { return kNullHandle; }
  // Dispositivo e contexto imediato nativos (Diligent: IRenderDevice* e IDeviceContext*), para bibliotecas que falam com a API direto.
  virtual void* nativeDevice() const { return nullptr; }
  // Contexto imediato nativo (Diligent: IDeviceContext*). Nulo fora do backend Diligent.
  virtual void* nativeContext() const { return nullptr; }
  // Durante o quadro, as estatisticas acumuladas; depois de present(), as do ultimo quadro.
  virtual DrawStats frameStats() const = 0;
};

// Tamanho do "dynamic heap" do Vulkan (MiB). O Diligent usa 8 MB por padrao, pequeno demais para os buffers
// uniformes por objeto do editor ("Space in dynamic heap is exhausted"). Configuravel no CMake:
// -DTOOL_DYNAMIC_HEAP_MB=128. (OpenGL e D3D12 nao expoem um tamanho total no Diligent.)
#ifndef TOOL_DYNAMIC_HEAP_MB
#define TOOL_DYNAMIC_HEAP_MB 64
#endif

// Parametros de criacao de um dispositivo real. nativeWindow = HWND / X11 Window (+ display) / nullptr (offscreen).
struct DeviceInfo {
  Backend backend = Backend::Null;
  void* nativeWindow = nullptr;   // janela nativa (Windows: HWND; X11: Window id como ponteiro)
  void* nativeDisplay = nullptr;  // X11: Display*
  u32 width = 1280, height = 720;
  bool validation = false;        // camadas de validacao (Vulkan/D3D12) e depuracao GL. Custam CPU: ligue so para depurar
  u32 dynamicHeapMB = TOOL_DYNAMIC_HEAP_MB;
};

} // namespace tool::rhi
