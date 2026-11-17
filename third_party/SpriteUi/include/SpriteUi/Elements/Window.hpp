#pragma once

// Janela (Window): um contêiner com barra de titulo, botoes de minimizar e fechar, e uma area de conteudo.
//
// Partes: o fundo e o contorno da janela; a barra de titulo no topo (fundo e, sobre ela, o rotulo que o chamador
// desenha em TitleBarRect()); os botoes de controle, a direita da barra (minimizar e fechar, com traco em linha).
// O conteudo fica nos filhos, arranjados na area de ContentRect(). Para o conteudo ficar abaixo da barra, o
// padding superior deve ser pelo menos titleBarHeight. Os padding esquerdo e direito recuam os botoes na barra.
//
// Posicao e tamanho: a janela usa ancoras (0, 0), entao a caixa vem dos offsets. Mover e redimensionar mudam os
// offsets. Filhos nao sao arrastados junto com a barra: so a janela muda de lugar, e os filhos vao com ela
// porque estao dentro dela.
//
// Eventos (OnPointer):
//  - Pressionar em qualquer ponto da janela pede trazer para frente (broughtToFront). O chamador chama
//    ElementTree::BringToFront. Nada mais muda a ordem dos irmaos.
//  - Arrastar a barra de titulo move a janela, depois que o ponteiro andou o limite de arraste (4 px).
//  - Arrastar uma borda (faixa de resizeMargin px por dentro) ou um canto redimensiona. O tamanho nunca fica
//    abaixo de minSize: a borda oposta fica parada quando o minimo e atingido.
//  - Click (Down e Up sobre o mesmo botao) pede minimizar ou fechar. A janela nao se remove sozinha: quem decide
//    e o chamador.
//  - Cancel (captura perdida) desfaz o arraste ou o redimensionamento.
//
// Exemplo:
//   auto id = tree.Create<Window>(root);
//   auto* win = tree.GetAs<Window>(id);
//   win->title = "Propriedades";
//   win->transform.offsetMax = {320, 240};
//   win->padding = {8, 8, 8, 8};
//   win->padding.top = win->titleBarHeight;   // conteudo abaixo da barra

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct WindowStyle {
    float cornerRadius = 0.0f; // Raio dos cantos da janela e da barra de titulo; zero = retos.
    Vec4 frame{0.13f, 0.14f, 0.17f, 1.0f};
    Vec4 frameBorder{0.3f, 0.32f, 0.38f, 1.0f};
    Vec4 titleBar{0.18f, 0.2f, 0.26f, 1.0f};
    Vec4 buttonBackground{0.22f, 0.24f, 0.3f, 1.0f};
    Vec4 glyph{0.9f, 0.92f, 0.95f, 1.0f};
    float borderWidth = 1.0f;
    float glyphHalfSize = 4.0f;
    float glyphThickness = 1.5f;
};

enum class WindowButton : std::uint8_t { None, Minimize, Close };

// Bits de borda para redimensionar. Um canto e a soma de duas bordas.
enum WindowEdge : std::uint8_t {
    EdgeNone = 0,
    EdgeLeft = 1,
    EdgeRight = 2,
    EdgeTop = 4,
    EdgeBottom = 8,
};

struct WindowEvents {
    bool broughtToFront = false;
    bool moved = false;
    bool resized = false;
    bool minimizeRequested = false;
    bool closeRequested = false;
};

class Window final : public Element {
  public:
    std::string title;             // Texto da barra; o chamador o desenha em TitleBarRect().
    Vec2 minSize{150.0f, 100.0f};  // Tamanho minimo no redimensionamento.
    float titleBarHeight = 30.0f;
    float resizeMargin = 6.0f;     // Faixa de cada borda que redimensiona.
    float buttonSize = 20.0f;
    float buttonGap = 4.0f;

    // Trata um evento de ponteiro. Ver a regra no topo do arquivo.
    [[nodiscard]] WindowEvents OnPointer(const PointerEvent& event) noexcept;

    // Faixa do topo da janela, com a largura da borda externa.
    [[nodiscard]] Rect TitleBarRect() const noexcept;

  private:
    enum class Drag : std::uint8_t { None, Pending, Move, Resize };

    void SetRect(Rect next) noexcept;

    Drag drag_ = Drag::None;
    Vec2 grab_{0.0f, 0.0f};
    Rect start_{};
    std::uint8_t edges_ = EdgeNone;
    WindowButton pressedButton_ = WindowButton::None;
};

// Retangulo de um botao de controle (dentro da barra, recuado pelo padding). Ver WindowButtonAt.
[[nodiscard]] Rect WindowButtonRect(const Window& window, WindowButton button) noexcept;

// Botao sob o ponto, ou WindowButton::None.
[[nodiscard]] WindowButton WindowButtonAt(const Window& window, Vec2 point) noexcept;

// Bordas que o ponto toca, dentro da faixa resizeMargin. EdgeNone quando nenhuma.
[[nodiscard]] std::uint8_t WindowEdgesAt(const Window& window, Vec2 point) noexcept;

// Retangulo da janela depois de arrastar a borda (ou canto) em edges por delta, a partir de start. Respeita
// minSize: quando o minimo e atingido, a borda oposta fica onde estava.
[[nodiscard]] Rect ResizedWindowRect(Rect start, std::uint8_t edges, Vec2 delta, Vec2 minSize) noexcept;

// Itens da janela: fundo, contorno, barra de titulo e botoes com seus tracos. Nao desenha o rotulo do titulo.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildWindowItems(const Window& window, const WindowStyle& style,
                                                                    float zOrder, std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
