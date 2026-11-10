#pragma once
// Abstração mínima de API gráfica (RHI). Cada backend (Diligent, Vulkan, D3D12, GL, Metal...)
// implementa IGraphicsDevice. O núcleo da Light Prism só conhece estas interfaces.
//
// Escopo: buffers (SRV/UAV/CBV/readback), pipelines de compute, dispatch, barreiras UAV e cópia.
// Não há gráficos (draw) aqui: o passe de iluminação continua sendo da engine hospedeira.
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace LightPrism {

enum class BufferKind : uint8_t {
    Structured,    // StructuredBuffer (SRV); atualizado pela CPU via Update()
    StructuredRW,  // RWStructuredBuffer (UAV) + SRV; escrito pela GPU. Update() permitido (inicialização)
    Uniform,       // constant buffer; tamanho múltiplo de 16
    Readback,      // staging CPU-legível; destino de CopyBuffer, lido com ReadBack()
};

// Como um buffer é visto pelo shader num binding.
enum class BindUsage : uint8_t {
    Constant,   // cbuffer
    ReadOnly,   // StructuredBuffer<T>   (SRV)
    ReadWrite,  // RWStructuredBuffer<T> (UAV)
};

struct BufferDesc {
    uint64_t sizeBytes = 0;
    uint32_t strideBytes = 0; // obrigatório para Structured / StructuredRW
    BufferKind kind = BufferKind::Structured;
    const char* debugName = "";
};

class IGpuBuffer {
public:
    virtual ~IGpuBuffer() = default;
    virtual const BufferDesc& Desc() const = 0;
    // Copia [data, data+size) para offset. Retorna false se fora dos limites, tipo inválido (Readback) ou falha.
    virtual bool Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;
    // Só para BufferKind::Readback. Copia o conteúdo para `dst`. Retorna false se não for Readback, se
    // estiver fora dos limites ou se o dado ainda não estiver disponível. O chamador deve esperar a GPU
    // terminar o CopyBuffer (tipicamente >= frames em voo depois) antes de chamar.
    virtual bool ReadBack(void* dst, uint64_t size, uint64_t offset = 0) = 0;
    // Handle nativo (ex.: Diligent::IBuffer*, VkBuffer, ID3D12Resource*), para o backend/engine.
    virtual void* NativeHandle() const = 0;
};

struct ComputePipelineDesc {
    const char* debugName = "";
    // Caminho do .hlsl relativo a shaderDirectory (ex.: "Cluster/ClusterCullCount.compute.hlsl").
    const char* shaderFile = "";
    const char* entryPoint = "CSMain";
    // Raiz de busca dos shaders. #include relativos (ex.: "../Common/Cluster.hlsli") resolvem a partir dela.
    const char* shaderDirectory = "";
};

class IComputePipeline {
public:
    virtual ~IComputePipeline() = default;
    virtual const ComputePipelineDesc& Desc() const = 0;
    // Associa um buffer à variável do shader com esse nome (nome do cbuffer ou da variável de recurso).
    // Retorna false se o buffer for nulo ou não suportar o uso pedido (ex.: Structured como ReadWrite).
    // Nome que o shader não usa (o compilador removeu o recurso, comum em arquivos com vários entry points)
    // é IGNORADO e retorna true: erros de digitação nos nomes são pegos pelo teste que confere os .hlsl.
    // Pode ser chamado a cada frame (buffers podem ser recriados).
    virtual bool SetBuffer(const char* name, IGpuBuffer* buffer, BindUsage usage) = 0;
};

class ICommandContext {
public:
    virtual ~ICommandContext() = default;
    // Dispara o compute com os bindings atuais. Retorna false se algum grupo for 0 ou passar de
    // DeviceCaps::maxComputeGroupsPerDim.
    virtual bool Dispatch(IComputePipeline& pipeline, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    // Barreira UAV: escritas anteriores neste buffer ficam visíveis para os comandos seguintes.
    virtual void UavBarrier(IGpuBuffer& buffer) = 0;
    // Copia size bytes do início de src para o início de dst (ex.: Stats -> Readback).
    virtual bool CopyBuffer(IGpuBuffer& src, IGpuBuffer& dst, uint64_t size) = 0;
};

struct DeviceCaps {
    bool computeShaders = true;
    bool fp16Arithmetic = false;
    bool mobileClass = false; // GPU tile-based / orçamento de banda baixo
    uint64_t vramBytes = 0;   // 0 = desconhecido
    uint64_t maxStructuredBufferBytes = 128ull * 1024 * 1024;
    uint32_t maxComputeGroupsPerDim = 65535;
};

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;
    virtual const char* BackendName() const = 0;
    virtual const DeviceCaps& Caps() const = 0;
    virtual std::unique_ptr<IGpuBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    // nullptr se não há suporte a compute ou se a compilação falhar.
    virtual std::unique_ptr<IComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    // Contexto onde os comandos são gravados/executados (thread de render).
    virtual ICommandContext& ImmediateContext() = 0;
};

// ---------------------------------------------------------------------------------------------
// Backend de referência em memória (testes, CI, servidores sem GPU). Não executa shaders: registra
// os comandos num log para inspeção. Copy/Update/ReadBack funcionam de verdade.
// ---------------------------------------------------------------------------------------------
struct NullCommand {
    enum class Type : uint8_t { Dispatch, UavBarrier, Copy } type = Type::Dispatch;
    std::string pipeline;               // Dispatch: debugName do pipeline
    uint32_t groups[3] = {0, 0, 0};     // Dispatch
    std::vector<std::pair<std::string, const IGpuBuffer*>> bindings; // Dispatch: snapshot dos bindings
    const IGpuBuffer* buffer = nullptr; // UavBarrier: buffer; Copy: origem
    const IGpuBuffer* target = nullptr; // Copy: destino
};

struct NullCommandLog {
    std::vector<NullCommand> commands;
    void Clear() { commands.clear(); }
};

std::unique_ptr<IGraphicsDevice> CreateNullDevice(const DeviceCaps& caps = {});
// nullptr se `device` não for um NullDevice.
NullCommandLog* GetNullCommandLog(IGraphicsDevice& device);

} // namespace LightPrism
