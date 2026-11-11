#pragma once

#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Rendering {

struct RenderBackendStats {
    std::uint64_t submittedFrames = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t submittedBatches = 0;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // Implementations must respect FramePacket order and may only merge adjacent
    // compatible batches. GPU resource lifetime/synchronization belongs to backend.
    virtual bool Submit(const FramePacket& packet) = 0;
};

class NullRenderBackend final : public IRenderBackend {
public:
    bool Submit(const FramePacket& packet) override {
        if (!IsFramePacketValid(packet)) return false;
        ++stats_.submittedFrames;
        stats_.submittedInstances += packet.instances.size();
        stats_.submittedBatches += packet.batches.size();
        return true;
    }

    [[nodiscard]] const RenderBackendStats& GetStats() const noexcept { return stats_; }

private:
    RenderBackendStats stats_{};
};

} // namespace ToolEngine::SpriteUi::Rendering
