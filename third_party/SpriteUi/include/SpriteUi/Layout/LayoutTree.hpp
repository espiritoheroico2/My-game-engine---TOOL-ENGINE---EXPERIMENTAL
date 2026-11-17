#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Layout/Layout.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ToolEngine::SpriteUi {

struct LayoutElement {
    UIHandle id{};
    UIHandle parent{};
    LayoutNode node{};
    std::vector<UIHandle> children{};
    bool visible = true;
    // Ordem de empilhamento entre irmaos: 0 = ordem da lista; BringToFront grava um valor maior que os demais.
    std::uint64_t stackOrder = 0;
};

class LayoutTree {
public:
    [[nodiscard]] UIHandle Create(UIHandle parent = {}, FlexItem style = {});
    bool Remove(UIHandle id);

    [[nodiscard]] LayoutElement* Get(UIHandle id);
    [[nodiscard]] const LayoutElement* Get(UIHandle id) const;

    bool SetStyle(UIHandle id, FlexItem style);
    bool SetVisible(UIHandle id, bool visible);

    // Faz o elemento pintar e receber o ponteiro por cima dos irmaos. Nao muda a ordem de layout (flex), so a ordem
    // de empilhamento usada pelo LayoutSceneBridge. Chamar ao pressionar uma janela e o que faz a janela subir.
    bool BringToFront(UIHandle id);

    // Filhos (ou raizes) na ordem de pintura: ordem de layout, com os elementos trazidos para frente por ultimo.
    [[nodiscard]] std::vector<UIHandle> PaintChildren(UIHandle id) const;
    [[nodiscard]] std::vector<UIHandle> PaintRoots() const;

    // Recalcula o layout. Se nada mudou desde o ultimo calculo (sem Create, Remove, SetStyle ou SetVisible) e o
    // viewport e o mesmo, nao faz nada: o resultado seria igual. Mude o estilo com SetStyle, nunca direto em node.
    void Layout(Rect viewport, LayoutStats* stats = nullptr);

    // Verdadeiro quando o proximo Layout vai recalcular.
    [[nodiscard]] bool IsLayoutDirty() const noexcept { return layoutDirty_; }
    [[nodiscard]] const std::vector<UIHandle>& Roots() const noexcept { return roots_; }

private:
    [[nodiscard]] LayoutNode Build(UIHandle id);
    void Commit(UIHandle id, const LayoutNode& node);
    void RemoveSubtree(UIHandle id);

    UIHandle next_{0, 1};
    std::uint64_t stackCounter_ = 0;
    bool layoutDirty_ = true;
    Rect laidOutViewport_{};
    std::unordered_map<std::uint64_t, LayoutElement> elements_;
    std::vector<UIHandle> roots_;
};

} // namespace ToolEngine::SpriteUi
