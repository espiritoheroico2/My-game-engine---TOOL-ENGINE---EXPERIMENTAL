#include "SpriteUi/Rendering/DrawList.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace ToolEngine::SpriteUi::Rendering {
namespace {

[[nodiscard]] bool SameBatch(const DrawBatch& batch, const UIRenderItem& item) noexcept {
    return batch.textureSlice == item.textureSlice &&
           batch.materialIndex == item.materialIndex &&
           batch.clipIndex == item.clipIndex &&
           batch.flags == item.flags;
}

[[nodiscard]] bool ValidClip(const ClipRectGPU& clip) noexcept {
    return IsFinite(clip.minX) && IsFinite(clip.minY) &&
           IsFinite(clip.maxX) && IsFinite(clip.maxY) &&
           clip.maxX >= clip.minX && clip.maxY >= clip.minY;
}

[[nodiscard]] bool ValidItem(const UIRenderItem& item) noexcept {
    if (!item.handle.IsValid() || !item.visible || !item.layout.visible) return false;
    if (!IsValidPremultipliedTint(item.tintPremultipliedLinear)) return false;
    if (!IsFinite(item.opacity) || !IsFinite(item.zOrder) || !IsFinite(item.cornerRadius) || !IsFinite(item.layout.resolvedSize) ||
        !IsFinite(item.layout.resolvedPosition)) return false;
    for (float uv : item.uvRect) {
        if (!IsFinite(uv)) return false;
    }
    return true;
}

} // namespace

ClipHierarchyResult BuildClipHierarchy(std::span<const ClipNode> nodes) {
    ClipHierarchyResult result;
    result.nodeClipIndices.assign(nodes.size(), NoClipIndex);
    result.effectiveRects.reserve(nodes.size());
    std::vector<std::uint32_t> effectiveIndex(nodes.size(), NoClipIndex);

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const ClipNode& node = nodes[i];
        if (node.parentIndex != NoClipIndex && node.parentIndex >= i) {
            result.valid = false;
            return result;
        }
        if (!node.enabled) {
            if (node.parentIndex != NoClipIndex)
                effectiveIndex[i] = effectiveIndex[node.parentIndex];
            result.nodeClipIndices[i] = effectiveIndex[i];
            continue;
        }
        if (!ValidClip(node.rect)) {
            result.valid = false;
            return result;
        }

        ClipRectGPU effective = node.rect;
        if (node.parentIndex != NoClipIndex) {
            const std::uint32_t parentClip = effectiveIndex[node.parentIndex];
            if (parentClip != NoClipIndex) {
                const ClipRectGPU& parent = result.effectiveRects[parentClip];
                effective.minX = std::max(effective.minX, parent.minX);
                effective.minY = std::max(effective.minY, parent.minY);
                effective.maxX = std::min(effective.maxX, parent.maxX);
                effective.maxY = std::min(effective.maxY, parent.maxY);
            }
        }
        if (result.effectiveRects.size() >= NoClipIndex) {
            result.valid = false;
            return result;
        }
        const auto index = static_cast<std::uint32_t>(result.effectiveRects.size());
        result.effectiveRects.push_back(effective);
        effectiveIndex[i] = index;
        result.nodeClipIndices[i] = index;
    }
    return result;
}

bool IsFramePacketValid(const FramePacket& packet) noexcept {
    for (const ClipRectGPU& clip : packet.clipRects) {
        if (!ValidClip(clip)) return false;
    }

    for (const SpriteInstanceGPU& instance : packet.instances) {
        const Vec4 tint{instance.tint[0], instance.tint[1], instance.tint[2], instance.tint[3]};
        if (!IsValidPremultipliedTint(tint)) return false;
        for (float value : instance.transformRow0) if (!IsFinite(value)) return false;
        for (float value : instance.transformRow1) if (!IsFinite(value)) return false;
        for (float value : instance.uvRect) if (!IsFinite(value)) return false;
        if (!IsFinite(instance.uvRect[0] + instance.uvRect[2]) ||
            !IsFinite(instance.uvRect[1] + instance.uvRect[3])) return false;
    }

    std::uint64_t expectedFirst = 0;
    for (const DrawBatch& batch : packet.batches) {
        if (batch.instanceCount == 0 || batch.firstInstance != expectedFirst) return false;
        expectedFirst += batch.instanceCount;
        if (expectedFirst > packet.instances.size()) return false;

        for (std::uint64_t i = batch.firstInstance; i < expectedFirst; ++i) {
            const SpriteInstanceGPU& instance = packet.instances[static_cast<std::size_t>(i)];
            if (instance.resourceIds[0] != batch.textureSlice ||
                instance.resourceIds[1] != batch.materialIndex ||
                instance.resourceIds[2] != batch.clipIndex ||
                instance.resourceIds[3] != batch.flags) {
                return false;
            }
            const std::uint32_t clipIndex = instance.resourceIds[2];
            if (clipIndex != 0xFFFFFFFFu && clipIndex >= packet.clipRects.size()) return false;
        }
    }
    return expectedFirst == packet.instances.size();
}

