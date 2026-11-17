#pragma once

// Barra de rolagem visual (vertical ou horizontal) como modelo puro. O deslocamento vive em
// UIScrollViewComponent: a barra le e escreve esse estado, sem copia. Nao desenha: o chamador desenha o trilho
// e o polegar (ThumbRect) com o DrawList.
//
// Regras:
//  - Sem overflow (conteudo cabe na viewport), o polegar ocupa o trilho inteiro e nenhum gesto muda a rolagem.
//  - Comprimento do polegar = trilho * viewport / conteudo, nunca abaixo de 16 px (kMinimumThumbLength) nem
//    acima do trilho.
//  - Clique no trilho fora do polegar avanca uma pagina (uma viewport) na direcao do clique.
//  - Arrasto pelo polegar mantem o ponto de agarre: o polegar segue o ponteiro, sem saltar.
//  - Roda do mouse rola pelo delta informado, respeitando os limites do conteudo.
//  - O eixo da barra respeita allowScrollX/allowScrollY do componente: eixo bloqueado nao tem barra nem rolagem.
//  - Controle desabilitado ignora ponteiro e roda (retornam false).

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Widgets/Widgets.hpp"

#include <cstdint>

namespace ToolEngine::SpriteUi::Widgets {

enum class ScrollAxis : std::uint8_t { Vertical, Horizontal };

class ScrollBarControl {
public:
    ScrollAxis axis = ScrollAxis::Vertical;
    static constexpr float kMinimumThumbLength = 16.0f;
    float minThumbLength = kMinimumThumbLength; // Nunca menor que kMinimumThumbLength.
    bool disabled = false;

    // Retangulo do trilho em coordenadas locais do painel. O comprimento usado e o do eixo.
    void SetTrack(Rect track) noexcept { track_ = track; }
    [[nodiscard]] Rect Track() const noexcept { return track_; }

    // Mostrar a barra so faz sentido quando o conteudo passa da viewport neste eixo.
    [[nodiscard]] bool HasOverflow(const UIScrollViewComponent& view) const noexcept;

    // Retangulo do polegar para o estado atual da rolagem.
    [[nodiscard]] Rect ThumbRect(const UIScrollViewComponent& view) const noexcept;

    // Pressiona o ponteiro: no polegar, comeca o arrasto; no trilho, avanca uma pagina. Retorna true se a rolagem mudou.
    [[nodiscard]] bool PointerDown(Vec2 pointer, UIScrollViewComponent& view) noexcept;

    // Arrasto em curso: o polegar acompanha o ponteiro. Retorna true se a rolagem mudou.
    [[nodiscard]] bool PointerMove(Vec2 pointer, UIScrollViewComponent& view) noexcept;

    // Solta o polegar. Nao muda a rolagem.
    void PointerUp() noexcept { dragging_ = false; }

    // Roda do mouse: delta positivo rola para baixo (ou para a direita). Retorna true se a rolagem mudou.
    [[nodiscard]] bool Wheel(float delta, UIScrollViewComponent& view) noexcept;

    [[nodiscard]] bool Dragging() const noexcept { return dragging_; }

private:
    [[nodiscard]] float AlongPointer(Vec2 pointer) const noexcept;
    [[nodiscard]] float TrackStart() const noexcept;
    [[nodiscard]] float TrackLength() const noexcept;
    [[nodiscard]] float Viewport(const UIScrollViewComponent& view) const noexcept;
    [[nodiscard]] float Content(const UIScrollViewComponent& view) const noexcept;
    [[nodiscard]] float Offset(const UIScrollViewComponent& view) const noexcept;
    [[nodiscard]] float ThumbLength(const UIScrollViewComponent& view) const noexcept;
    [[nodiscard]] float ThumbStart(const UIScrollViewComponent& view) const noexcept;
    void SetOffset(UIScrollViewComponent& view, float offset) const noexcept;
    [[nodiscard]] bool Scroll(float delta, UIScrollViewComponent& view) const noexcept;
    [[nodiscard]] bool ScrollToThumbStart(float thumbStart, UIScrollViewComponent& view) const noexcept;

    Rect track_{};
    bool dragging_ = false;
    float grabOffset_ = 0.0f; // Distancia entre o ponteiro e o inicio do polegar no momento do agarre.
};

} // namespace ToolEngine::SpriteUi::Widgets
