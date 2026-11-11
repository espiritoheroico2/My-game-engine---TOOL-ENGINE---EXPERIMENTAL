#pragma once

// Interacao de arrastar janelas no docking (Fase B do guia). Componente puro: recebe posicoes do ponteiro e
// devolve a acao a executar. Nao renderiza nem altera a arvore de docking; quem chama aplica a acao.
//
// Regras:
//  - Clique sem arrasto nao faz nada.
//  - O arrasto so comeca depois de passar do limiar (dragThreshold). Tremor do ponteiro nao move a janela.
//  - Solta sobre uma folha: borda vira Dock (divide a folha), centro vira Tab (junta como aba).
//  - Solta fora de qualquer folha: Float (janela flutuante).
//  - Cancelar (pointerCancelled) descarta o arrasto sem acao.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/Docking.hpp"

#include <cstdint>
#include <span>

namespace ToolEngine::SpriteUi::Widgets {

struct DockInteractionConfig {
    float dragThreshold = 6.0f;   // pixels logicos de deslocamento ate o arrasto comecar
    float edgeFraction = 0.25f;   // fracao de cada borda da folha que vira zona de encaixe
};

enum class DockAction : std::uint8_t {
    None,
    Dock,   // divide a folha alvo no slot indicado
    Tab,    // junta a janela como aba da folha alvo
    Float   // solta fora do dock: janela flutuante
};

struct DockLeafRect {
    UIHandle leaf{};
    Rect bounds{};
};

struct DockDropResult {
    DockAction action = DockAction::None;
    DockSlot slot = DockSlot::None;   // valido quando action == Dock
    UIHandle targetLeaf{};            // folha alvo quando action == Dock ou Tab
};

// Zona de encaixe de um ponto dentro de uma folha. Retorna None fora da folha ou se a folha nao tem area.
[[nodiscard]] DockSlot ComputeDropSlot(Rect leaf, Vec2 pointer, float edgeFraction) noexcept;

// Mantem uma janela flutuante visivel dentro da tela. Se a janela for maior que a tela, reduz o tamanho.
[[nodiscard]] Rect ClampWindowToScreen(Rect window, Rect screen) noexcept;

class DockDragController {
public:
    explicit DockDragController(DockInteractionConfig config = {}) noexcept : config_{config} {}

    // Pressionar o titulo ou a aba de uma janela.
    void BeginPress(UIHandle window, Vec2 pointer) noexcept;

    // Atualiza com a posicao atual. Retorna true quando o arrasto esta ativo.
    bool Move(Vec2 pointer) noexcept;

    // Soltar o ponteiro: decide a acao usando as folhas visiveis. Sempre encerra o gesto.
    [[nodiscard]] DockDropResult Release(Vec2 pointer, std::span<const DockLeafRect> leaves) noexcept;

    // Cancelar (perda de foco, captura interrompida): encerra sem acao.
    void Cancel() noexcept;

    [[nodiscard]] bool IsPressed() const noexcept { return pressed_; }
    [[nodiscard]] bool IsDragging() const noexcept { return dragging_; }
    [[nodiscard]] UIHandle Window() const noexcept { return window_; }

private:
    DockInteractionConfig config_{};
    UIHandle window_{};
    Vec2 pressPosition_{};
    Vec2 lastPosition_{};
    bool pressed_ = false;
    bool dragging_ = false;
};

} // namespace ToolEngine::SpriteUi::Widgets