FramePacket RenderExtractor::Build(
    std::uint64_t frameId,
    const std::vector<UIRenderItem>& items,
    ExtractionStats* outStats,
    std::span<const ClipRectGPU> clipRects) const
{
    FramePacket packet;
    packet.frameId = frameId;
    packet.clipRects.assign(clipRects.begin(), clipRects.end());

    ExtractionStats localStats{};
    std::vector<const UIRenderItem*> ordered;
    ordered.reserve(items.size());

    for (const UIRenderItem& item : items) {
        if (!item.visible || !item.layout.visible) {
            ++localStats.skippedInvisible;
            continue;
        }
        if (!ValidItem(item) ||
            (item.clipIndex != 0xFFFFFFFFu &&
             (item.clipIndex >= clipRects.size() || !ValidClip(clipRects[item.clipIndex])))) {
            ++localStats.skippedInvalid;
            continue;
        }
        ordered.push_back(&item);
    }

    // Paint order is authoritative. Material sorting is intentionally forbidden
    // across non-adjacent items because alpha blending is order dependent.
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const UIRenderItem* a, const UIRenderItem* b) {
            return std::tuple{a->layer, a->zOrder, a->paintOrder, a->handle.Pack()} <
                   std::tuple{b->layer, b->zOrder, b->paintOrder, b->handle.Pack()};
        });

    packet.instances.reserve(ordered.size());
    packet.batches.reserve(ordered.size());

    for (const UIRenderItem* item : ordered) {
        const auto affine = BuildUnitQuadTransform(item->layout, item->transform);
        if (!affine) {
            ++localStats.skippedInvalid;
            continue;
        }

        const float packedDepth = item->transform.depth + item->zOrder;
        if (!IsFinite(packedDepth)) {
            ++localStats.skippedInvalid;
            continue;
        }

        SpriteInstanceGPU instance{};
        instance.transformRow0[0] = affine->m00;
        instance.transformRow0[1] = affine->m01;
        instance.transformRow0[2] = affine->tx;
        instance.transformRow0[3] = packedDepth;

        instance.transformRow1[0] = affine->m10;
        instance.transformRow1[1] = affine->m11;
        instance.transformRow1[2] = affine->ty;
        instance.transformRow1[3] = std::max(0.0f, item->cornerRadius);

        std::copy(std::begin(item->uvRect), std::end(item->uvRect), std::begin(instance.uvRect));

        const Vec4 tint = item->tintPremultipliedLinear * Clamp01(item->opacity);
        instance.tint[0] = tint.x;
        instance.tint[1] = tint.y;
        instance.tint[2] = tint.z;
        instance.tint[3] = tint.w;

        instance.resourceIds[0] = item->textureSlice;
        instance.resourceIds[1] = item->materialIndex;
        instance.resourceIds[2] = item->clipIndex;
        instance.resourceIds[3] = item->flags;

        if (packet.instances.size() >= std::numeric_limits<std::uint32_t>::max()) {
            ++localStats.skippedInvalid;
            continue;
        }
        const auto instanceIndex = static_cast<std::uint32_t>(packet.instances.size());
        packet.instances.push_back(instance);

        if (packet.batches.empty() || !SameBatch(packet.batches.back(), *item)) {
            packet.batches.push_back(DrawBatch{
                instanceIndex, 1u, item->textureSlice, item->materialIndex,
                item->clipIndex, item->flags
            });
        } else {
            ++packet.batches.back().instanceCount;
        }
    }

    localStats.submittedItems = static_cast<std::uint32_t>(packet.instances.size());
    localStats.batchCount = static_cast<std::uint32_t>(packet.batches.size());
    if (outStats) *outStats = localStats;
    return packet;
}

} // namespace ToolEngine::SpriteUi::Rendering
