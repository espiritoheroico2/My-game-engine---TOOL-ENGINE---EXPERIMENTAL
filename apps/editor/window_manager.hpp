// window_manager.hpp - Regras de janelas do editor: camadas, ordem, quem recebe o clique e quando uma janela pode ser
// arrastada ou desencaixada. Puro (sem SDL, sem desenho): a janela real so guarda o retangulo e chama estas regras.
//
// Por que existe: cada painel do editor tinha o proprio teste de clique e a propria ordem de desenho. Por isso um
// menu aberto deixava o clique passar para a janela de tras, a barra de titulo ficava atras de outros paineis, e
// qualquer clique na barra podia tirar uma janela do dock. Aqui a regra e uma so:
//
//   1. Camada (Layer) e a primeira chave. Camadas altas ficam sempre por cima das baixas:
//        Background 0 < Docked 1 < Floating 2 < Popup 3 < Tooltip 4 < DragPreview 5
//      Uma janela fora do dock e Floating: fica sempre por cima das encaixadas.
//   2. Dentro da camada, a ordem (zOrder) sobe quando a janela e clicada (bringToFront).
//   3. Popup aberto captura o ponteiro: clique fora dele fecha o popup e NAO chega as janelas de tras.
//   4. Arrastar comeca so no icone de alca (grip) a esquerda do titulo, nunca no resto da barra.
//   5. Uma janela encaixada so sai do dock depois de arrastar pelo grip alem de um limiar (evita saidas acidentais).
#pragma once

#include <cstdint>
#include <vector>

namespace editor::wm {

enum class Layer : std::int32_t {
  Background = 0,
  Docked = 1,
  Floating = 2,
  Popup = 3,
  Tooltip = 4,
  DragPreview = 5,
};

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  [[nodiscard]] bool contains(float px, float py) const noexcept {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

// Valor de retorno de pick(): nenhum alvo, ou clique capturado por um popup aberto (fecha o popup, nao atinge nada).
inline constexpr int kNoTarget = -1;
inline constexpr int kCapturedByPopup = -2;

struct WindowState {
  int id = 0;
  Rect rect{};
  bool docked = false;        // encaixada no dock: camada Docked
  bool visible = true;
  bool popup = false;         // menu ou lista suspensa: camada Popup e captura o ponteiro quando aberto
  float titleHeight = 24.f;   // altura da barra de titulo (topo da janela)
  float gripWidth = 14.f;     // largura do icone de alca, a esquerda do titulo
  int zOrder = 0;
};

struct DragState {
  bool active = false;        // o botao esta preso numa janela (clique comecou no grip)
  int id = 0;
  bool wasDocked = false;     // a janela estava encaixada quando o clique comecou
  bool moving = false;        // ja passou do limiar: a janela esta sendo movida
  float startX = 0.f, startY = 0.f;
};

class WindowManager {
 public:
  static constexpr float kUndockThreshold = 16.f;   // pixels de arrasto pelo grip antes de sair do dock

  // Cadastra ou atualiza uma janela. Janelas de mesmo id sao substituidas.
  void upsert(const WindowState& w);
  void remove(int id);
  void setOpenPopup(int id, bool open);   // popup aberto: captura cliques fora dele
  [[nodiscard]] const WindowState* find(int id) const;

  // Camada efetiva de uma janela: popup > docked/floating; janela em arraste vai para DragPreview.
  [[nodiscard]] Layer effectiveLayer(const WindowState& w) const noexcept;

  // Alvo do clique: o id da janela de maior chave (camada, zOrder, id), kNoTarget ou kCapturedByPopup.
  [[nodiscard]] int pick(float x, float y) const;

  // Sobe a janela para o topo da propria camada.
  void bringToFront(int id);

  // Grip de uma janela: icone a esquerda do titulo. So ele inicia arraste.
  [[nodiscard]] Rect gripRect(const WindowState& w) const noexcept;

  // Inicio do clique. Retorna true se o clique virou arraste (estava no grip). Clique fora do grip nao arrasta.
  bool beginPress(int id, float x, float y);
  // Movimento com o botao preso. Devolve true se a janela mudou de lugar neste passo.
  bool movePointer(float x, float y);
  // Soltar o botao. Retorna true se houve arraste (nao conta como clique para acoes da janela).
  bool endPress();

  [[nodiscard]] const DragState& drag() const noexcept { return drag_; }

 private:
  std::vector<WindowState> windows_;
  std::vector<int> openPopups_;
  DragState drag_;
  void applyDock(WindowState& w, bool docked);
};

}  // namespace editor::wm
