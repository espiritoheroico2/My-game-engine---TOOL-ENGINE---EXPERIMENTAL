#pragma once

// Dialogo modal como modelo puro: bloqueia a entrada atras dele, prende o foco nos seus controles e responde
// a Esc e Enter. Nao desenha e nao conhece eventos de plataforma: o chamador informa a posicao do ponteiro,
// a tecla e a quantidade de controles focaveis do dialogo.
//
// Regras:
//  - Aberto, o modal consome todo ponteiro. Dentro do dialogo (Dialog) o chamador entrega o evento ao
//    conteudo; no fundo (Blocked) nada chega a janela de tras.
//  - Clique no fundo fecha o modal so se closeOnOutsideClick estiver ligado (Cancelled). Movimento do
//    ponteiro nunca fecha.
//  - O foco comeca no controle padrao. Tab e Shift+Tab andam em ciclo pelos controles do dialogo, sem sair.
//  - Esc cancela e fecha. Enter ativa o controle focado e fecha (Confirmed). Activated() diz qual.
//  - Ao fechar, o modal devolve o alvo de foco que tinha antes de abrir (ReturnFocusTarget).
//  - Fechado, o modal nao bloqueia nada e ignora teclas.

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

struct ModalConfig {
    bool closeOnOutsideClick = false;
};

enum class ModalKey : std::uint8_t { Tab, BackTab, Enter, Escape };

enum class ModalEvent : std::uint8_t {
    None,         // Nada mudou.
    FocusMoved,   // O foco andou dentro do dialogo.
    Confirmed,    // Enter: o controle focado foi ativado e o modal fechou.
    Cancelled     // Esc: o modal fechou sem ativar nada.
};

enum class ModalPointer : std::uint8_t {
    Pass,      // Modal fechado: o ponteiro segue normalmente.
    Dialog,    // Dentro do dialogo: o chamador entrega o evento ao conteudo.
    Blocked,   // No fundo: consumido, sem efeito.
    Cancelled  // No fundo com closeOnOutsideClick: o modal fechou.
};

class ModalControl {
public:
    explicit ModalControl(ModalConfig config = {}) noexcept : config_{config} {}

    // Abre com o dialogo na caixa dada; tudo fora dela e o fundo. focusCount e a quantidade de controles
    // focaveis; defaultIndex e o que comeca focado (se for invalido, o primeiro). returnFocusTo e o alvo que
    // recebe o foco ao fechar.
    void Open(Rect dialog, std::size_t focusCount, std::size_t defaultIndex, std::uint64_t returnFocusTo) noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }
    [[nodiscard]] Rect Dialog() const noexcept { return dialog_; }
    [[nodiscard]] std::size_t Focus() const noexcept { return focus_; }
    [[nodiscard]] std::size_t Activated() const noexcept { return activated_; }
    [[nodiscard]] std::uint64_t ReturnFocusTarget() const noexcept { return returnFocusTo_; }

    // Ponteiro pressionado. Pode fechar o modal (closeOnOutsideClick).
    [[nodiscard]] ModalPointer PointerDown(Vec2 pointer) noexcept;
    // Movimento do ponteiro. Nunca fecha.
    [[nodiscard]] ModalPointer PointerMove(Vec2 pointer) const noexcept;

    [[nodiscard]] ModalEvent Key(ModalKey key) noexcept;

private:
    void Close() noexcept;
    [[nodiscard]] ModalPointer Classify(Vec2 pointer) const noexcept;
    [[nodiscard]] ModalEvent CycleFocus(bool forward) noexcept;

    ModalConfig config_;
    Rect dialog_{};
    std::size_t focusCount_ = 0;
    std::size_t focus_ = 0;
    std::size_t activated_ = 0;
    std::uint64_t returnFocusTo_ = 0;
    bool open_ = false;
};

} // namespace ToolEngine::SpriteUi::Widgets
