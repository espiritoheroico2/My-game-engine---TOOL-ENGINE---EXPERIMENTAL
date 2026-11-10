#include "LightPrism/Backends/DiligentDevice.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "Buffer.h"
#include "BufferView.h"
#include "DeviceContext.h"
#include "EngineFactory.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"

namespace LightPrism {
namespace {

namespace DG = Diligent;

// ---------------------------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------------------------
class DiligentBuffer final : public IGpuBuffer {
public:
    DiligentBuffer(const BufferDesc& d, DG::RefCntAutoPtr<DG::IBuffer> buffer, DG::IDeviceContext* ctx)
        : m_name(d.debugName ? d.debugName : ""), m_desc(d), m_buffer(std::move(buffer)), m_context(ctx) {
        m_desc.debugName = m_name.c_str();
    }

    const BufferDesc& Desc() const override { return m_desc; }

    bool Update(const void* data, uint64_t size, uint64_t offset) override {
        if (!m_buffer || m_desc.kind == BufferKind::Readback || offset + size > m_desc.sizeBytes) return false;
        if (size == 0) return true;
        m_context->UpdateBuffer(m_buffer.RawPtr(), offset, size, data, DG::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return true;
    }

    // Não bloqueia: se a GPU ainda não terminou a cópia, retorna false e o chamador tenta no frame seguinte.
    bool ReadBack(void* dst, uint64_t size, uint64_t offset) override {
        if (!m_buffer || m_desc.kind != BufferKind::Readback || offset + size > m_desc.sizeBytes) return false;
        if (size == 0) return true;
        DG::PVoid mapped = nullptr;
        m_context->MapBuffer(m_buffer.RawPtr(), DG::MAP_READ, DG::MAP_FLAG_DO_NOT_WAIT, mapped);
        if (!mapped) return false;
        std::memcpy(dst, static_cast<const unsigned char*>(mapped) + offset, static_cast<size_t>(size));
        m_context->UnmapBuffer(m_buffer.RawPtr(), DG::MAP_READ);
        return true;
    }

    void* NativeHandle() const override { return m_buffer.RawPtr(); } // Diligent::IBuffer*

    DG::IBuffer* Raw() const { return m_buffer.RawPtr(); }

private:
    std::string m_name; // declarado antes de m_desc: debugName aponta para ele
    BufferDesc m_desc;
    DG::RefCntAutoPtr<DG::IBuffer> m_buffer;
    DG::IDeviceContext* m_context;
};

// ---------------------------------------------------------------------------------------------
// Pipeline de compute
// ---------------------------------------------------------------------------------------------
class DiligentPipeline final : public IComputePipeline {
public:
    DiligentPipeline(const ComputePipelineDesc& d, DG::RefCntAutoPtr<DG::IPipelineState> pso,
                     DG::RefCntAutoPtr<DG::IShaderResourceBinding> srb)
        : m_name(d.debugName ? d.debugName : ""), m_file(d.shaderFile ? d.shaderFile : ""),
          m_entry(d.entryPoint ? d.entryPoint : ""), m_dir(d.shaderDirectory ? d.shaderDirectory : ""),
          m_pso(std::move(pso)), m_srb(std::move(srb)) {
        m_desc.debugName = m_name.c_str();
        m_desc.shaderFile = m_file.c_str();
        m_desc.entryPoint = m_entry.c_str();
        m_desc.shaderDirectory = m_dir.c_str();
    }

    const ComputePipelineDesc& Desc() const override { return m_desc; }

    bool SetBuffer(const char* name, IGpuBuffer* buffer, BindUsage usage) override {
        if (!name || !buffer) return false;
        const BufferKind k = buffer->Desc().kind;
        const bool compatible = (usage == BindUsage::Constant && k == BufferKind::Uniform) ||
                                (usage == BindUsage::ReadOnly && (k == BufferKind::Structured || k == BufferKind::StructuredRW)) ||
                                (usage == BindUsage::ReadWrite && k == BufferKind::StructuredRW);
        if (!compatible) return false;

        DG::IShaderResourceVariable* var = m_srb->GetVariableByName(DG::SHADER_TYPE_COMPUTE, name);
        if (!var) return true; // recurso removido pelo compilador neste entry point: ignorado (ver Rhi.hpp)

        DG::IBuffer* raw = static_cast<DiligentBuffer*>(buffer)->Raw();
        switch (usage) {
        case BindUsage::Constant: var->Set(raw); break;
        case BindUsage::ReadOnly: var->Set(raw->GetDefaultView(DG::BUFFER_VIEW_SHADER_RESOURCE)); break;
        case BindUsage::ReadWrite: var->Set(raw->GetDefaultView(DG::BUFFER_VIEW_UNORDERED_ACCESS)); break;
        }
        return true;
    }

