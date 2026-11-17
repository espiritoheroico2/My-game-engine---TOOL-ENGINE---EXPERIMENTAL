#pragma once

// Icones de encaixe (guias) mostrados durante o arrasto de uma janela sobre uma folha do dock.
//
// Sao cinco icones: esquerda, direita, topo, base e centro (juntar como aba). Os itens vao para a camada
// UILayer::DragPreview, por cima de tudo, com transparencia media (45 a 60 por cento). O slot sob o ponteiro
// (highlighted) usa a cor de destaque e a opacidade maior.
//
// Quem chama decide quando desenhar: so para a folha sob o ponteiro, com o slot vindo de
// DockDragController::Track (que ja tem a histerese). Este modulo nao le ponteiro nem muda a arvore.
//
// Exemplo de jogo: arrastar a janela de inventario sobre a folha do mapa mostra as cinco guias so sobre o mapa;
// ao chegar na borda direita, a guia direita fica mais forte e o encaixe lateral e o que sera feito.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Docking.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Widgets {

struct DockGuideStyle {
    float iconSize = 28.0f;          // lado do icone, em pixels logicos
    float edgeInset = 12.0f;         // distancia das guias laterais e de topo/base para a borda da folha
    float idleAlpha = 0.45f;         // opacidade dos icones comuns (45 por cento)
    float highlightAlpha = 0.60f;    // opacidade do icone sob o ponteiro (60 por cento)
    Vec4 color{0.55f, 0.75f, 1.0f, 1.0f};          // cor RGB linear dos icones comuns
    Vec4 highlightColor{0.95f, 0.8f, 0.3f, 1.0f};  // cor RGB linear do icone destacado
};

// Itens dos cinco icones, na ordem: Left, Right, Top, Bottom, Center. Vazio se a folha nao tem area finita ou o
// estilo tem valores invalidos. highlighted = None deixa todos com a opacidade comum.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildDockGuideItems(
    UIHandle owner, Rect leaf, DockSlot highlighted, const DockGuideStyle& style = {},
    std::uint64_t paintOrder = 0);

} // namespace ToolEngine::SpriteUi::Widgets
