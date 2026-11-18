#pragma once

// Pilha de camadas e ordem de desenho. Uma unica regra vale para o desenho e para o acerto de ponteiro: quem
// e desenhado por ultimo fica por cima e e o primeiro a receber o ponteiro.
//
// Chave de ordem, em ordem de prioridade (o maior vence):
//   1. layer      camada explicita (UILayer). Camadas altas ficam sempre por cima das baixas.
//   2. zOrder     ordem dentro da camada. Em um elemento, e o efetivo: a soma dos zOrder dos ancestrais e do proprio.
//                 Assim, subir uma janela sobe a janela inteira, e os filhos seguem o pai.
//   3. paintOrder ordem de pintura da arvore (pre-ordem): irmaos mais tarde ficam por cima.
//   4. handle     desempate deterministico.
//
// Exemplo de jogo: um menu de contexto aberto sobre uma janela de inventario fica na camada Popup; mesmo que a
// janela de inventario tenha zOrder 5 na camada Floating, o menu vence. Ao arrastar a janela, ela vai para a
// camada DragPreview e passa por cima de todas as outras, inclusive as janelas flutuantes.

#include <cstdint>
#include <tuple>

namespace ToolEngine::SpriteUi {

// Camadas padrao, de baixo para cima. Valores inteiros para caber em UIRenderItem::layer e HitTarget::layer.
enum class UILayer : std::int32_t {
    Background = 0,   // Fundo da tela e paineis sem encaixe.
    Docked = 1,       // Paineis encaixados (dock e divisores).
    Floating = 2,     // Janelas flutuantes.
    Popup = 3,        // Menus, listas suspensas e popups.
    Tooltip = 4,      // Dicas de ferramenta.
    DragPreview = 5   // Elemento sendo arrastado. Sempre por cima de tudo.
};

// Nome curto de uma camada, para depuracao e testes.
[[nodiscard]] constexpr const char* LayerName(UILayer layer) noexcept {
    switch (layer) {
    case UILayer::Background: return "Background";
    case UILayer::Docked: return "Docked";
    case UILayer::Floating: return "Floating";
    case UILayer::Popup: return "Popup";
    case UILayer::Tooltip: return "Tooltip";
    case UILayer::DragPreview: return "DragPreview";
    }
    return "Unknown";
}

// Campos que definem a ordem de um item (desenho) ou de um alvo (acerto).
struct DrawOrderKey {
    std::int32_t layer = 0;
    float zOrder = 0.0f;
    std::uint64_t paintOrder = 0;
    std::uint64_t handle = 0; // UIHandle::Pack()
};

// Verdadeiro quando a chave a fica por cima da chave b. O RenderExtractor ordena com esta mesma regra e o
// roteador de entrada escolhe o alvo com ela: os dois nunca divergem.
[[nodiscard]] inline bool DrawsAbove(const DrawOrderKey& a, const DrawOrderKey& b) noexcept {
    return std::tuple{a.layer, a.zOrder, a.paintOrder, a.handle} >
           std::tuple{b.layer, b.zOrder, b.paintOrder, b.handle};
}

} // namespace ToolEngine::SpriteUi