    DG::IPipelineState* Pso() const { return m_pso.RawPtr(); }
    DG::IShaderResourceBinding* Srb() const { return m_srb.RawPtr(); }

private:
    std::string m_name, m_file, m_entry, m_dir;
    ComputePipelineDesc m_desc;
    DG::RefCntAutoPtr<DG::IPipelineState> m_pso;
    DG::RefCntAutoPtr<DG::IShaderResourceBinding> m_srb;
};

// ---------------------------------------------------------------------------------------------
// Contexto de comandos (contexto imediato do Diligent)
// ---------------------------------------------------------------------------------------------
class DiligentContext final : public ICommandContext {
public:
    DiligentContext(DG::IDeviceContext* ctx, const DeviceCaps* caps) : m_context(ctx), m_caps(caps) {}

    bool Dispatch(IComputePipeline& pipeline, uint32_t x, uint32_t y, uint32_t z) override {
        const uint32_t m = m_caps->maxComputeGroupsPerDim;
        if (x == 0 || y == 0 || z == 0 || x > m || y > m || z > m) return false;
        auto& p = static_cast<DiligentPipeline&>(pipeline);
        m_context->SetPipelineState(p.Pso());
        m_context->CommitShaderResources(p.Srb(), DG::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_context->DispatchCompute(DG::DispatchComputeAttribs{x, y, z});
        return true;
    }

    void UavBarrier(IGpuBuffer& buffer) override {
        DG::IBuffer* raw = static_cast<DiligentBuffer&>(buffer).Raw();
        // OldState == NewState == UNORDERED_ACCESS emite uma barreira UAV (escritas anteriores ficam visíveis).
        const DG::StateTransitionDesc barrier{raw, DG::RESOURCE_STATE_UNORDERED_ACCESS, DG::RESOURCE_STATE_UNORDERED_ACCESS,
                                              DG::STATE_TRANSITION_FLAG_NONE};
        m_context->TransitionResourceStates(1, &barrier);
    }

    bool CopyBuffer(IGpuBuffer& src, IGpuBuffer& dst, uint64_t size) override {
        auto& s = static_cast<DiligentBuffer&>(src);
        auto& d = static_cast<DiligentBuffer&>(dst);
        if (size > s.Desc().sizeBytes || size > d.Desc().sizeBytes) return false;
        m_context->CopyBuffer(s.Raw(), 0, DG::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, d.Raw(), 0, size,
                              DG::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return true;
    }

private:
    DG::IDeviceContext* m_context;
    const DeviceCaps* m_caps;
};

// ---------------------------------------------------------------------------------------------
// Dispositivo
// ---------------------------------------------------------------------------------------------
class DiligentDevice final : public IGraphicsDevice {
public:
    DiligentDevice(DG::IRenderDevice* device, DG::IDeviceContext* context, const DiligentDeviceOptions& opt)
        : m_device(device), m_context(context), m_commands(context, &m_caps) {
        const DG::RenderDeviceInfo& info = device->GetDeviceInfo();
        m_caps.computeShaders = info.Features.ComputeShaders != DG::DEVICE_FEATURE_STATE_DISABLED;
        m_caps.fp16Arithmetic = info.Features.ShaderFloat16 != DG::DEVICE_FEATURE_STATE_DISABLED;
        m_caps.mobileClass = opt.mobileClass || info.Type == DG::RENDER_DEVICE_TYPE_GLES;

        const DG::GraphicsAdapterInfo& adapter = device->GetAdapterInfo();
        m_caps.vramBytes = adapter.Memory.LocalMemory != 0 ? adapter.Memory.LocalMemory
                                                           : adapter.Memory.UnifiedMemory; // iGPU: 0 se desconhecido
        if (adapter.ComputeShader.MaxThreadGroupCountX != 0)
            m_caps.maxComputeGroupsPerDim = adapter.ComputeShader.MaxThreadGroupCountX;
    }

    const char* BackendName() const override { return "Diligent"; }
    const DeviceCaps& Caps() const override { return m_caps; }
    ICommandContext& ImmediateContext() override { return m_commands; }

    std::unique_ptr<IGpuBuffer> CreateBuffer(const BufferDesc& d) override {
        if (!m_device || !m_context || d.sizeBytes == 0 || d.sizeBytes > m_caps.maxStructuredBufferBytes) return nullptr;

        DG::BufferDesc bd;
        bd.Name = d.debugName;
        bd.Size = d.sizeBytes;
        bd.Usage = DG::USAGE_DEFAULT; // atualizado via UpdateBuffer / escrito pela GPU
        switch (d.kind) {
        case BufferKind::Structured:
        case BufferKind::StructuredRW:
            if (d.strideBytes == 0 || d.sizeBytes % d.strideBytes != 0) return nullptr;
            bd.BindFlags = DG::BIND_SHADER_RESOURCE;
            if (d.kind == BufferKind::StructuredRW) bd.BindFlags |= DG::BIND_UNORDERED_ACCESS;
            bd.Mode = DG::BUFFER_MODE_STRUCTURED;
            bd.ElementByteStride = d.strideBytes;
            break;
        case BufferKind::Uniform:
            if (d.sizeBytes % 16 != 0) return nullptr; // exigência de cbuffer (D3D)
            bd.BindFlags = DG::BIND_UNIFORM_BUFFER;
            break;
        case BufferKind::Readback:
            bd.Usage = DG::USAGE_STAGING;
            bd.BindFlags = DG::BIND_NONE;
            bd.CPUAccessFlags = DG::CPU_ACCESS_READ;
            break;
        }

        DG::RefCntAutoPtr<DG::IBuffer> buffer;
        m_device->CreateBuffer(bd, nullptr, &buffer);
        if (!buffer) return nullptr;
        return std::make_unique<DiligentBuffer>(d, std::move(buffer), m_context);
    }

    std::unique_ptr<IComputePipeline> CreateComputePipeline(const ComputePipelineDesc& d) override {
        if (!m_caps.computeShaders || !d.shaderFile || !*d.shaderFile) return nullptr;

        // #include relativos entre pastas ("../Common/x.hlsli") e dentro da mesma pasta resolvem por estas raízes.
        const std::string root = d.shaderDirectory ? d.shaderDirectory : "";
        const std::string search = root + ";" + root + "/Cluster;" + root + "/Common";
        DG::RefCntAutoPtr<DG::IShaderSourceInputStreamFactory> factory;
        m_device->GetEngineFactory()->CreateDefaultShaderSourceStreamFactory(search.c_str(), &factory);
        if (!factory) return nullptr;

        // OpenGL: usa o GLSL 430 gerado offline (tools/shaders/build_glsl.py -> <root>/glsl/<arquivo>.<entry>.430.glsl).
        // Assim o OpenGL nao depende do tradutor HLSL->GLSL do Diligent, que nao aceita estes compute.
        const DG::RENDER_DEVICE_TYPE devType = m_device->GetDeviceInfo().Type;
        const bool useGlsl = devType == DG::RENDER_DEVICE_TYPE_GL || devType == DG::RENDER_DEVICE_TYPE_GLES;
        std::string glslText;
        if (useGlsl) {
            std::string stem = d.shaderFile;
            const size_t slash = stem.find_last_of("/\\");
            if (slash != std::string::npos) stem = stem.substr(slash + 1);
            const std::string suffix = ".compute.hlsl";
            if (stem.size() > suffix.size()) stem = stem.substr(0, stem.size() - suffix.size());
            const std::string path = root + "/glsl/" + stem + "." + (d.entryPoint ? d.entryPoint : "CSMain") + ".430.glsl";
            std::ifstream in(path, std::ios::binary);
            if (!in) return nullptr;
            glslText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        DG::ShaderCreateInfo sci;
        if (useGlsl) {
            sci.SourceLanguage = DG::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
            sci.Source = glslText.c_str();
            sci.SourceLength = glslText.size();
            sci.EntryPoint = "main";
        } else {
            sci.SourceLanguage = DG::SHADER_SOURCE_LANGUAGE_HLSL;
            sci.FilePath = d.shaderFile;
            sci.EntryPoint = d.entryPoint ? d.entryPoint : "CSMain";
        }
        sci.pShaderSourceStreamFactory = factory;
        sci.Desc.ShaderType = DG::SHADER_TYPE_COMPUTE;
        sci.Desc.Name = d.debugName;
        sci.Desc.UseCombinedTextureSamplers = false;

        DG::RefCntAutoPtr<DG::IShader> shader;
        m_device->CreateShader(sci, &shader);
        if (!shader) return nullptr;

        DG::ComputePipelineStateCreateInfo pci;
        pci.PSODesc.Name = d.debugName;
        pci.PSODesc.PipelineType = DG::PIPELINE_TYPE_COMPUTE;
        pci.pCS = shader;
        // Buffers são trocados a cada frame (recriação ao redimensionar): variáveis dinâmicas.
        pci.PSODesc.ResourceLayout.DefaultVariableType = DG::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        DG::RefCntAutoPtr<DG::IPipelineState> pso;
        m_device->CreateComputePipelineState(pci, &pso);
        if (!pso) return nullptr;

        DG::RefCntAutoPtr<DG::IShaderResourceBinding> srb;
        pso->CreateShaderResourceBinding(&srb, true);
        if (!srb) return nullptr;
        return std::make_unique<DiligentPipeline>(d, std::move(pso), std::move(srb));
    }

private:
    DG::RefCntAutoPtr<DG::IRenderDevice> m_device;
    DG::RefCntAutoPtr<DG::IDeviceContext> m_context;
    DeviceCaps m_caps;
    DiligentContext m_commands; // referencia m_caps: declarado depois dele
};

} // namespace

std::unique_ptr<IGraphicsDevice> CreateDiligentDevice(Diligent::IRenderDevice* device,
                                                      Diligent::IDeviceContext* context,
                                                      const DiligentDeviceOptions& options) {
    if (!device || !context) return nullptr;
    return std::make_unique<DiligentDevice>(device, context, options);
}

} // namespace LightPrism
