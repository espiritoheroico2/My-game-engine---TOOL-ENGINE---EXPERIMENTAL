#pragma once

// Lista de itens de desenho em trechos, um trecho por no.
// Os builders dos nos sujos sao executados antes de publicar qualquer alteracao.
// Se a quantidade de itens nao mudar, somente os intervalos sujos sao copiados.
// Se houver mudanca estrutural, a lista contigua e reconstruida uma unica vez.

#include "SpriteUi/Rendering/DrawList.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

namespace ToolEngine::SpriteUi::Rendering {

class RenderListCache {
public:
    using NodeBuilder = std::function<void(std::size_t node, std::vector<UIRenderItem>& out)>;

    void Reset(std::size_t nodeCount) {
        segments_.assign(nodeCount, Segment{});
        items_.clear();
        pendingItems_.clear();
        pendingSegments_.clear();
        rebuiltItems_.clear();
        lastRebuildCount_ = 0;
    }

    void MarkDirty(std::size_t node) {
        if (node < segments_.size()) segments_[node].dirty = true;
    }

    // Atualiza os nos sujos na ordem original.
    // Se um builder lancar excecao, a lista publicada e os dirty flags permanecem intactos.
    const std::vector<UIRenderItem>& Update(const NodeBuilder& build) {
        lastRebuildCount_ = 0;
        pendingItems_.clear();
        pendingSegments_.clear();

        // Fase 1: preparar todas as alteracoes sem tocar na lista publicada.
        for (std::size_t node = 0; node < segments_.size(); ++node) {
            if (!segments_[node].dirty) continue;

            scratch_.clear();
            build(node, scratch_);

            if (scratch_.size() > pendingItems_.max_size() - pendingItems_.size()) {
                throw std::length_error("RenderListCache: staged item count overflow");
            }

            const std::size_t stagedOffset = pendingItems_.size();
            pendingItems_.insert(pendingItems_.end(), scratch_.begin(), scratch_.end());
            pendingSegments_.push_back({node, stagedOffset, scratch_.size()});
        }

        if (pendingSegments_.empty()) return items_;

        std::size_t newTotal = 0;
        bool structuralChange = false;
        std::size_t pendingIndex = 0;
        for (std::size_t node = 0; node < segments_.size(); ++node) {
            const Segment& segment = segments_[node];
            std::size_t nextCount = segment.count;
            if (segment.dirty) {
                const PendingSegment& pending = pendingSegments_[pendingIndex++];
                nextCount = pending.count;
                structuralChange = structuralChange || (nextCount != segment.count);
            }
            if (nextCount > items_.max_size() - newTotal) {
                throw std::length_error("RenderListCache: resulting item count overflow");
            }
            newTotal += nextCount;
        }

        // Fase 2: publicar. Sem alteracao estrutural, enderecos e capacidade permanecem estaveis.
        if (!structuralChange) {
            std::size_t oldOffset = 0;
            std::size_t pendingIndexForCopy = 0;
            for (std::size_t node = 0; node < segments_.size(); ++node) {
                const Segment& segment = segments_[node];
                if (segment.dirty) {
                    const PendingSegment& pending = pendingSegments_[pendingIndexForCopy++];
                    std::copy_n(pendingItems_.begin() + static_cast<std::ptrdiff_t>(pending.offset),
                                pending.count,
                                items_.begin() + static_cast<std::ptrdiff_t>(oldOffset));
                }
                oldOffset += segment.count;
            }
            for (const PendingSegment& pending : pendingSegments_) {
                segments_[pending.node].dirty = false;
            }
        } else {
            rebuiltItems_.clear();
            rebuiltItems_.reserve(newTotal);

            std::size_t oldOffset = 0;
            pendingIndex = 0;
            for (std::size_t node = 0; node < segments_.size(); ++node) {
                const Segment& segment = segments_[node];
                if (segment.dirty) {
                    const PendingSegment& pending = pendingSegments_[pendingIndex++];
                    const auto first = pendingItems_.begin() + static_cast<std::ptrdiff_t>(pending.offset);
                    rebuiltItems_.insert(rebuiltItems_.end(), first,
                                          first + static_cast<std::ptrdiff_t>(pending.count));
                } else {
                    const auto first = items_.begin() + static_cast<std::ptrdiff_t>(oldOffset);
                    rebuiltItems_.insert(rebuiltItems_.end(), first,
                                          first + static_cast<std::ptrdiff_t>(segment.count));
                }
                oldOffset += segment.count;
            }

            items_.swap(rebuiltItems_);
            for (const PendingSegment& pending : pendingSegments_) {
                segments_[pending.node].count = pending.count;
                segments_[pending.node].dirty = false;
            }
        }

        lastRebuildCount_ = pendingSegments_.size();
        return items_;
    }

    [[nodiscard]] std::size_t LastRebuildCount() const noexcept { return lastRebuildCount_; }

private:
    struct Segment {
        std::size_t count = 0;
        bool dirty = true;
    };

    struct PendingSegment {
        std::size_t node = 0;
        std::size_t offset = 0;
        std::size_t count = 0;
    };

    std::vector<Segment> segments_;
    std::vector<UIRenderItem> items_;
    std::vector<UIRenderItem> scratch_;
    std::vector<UIRenderItem> pendingItems_;
    std::vector<PendingSegment> pendingSegments_;
    std::vector<UIRenderItem> rebuiltItems_;
    std::size_t lastRebuildCount_ = 0;
};

} // namespace ToolEngine::SpriteUi::Rendering
