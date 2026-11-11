#pragma once

// Modelo de edicao de texto de uma linha (Fase C do guia). Puro: nao renderiza nem le teclado.
//
// Regras:
//  - O texto so muda por uma operacao explicita (Insert, Backspace, Delete, Cut, Paste, Undo). Desenhar,
//    passar quadros ou ler o estado nunca altera o texto. Isso e o que impede o "campo apagando sozinho".
//  - Posicoes (caret e ancora) sao offsets de byte que sempre caem em fronteira de codepoint UTF-8.
//  - Backspace e Delete removem um codepoint inteiro, nao um byte.
//  - Caracteres de controle (< 0x20 e 0x7F) sao descartados ao inserir. Campo de uma linha nao aceita quebra.
//  - Sem IME, sem bidi, sem quebra de linha nesta etapa.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

class TextEditState {
public:
    TextEditState() = default;
    explicit TextEditState(std::string text);

    [[nodiscard]] const std::string& Text() const noexcept { return text_; }
    [[nodiscard]] std::size_t Caret() const noexcept { return caret_; }
    [[nodiscard]] std::size_t Anchor() const noexcept { return anchor_; }
    [[nodiscard]] bool HasSelection() const noexcept { return anchor_ != caret_; }
    [[nodiscard]] std::pair<std::size_t, std::size_t> SelectionRange() const noexcept;   // (inicio, fim)
    [[nodiscard]] std::string SelectedText() const;

    // Edicao (cada uma grava um ponto de desfazer antes de mudar o texto).
    void InsertText(std::string_view utf8);
    bool Backspace();
    bool Delete();
    std::string Cut();
    [[nodiscard]] std::string Copy() const { return SelectedText(); }
    void Paste(std::string_view utf8) { InsertText(utf8); }
    bool Undo();

    // Movimento. `extend` = true mantem a ancora e estende a selecao (Shift).
    void MoveLeft(bool extend = false) noexcept;
    void MoveRight(bool extend = false) noexcept;
    void MoveHome(bool extend = false) noexcept;
    void MoveEnd(bool extend = false) noexcept;
    void SelectAll() noexcept;

    // Limite de pontos de desfazer guardados.
    static constexpr std::size_t kMaxUndo = 256;

private:
    struct Snapshot {
        std::string text;
        std::size_t caret;
        std::size_t anchor;
    };

    void Record();
    void DeleteSelection();
    [[nodiscard]] std::size_t PrevBoundary(std::size_t pos) const noexcept;
    [[nodiscard]] std::size_t NextBoundary(std::size_t pos) const noexcept;
    void SetCaret(std::size_t pos, bool extend) noexcept;

    std::string text_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    std::vector<Snapshot> undo_;
};

} // namespace ToolEngine::SpriteUi::Widgets
