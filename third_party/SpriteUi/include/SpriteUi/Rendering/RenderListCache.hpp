#pragma once

// Lista de itens de desenho em trechos, um trecho por no (por exemplo, um botao ou um painel).
// Quando um no fica sujo, so o trecho dele e refeito; os outros sao reaproveitados como estao.
// Exemplo: um painel com tres botoes. Mudar o texto do botao 2 chama o construtor so para o botao 2.
// Sem mudanca nenhuma, nenhum no e refeito.

#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace ToolEngine::SpriteUi::Rendering {

class RenderListCache {
public:
    // Preenche out com os itens do no. Recebe o indice do no e uma lista vazia para escrever.
    using NodeBuilder = std::function<void(std::size_t node, std::vector<UIRenderItem>& out)>;

    // Define quantos nos existem. Todos ficam sujos, entao o proximo Update refaz tudo.
    void Reset(std::size_t nodeCount) {
        segments_.assign(nodeCount, Segment{});
        items_.clear();
    }

    void MarkDirty(std::size_t node) {
        if (node < segments_.size()) segments_[node].dirty = true;
    }

    // Refaz os trechos sujos, na ordem dos nos, e devolve a lista inteira. Nos limpos nao sao chamados.
    const std::vector<UIRenderItem>& Update(const NodeBuilder& build) {
        lastRebuildCount_ = 0;
        std::size_t offset = 0;
        for (std::size_t node = 0; node < segments_.size(); ++node) {
            Segment& segment = segments_[node];
            if (segment.dirty) {
                scratch_.clear();
                build(node, scratch_);
                const auto first = items_.begin() + static_cast<std::ptrdiff_t>(offset);
                items_.erase(first, first + static_cast<std::ptrdiff_t>(segment.count));
                items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(offset), scratch_.begin(), scratch_.end());
                segment.count = scratch_.size();
                segment.dirty = false;
                ++lastRebuildCount_;
            }
            offset += segment.count;
        }
        return items_;
    }

    // Quantos nos foram refeitos na ultima chamada de Update.
    [[nodiscard]] std::size_t LastRebuildCount() const noexcept { return lastRebuildCount_; }

private:
    struct Segment {
        std::size_t count = 0;
        bool dirty = true;
    };

    std::vector<Segment> segments_;
    std::vector<UIRenderItem> items_;
    std::vector<UIRenderItem> scratch_;
    std::size_t lastRebuildCount_ = 0;
};

} // namespace ToolEngine::SpriteUi::Rendering
