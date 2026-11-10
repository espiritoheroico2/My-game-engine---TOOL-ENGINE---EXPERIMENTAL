#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "LightPrism/Rhi.hpp"

namespace LightPrism {
namespace {

class NullBuffer final : public IGpuBuffer {
public:
    explicit NullBuffer(const BufferDesc& d)
        : m_name(d.debugName ? d.debugName : ""), m_desc(d), m_bytes(static_cast<size_t>(d.sizeBytes)) {
        m_desc.debugName = m_name.c_str();
    }
    const BufferDesc& Desc() const override { return m_desc; }

    bool Update(const void* data, uint64_t size, uint64_t offset) override {
        if (m_desc.kind == BufferKind::Readback || offset + size > m_bytes.size()) return false;
        if (size) std::memcpy(m_bytes.data() + offset, data, static_cast<size_t>(size));
        return true;
    }
    bool ReadBack(void* dst, uint64_t size, uint64_t offset) override {
        if (m_desc.kind != BufferKind::Readback || offset + size > m_bytes.size()) return false;
        if (size) std::memcpy(dst, m_bytes.data() + offset, static_cast<size_t>(size));
        return true;
    }
    // Ponteiro para os bytes — útil para inspeção em testes.
    void* NativeHandle() const override { return const_cast<unsigned char*>(m_bytes.data()); }

    unsigned char* Bytes() { return m_bytes.data(); }
    size_t ByteCount() const { return m_bytes.size(); }

private:
    std::string m_name; // declarado antes de m_desc: debugName aponta para ele
    BufferDesc m_desc;
    std::vector<unsigned char> m_bytes;
};

class NullPipeline final : public IComputePipeline {
public:
    explicit NullPipeline(const ComputePipelineDesc& d)
        : m_name(d.debugName ? d.debugName : ""), m_file(d.shaderFile ? d.shaderFile : ""),
          m_entry(d.entryPoint ? d.entryPoint : ""), m_dir(d.shaderDirectory ? d.shaderDirectory : "") {
        m_desc.debugName = m_name.c_str();
        m_desc.shaderFile = m_file.c_str();
        m_desc.entryPoint = m_entry.c_str();
        m_desc.shaderDirectory = m_dir.c_str();
    }
    const ComputePipelineDesc& Desc() const override { return m_desc; }

    bool SetBuffer(const char* name, IGpuBuffer* buffer, BindUsage usage) override {
        if (!name || !buffer) return false;
        const BufferKind k = buffer->Desc().kind;
        const bool ok = (usage == BindUsage::Constant && k == BufferKind::Uniform) ||
                        (usage == BindUsage::ReadOnly && (k == BufferKind::Structured || k == BufferKind::StructuredRW)) ||
                        (usage == BindUsage::ReadWrite && k == BufferKind::StructuredRW);
        if (!ok) return false;
        m_bindings[name] = buffer;
        return true;
    }

    std::vector<std::pair<std::string, const IGpuBuffer*>> Snapshot() const {
        return {m_bindings.begin(), m_bindings.end()};
    }

private:
    std::string m_name, m_file, m_entry, m_dir;
    ComputePipelineDesc m_desc;
    std::map<std::string, const IGpuBuffer*> m_bindings;
};

class NullContext final : public ICommandContext {
public:
    explicit NullContext(const DeviceCaps& caps) : m_caps(caps) {}

    bool Dispatch(IComputePipeline& p, uint32_t x, uint32_t y, uint32_t z) override {
        const uint32_t m = m_caps.maxComputeGroupsPerDim;
        if (x == 0 || y == 0 || z == 0 || x > m || y > m || z > m) return false;
        NullCommand c;
        c.type = NullCommand::Type::Dispatch;
        auto& np = static_cast<NullPipeline&>(p);
        c.pipeline = np.Desc().debugName;
        c.groups[0] = x; c.groups[1] = y; c.groups[2] = z;
        c.bindings = np.Snapshot();
        log.commands.push_back(std::move(c));
        return true;
    }
    void UavBarrier(IGpuBuffer& b) override {
        NullCommand c;
        c.type = NullCommand::Type::UavBarrier;
        c.buffer = &b;
        log.commands.push_back(std::move(c));
    }
    bool CopyBuffer(IGpuBuffer& src, IGpuBuffer& dst, uint64_t size) override {
        auto& s = static_cast<NullBuffer&>(src);
        auto& d = static_cast<NullBuffer&>(dst);
        if (size > s.ByteCount() || size > d.ByteCount()) return false;
        if (size) std::memcpy(d.Bytes(), s.Bytes(), static_cast<size_t>(size));
        NullCommand c;
        c.type = NullCommand::Type::Copy;
        c.buffer = &src;
        c.target = &dst;
        log.commands.push_back(std::move(c));
        return true;
    }

    NullCommandLog log;

private:
    DeviceCaps m_caps;
};

class NullDevice final : public IGraphicsDevice {
public:
    explicit NullDevice(const DeviceCaps& c) : m_caps(c), m_context(m_caps) {}
    const char* BackendName() const override { return "Null"; }
    const DeviceCaps& Caps() const override { return m_caps; }

    std::unique_ptr<IGpuBuffer> CreateBuffer(const BufferDesc& d) override {
        if (d.sizeBytes == 0 || d.sizeBytes > m_caps.maxStructuredBufferBytes) return nullptr;
        const bool structured = d.kind == BufferKind::Structured || d.kind == BufferKind::StructuredRW;
        if (structured && (d.strideBytes == 0 || d.sizeBytes % d.strideBytes != 0)) return nullptr;
        if (d.kind == BufferKind::Uniform && d.sizeBytes % 16 != 0) return nullptr;
        return std::make_unique<NullBuffer>(d);
    }
    std::unique_ptr<IComputePipeline> CreateComputePipeline(const ComputePipelineDesc& d) override {
        if (!m_caps.computeShaders || !d.shaderFile || !*d.shaderFile) return nullptr;
        return std::make_unique<NullPipeline>(d);
    }
    ICommandContext& ImmediateContext() override { return m_context; }

    NullCommandLog& Log() { return m_context.log; }

private:
    DeviceCaps m_caps;
    NullContext m_context;
};

} // namespace

std::unique_ptr<IGraphicsDevice> CreateNullDevice(const DeviceCaps& caps) {
    return std::make_unique<NullDevice>(caps);
}

NullCommandLog* GetNullCommandLog(IGraphicsDevice& device) {
    auto* nd = dynamic_cast<NullDevice*>(&device);
    return nd ? &nd->Log() : nullptr;
}

} // namespace LightPrism
