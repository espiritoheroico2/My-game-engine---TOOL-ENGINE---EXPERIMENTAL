#pragma once

// Operacoes sobre a arvore de docking que aplicam a acao devolvida pelo DockDragController (Fase B, item 5).
// Regras:
//  - A folha de uma janela e identificada pelo conteudo visivel (conteudo unico ou aba ativa). Qualquer aba
//    do grupo serve para achar a folha.
//  - Antes de tirar uma janela da arvore, o alvo do encaixe e verificado. Assim um encaixe invalido nao deixa
//    a janela solta.
//  - Remover a ultima janela de um filho deixa o pai com o irmao (colapso), sem folhas vazias.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/Docking.hpp"

namespace ToolEngine::SpriteUi::Widgets {

// Folha que contem `content` (como conteudo unico ou como aba). nullptr se nao existe.
[[nodiscard]] UIDockNode* FindLeafContaining(UIDockNode& root, UIHandle content) noexcept;

// Tira a janela da arvore e colapsa folhas vazias. false se a janela nao esta na arvore.
bool RemoveFromDock(UIDockNode& root, UIHandle window) noexcept;

// Aplica a acao do controlador a uma janela:
//  - Dock: divide a folha alvo no slot indicado e coloca a janela no novo filho.
//  - Tab: junta a janela como aba da folha alvo.
//  - Float: tira a janela da arvore. O chamador cria a janela flutuante.
//  - None: nao muda nada.
// Retorna false sem alterar a arvore quando o alvo nao existe, e quando a janela cairia no proprio lugar.
bool ApplyDropResult(UIDockNode& root, UIHandle window, const DockDropResult& drop, float ratio = 0.5f) noexcept;

// Arrasta o divisor de um no dividido. offsetPx e a posicao do divisor, medida a partir do inicio da area do
// no (bounds, preenchido por Layout). Cada lado fica com pelo menos minSidePx. Exemplo: um no de 300 px de
// largura com minimo de 80 px: arrastar para 10 px deixa o lado esquerdo com 80 px. Falso se o no nao e
// dividido, se a area nao cabe dois minimos ou se o valor nao e finito.
bool DragSplitDivider(UIDockNode& node, float offsetPx, float minSidePx) noexcept;

} // namespace ToolEngine::SpriteUi::Widgets
