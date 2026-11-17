#pragma once

// Janelas flutuantes (modelo puro, sem desenho). Uma aba que sai do dock vira uma janela flutuante sobre um
// canvas. O modelo guarda posicao, tamanho, ordem Z e estado (normal, maximizada, minimizada). Nao desenha nem
// le ponteiro: quem chama traduz o gesto em Move/Resize e aplica o encaixe sugerido.
//
// Regras:
//  - A janela nunca sai do canvas por completo: sempre sobra uma faixa horizontal (kFloatingGrabWidth) e a
//    barra de titulo inteira fica dentro do canvas na vertical.
//  - Resize respeita o tamanho minimo; a borda oposta fica parada quando o minimo e atingido.
//  - Float/Dock usam o dock existente (RemoveFromDock, ApplyDropResult, FindLeafContaining, ComputeDropSlot).
//    Nada de logica de arvore duplicada.
//  - Ordem Z: cada janela tem um numero unico; BringToFront nunca repete um valor e renumera a ordem quando o
//    contador chega ao fim, sem mudar a ordem relativa entre as janelas.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/Docking.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

// Largura minima (em pixels logicos) da janela que deve continuar visivel na horizontal.
inline constexpr float kFloatingGrabWidth = 48.0f;

enum class FloatState : std::uint8_t { Normal, Maximized, Minimized };

struct FloatingWindow {
    UIHandle window{};
    Rect rect{};                  // posicao e tamanho atuais (no estado normal, e ao minimizar)
    Rect restoreRect{};           // retangulo normal guardado enquanto maximizada
    Vec2 minSize{150.0f, 100.0f};
    float titleBarHeight = 30.0f;
    std::uint32_t zOrder = 0;
    FloatState state = FloatState::Normal;
    // Janela do mesmo grupo de abas de onde a janela saiu. Dock volta para esse grupo se ele ainda existir.
    UIHandle dockAnchor{};
};

// Sugestao de encaixe para uma janela flutuante arrastada.
struct FloatingSnap {
    DockSlot slot = DockSlot::None;   // Center = juntar como aba; None = sem encaixe
    UIHandle targetLeaf{};            // folha alvo (vazio quando canvasEdge)
    bool canvasEdge = false;          // encaixe na borda do canvas (divide a raiz do dock)
};

class FloatingWindowManager {
public:
    // firstZ: primeiro valor do contador de ordem Z (existe para exercitar o caso de estouro).
    explicit FloatingWindowManager(Rect canvas, std::uint32_t firstZ = 1) noexcept;

    [[nodiscard]] Rect Canvas() const noexcept { return canvas_; }
    // Troca o canvas e reencaixa todas as janelas flutuantes nele.
    void SetCanvas(Rect canvas) noexcept;

    // Tira `window` do dock (se estiver nele) e a torna flutuante em `rect`. Recusa handle invalido, janela ja
    // flutuante ou retangulo nao finito/sem area. Tamanho menor que minSize e ampliado.
    bool Float(UIDockNode& root, UIHandle window, Rect rect, Vec2 minSize = {150.0f, 100.0f},
               float titleBarHeight = 30.0f);

    // Devolve a janela flutuante ao dock. Com `slot` Center junta como aba. Se `targetLeaf` nao for valido, usa
    // o grupo de origem (se ainda existir); senao, a primeira folha da arvore (a raiz, se nao esta dividida).
    // Retorna false, sem alterar nada, se a janela nao esta flutuando ou o encaixe nao e possivel.
    bool Dock(UIDockNode& root, UIHandle window, DockSlot slot = DockSlot::Center, UIHandle targetLeaf = {},
              float ratio = 0.5f);

    bool Move(UIHandle window, float dx, float dy) noexcept;
    bool Resize(UIHandle window, ResizeEdge edge, float dx, float dy) noexcept;
    bool BringToFront(UIHandle window) noexcept;

    bool Maximize(UIHandle window) noexcept;
    bool Restore(UIHandle window) noexcept;
    bool Minimize(UIHandle window) noexcept;

    [[nodiscard]] const FloatingWindow* Find(UIHandle window) const noexcept;
    [[nodiscard]] std::span<const FloatingWindow> Windows() const noexcept { return windows_; }

    // Sugere um encaixe para o ponteiro. Primeiro a borda do canvas (raiz do dock), depois as folhas visiveis
    // (borda da folha vira Dock, centro vira Center/Tab). Fora de tudo: sem encaixe.
    [[nodiscard]] FloatingSnap SuggestSnap(Vec2 pointer, std::span<const DockLeafRect> leaves,
                                           float edgeFraction = 0.25f, float canvasMargin = 16.0f) const noexcept;

private:
    [[nodiscard]] FloatingWindow* FindMutable(UIHandle window) noexcept;
    [[nodiscard]] std::uint32_t TakeZ() noexcept;
    void RenumberZ() noexcept;

    Rect canvas_{};
    std::uint32_t nextZ_ = 1;
    std::vector<FloatingWindow> windows_;
};

} // namespace ToolEngine::SpriteUi::Widgets
