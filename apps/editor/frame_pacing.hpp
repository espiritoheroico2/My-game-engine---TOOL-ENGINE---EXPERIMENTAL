// ToolEngine Editor - ritmo dos quadros: decide se o laco principal monta e apresenta um quadro agora ou espera.
//
// Funcao pura, sem SDL: o main.cpp monta as entradas (houve evento? Play? alguma vista 3D ocupada? quadro pedido? proximo
// prazo?) e age sobre a resposta. Ocioso e sem nada pedindo, o editor dorme ate o proximo evento ou prazo em vez de
// refazer a interface a cada ~16 ms.
//
// Regras:
//   - Evento, Play, vista 3D ocupada, quadro pedido ou modo "sempre desenhar" => desenha agora.
//   - Depois de um desses, desenha mais `settleFrames` quadros (a interface leva 1 ou 2 quadros para assentar:
//     areas do roteador vem do quadro anterior, pedidos aplicados depois do desenho).
//   - Prazo (cursor do relogio, atualizacao do Profiler...) ja vencido => desenha agora (sem quadros de assentamento).
//   - Senao espera: ate o prazo mais proximo, no maximo `maxWaitMs`.
#pragma once
#include <algorithm>
#include <limits>

namespace editor::frame_pacing {

inline constexpr double kNoDeadline = std::numeric_limits<double>::infinity();
inline constexpr double kMaxWaitMs = 250.0;   // teto da espera: o laco acorda para reconferir fontes que nao geram evento

struct Inputs {
  bool eventArrived = false;     // chegou (ou ja esta na fila) algum evento do SDL
  bool playing = false;          // Play: sempre desenha
  bool anyViewBusy = false;      // alguma vista 3D nao esta ociosa
  bool frameRequested = false;   // alguem pediu um quadro (Editor::requestFrame)
  bool alwaysRender = false;     // modos que nunca esperam (jogo exportado, roteiro de teste, primeiros quadros...)
  double deadlineSec = kNoDeadline;   // prazo pendente mais proximo (segundos, mesma base de nowSec)
  double nowSec = 0.0;
};

struct Decision {
  bool render = true;    // monta a interface e apresenta
  double waitMs = 0.0;   // so vale com render == false: quanto esperar (> 0, no maximo maxWaitMs)
};

class Pacer {
public:
  explicit Pacer(int settleFrames = 2, double maxWaitMs = kMaxWaitMs)
      : settleFrames_(std::max(0, settleFrames)), maxWaitMs_(std::max(1.0, maxWaitMs)) {}

  // So muda o estado quando a resposta e "desenhe": pode ser chamada de novo depois de uma espera.
  Decision decide(const Inputs& in) {
    if (in.alwaysRender || in.playing || in.anyViewBusy || in.eventArrived || in.frameRequested) {
      settle_ = settleFrames_;
      return {true, 0.0};
    }
    if (settle_ > 0) {
      --settle_;
      return {true, 0.0};
    }
    const double leftMs = (in.deadlineSec - in.nowSec) * 1000.0;
    if (leftMs <= 0.0) return {true, 0.0};   // prazo vencido
    return {false, std::min(leftMs, maxWaitMs_)};
  }

  int settleLeft() const { return settle_; }

private:
  int settleFrames_;
  double maxWaitMs_;
  int settle_ = 0;
};

}  // namespace editor::frame_pacing
