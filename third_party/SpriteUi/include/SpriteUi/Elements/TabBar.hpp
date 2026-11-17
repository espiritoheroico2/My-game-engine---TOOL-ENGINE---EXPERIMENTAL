#pragma once

// Barra de abas (Tab Bar): uma faixa horizontal de abas e, abaixo dela, a area do painel da aba ativa.
//
// Partes: o fundo da faixa, uma aba por rotulo (fundo, com a aba ativa mais clara), a faixa de destaque sob a
// aba ativa e o contorno de foco na aba ativa quando a barra tem foco. O texto de cada aba e desenhado pelo
// chamador, dentro de TabRect(). O estado (rotulos e aba ativa) vem do modelo TabBarControl (Controls.hpp).
//
// Abas: nao ha numero maximo. Cada aba tem a largura tabWidth; a faixa cresce com o numero de abas e, quando
// elas nao cabem, rola na horizontal para manter a aba ativa inteira na tela.
//
// Paineis: cada aba tem um painel, que e um filho da barra na mesma ordem (filho i e o painel da aba i). So o
// painel ativo e visivel. Os paineis ocultos mantem o estado dos controles e a ultima caixa: ArrangePanel()
// arranja so o painel ativo. O chamador nao deve chamar tree.Arrange sobre a barra, senao os ocultos tambem
// sao arranjados. Ocultos nao sao desenhados nem recebem clique: o chamador so desenha o painel ativo.
//
// Eventos: Click numa aba a torna ativa (uma so por vez). Setas Esquerda e Direita, com foco, movem a aba ativa
// de uma em uma e param nas pontas (sem volta). Enter, com foco, confirma a aba ativa (evento Activate).
// Desabilitada, a barra nao reage a nada.
//
// Exemplo:
//   auto id = tree.Create<TabBar>(parent);
//   auto* tabs = tree.GetAs<TabBar>(id);
//   tabs->control = Widgets::TabBarControl({"Geral", "Video", "Audio"});
//   tabs->states.focused = true;
//   auto events = tabs->KeyPressed(TabKey::Right);   // Aba ativa vai para "Video".

#include "SpriteUi/Core/Math.hpp"
#include "SpriteUi/Elements/Element.hpp"
#include "SpriteUi/Elements/ElementTree.hpp"
#include "SpriteUi/Elements/PointerEvent.hpp"
#include "SpriteUi/Elements/PressGesture.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Widgets/Controls.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Elements {

struct TabBarStyle {
    float cornerRadius = 0.0f; // Raio dos cantos da faixa e das abas; zero = retos.
    Vec4 strip{0.09f, 0.09f, 0.11f, 1.0f};
    Vec4 tab{0.14f, 0.14f, 0.17f, 1.0f};
    Vec4 activeTab{0.2f, 0.22f, 0.28f, 1.0f};
    Vec4 activeMark{0.3f, 0.6f, 1.0f, 1.0f};
    Vec4 focusRing{0.95f, 0.8f, 0.3f, 1.0f};
    float activeMarkHeight = 2.0f;
    float focusRingWidth = 2.0f;
};

enum class TabKey : std::uint8_t { Left, Right, Enter };

struct TabEvents {
    bool changed = false;    // A aba ativa mudou.
    bool activated = false;  // Enter sobre a aba ativa.
};

class TabBar final : public Element {
  public:
    Widgets::TabBarControl control;
    float tabWidth = 96.0f;     // Largura de cada aba.
    float tabGap = 2.0f;        // Folga entre abas vizinhas.
    float stripPadding = 4.0f;  // Espaco entre a borda da faixa e as abas.
    float panelGap = 4.0f;      // Folga entre a faixa e o painel.
    float panelHeight = 0.0f;   // Altura da area do painel.
    float scrollX = 0.0f;       // Rolagem horizontal da faixa; sempre dentro de 0 e o maximo.

    // Click numa aba (o gesto completa dentro da barra): torna essa aba ativa. Fora das abas, nada.
    [[nodiscard]] TabEvents OnPointer(const PointerEvent& event) noexcept;

    // Tecla com foco. Setas movem a aba ativa; Enter confirma a aba ativa. Ver a regra no topo do arquivo.
    [[nodiscard]] TabEvents KeyPressed(TabKey key) noexcept;

    // Arranja so o painel da aba ativa na area do painel. Os paineis ocultos mantem a caixa anterior.
    void ArrangePanel(ElementTree& tree) const;

    // Area do painel: logo abaixo da faixa, com a largura da barra e altura panelHeight.
    [[nodiscard]] Rect PanelArea() const noexcept;

  private:
    bool MoveActive(int delta) noexcept;
    void EnsureActiveVisible() noexcept;

    PressGesture gesture_;
};

// Retangulo da aba i no canvas, ja com a rolagem da faixa. Pode estar parcialmente fora da faixa.
[[nodiscard]] Rect TabRect(const TabBar& tab, std::size_t index) noexcept;

// Indice da aba sob o ponto (canvas), ou NoControlIndex se o ponto cai fora das abas ou na folga entre elas.
[[nodiscard]] std::size_t TabAt(const TabBar& tab, Vec2 point) noexcept;

// Itens da faixa: fundo, as abas visiveis (so elas, nunca todas), faixa de destaque e contorno de foco
// na aba ativa. clipIndex recorta tudo na area da barra.
[[nodiscard]] std::vector<Rendering::UIRenderItem> BuildTabBarItems(const TabBar& tab, const TabBarStyle& style,
                                                                    std::uint32_t clipIndex, float zOrder,
                                                                    std::uint64_t paintOrder);

} // namespace ToolEngine::SpriteUi::Elements
