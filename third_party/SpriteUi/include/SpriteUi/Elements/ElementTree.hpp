#pragma once

// Arvore de elementos. Dona de todos os elementos; o chamador so segura UIHandle.
//
// Regras:
//  - Destruir um elemento destroi a subarvore inteira. O handle antigo deixa de valer, mesmo que o indice
//    seja reaproveitado (o geracao do slot muda).
//  - A ordem dos filhos e a ordem de pintura: pai antes dos filhos, irmaos na ordem da lista.
//  - Get devolve ponteiro valido ate a proxima mudanca na arvore (criar, destruir ou reordenar). Nao o guarde.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Elements/Element.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

class ElementTree {
  public:
    ElementTree() = default;

    // Cria um elemento do tipo T. Sem pai (handle invalido) vira raiz.
    template <class T, class... Args>
    UIHandle Create(UIHandle parent, Args&&... args) {
        return Adopt(std::make_unique<T>(std::forward<Args>(args)...), parent);
    }

    // Destroi o elemento e toda a subarvore. Handle invalido ou antigo nao faz nada.
    void Destroy(UIHandle handle);

    // Acesso. nullptr se o handle for invalido ou antigo.
    [[nodiscard]] Element* Get(UIHandle handle) noexcept;
    [[nodiscard]] const Element* Get(UIHandle handle) const noexcept;

    // Acesso tipado. nullptr se o handle for invalido ou o elemento nao for do tipo T.
    template <class T>
    [[nodiscard]] T* GetAs(UIHandle handle) noexcept {
        return dynamic_cast<T*>(Get(handle));
    }

    // Move o elemento para o fim da lista de irmaos: ele passa a pintar por cima deles.
    void BringToFront(UIHandle handle);

    // Ordem de pintura da subarvore de root (pre-ordem), gravada em out.
    void PaintOrder(UIHandle root, std::vector<UIHandle>& out) const;

    // Calcula a caixa externa de root dentro de area e, recursivamente, a dos filhos dentro da area util do
    // pai. Um filho que sai da area util nao e empurrado: a caixa dele pode extrapolar, e o desenho recorta.
    void Arrange(UIHandle root, Rect area);

    // Numero de elementos vivos na arvore.
    [[nodiscard]] std::size_t LiveCount() const noexcept;

  private:
    struct Slot {
        std::uint32_t generation = 1;
        std::unique_ptr<Element> element;
    };

    UIHandle Adopt(std::unique_ptr<Element> element, UIHandle parent);
    void DetachFromParent(const Element& element);
    void DestroySlot(std::uint32_t index);

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
};

} // namespace ToolEngine::SpriteUi::Elements
