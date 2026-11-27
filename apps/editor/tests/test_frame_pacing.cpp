// Testes sem janela: decisao de ritmo dos quadros (frame_pacing.hpp). Roda no ctest como Editor.FramePacing.
#include "frame_pacing.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what);
  if (!ok) ++gFailures;
}

bool Near(double a, double b) { return a > b - 1e-6 && a < b + 1e-6; }

}  // namespace

int main() {
  namespace fp = editor::frame_pacing;

  {
    fp::Pacer p(0);
    fp::Inputs in;
    in.nowSec = 10.0;
    const fp::Decision d = p.decide(in);
    Check(!d.render && Near(d.waitMs, fp::kMaxWaitMs), "ocioso sem prazos: espera o teto");
  }
  {
    fp::Pacer p(0);
    fp::Inputs in;
    in.eventArrived = true;
    Check(p.decide(in).render, "evento forca um quadro");
  }
  {
    fp::Pacer p(0);
    fp::Inputs in;
    in.playing = true;
    Check(p.decide(in).render, "Play sempre desenha");
    in.playing = false;
    in.anyViewBusy = true;
    Check(p.decide(in).render, "vista 3D ocupada desenha");
    in.anyViewBusy = false;
    in.alwaysRender = true;
    Check(p.decide(in).render, "modo sempre desenhar desenha");
  }
  {
    fp::Pacer p(0);
    fp::Inputs in;
    in.nowSec = 1.0;
    in.deadlineSec = 1.005;
    fp::Decision d = p.decide(in);
    Check(!d.render && Near(d.waitMs, 5.0), "prazo a 5 ms: espera 5 ms");
    in.nowSec = 1.0051;
    d = p.decide(in);
    Check(d.render, "prazo vencido: desenha");
    in.deadlineSec = fp::kNoDeadline;
    d = p.decide(in);
    Check(!d.render, "sem settle, depois do prazo volta a esperar");
  }
  {
    fp::Pacer p(0);
    fp::Inputs in;
    in.nowSec = 1.0;
    in.deadlineSec = 1.0 + 10.0;
    const fp::Decision d = p.decide(in);
    Check(!d.render && Near(d.waitMs, fp::kMaxWaitMs), "prazo distante: espera no maximo o teto");
  }
  {
    fp::Pacer p(0, 100.0);
    fp::Inputs in;
    const fp::Decision d = p.decide(in);
    Check(Near(d.waitMs, 100.0), "teto configuravel");
  }
  {   // requestFrame: o chamador limpa o pedido ao desenhar; aqui o quadro seguinte (sem pedido) volta a esperar
    fp::Pacer p(0);
    fp::Inputs in;
    in.frameRequested = true;
    Check(p.decide(in).render, "quadro pedido desenha");
    in.frameRequested = false;
    Check(!p.decide(in).render, "pedido limpo: volta a esperar");
  }
  {   // quadros de assentamento depois de um evento
    fp::Pacer p(2);
    fp::Inputs in;
    in.eventArrived = true;
    Check(p.decide(in).render, "evento desenha");
    in.eventArrived = false;
    Check(p.decide(in).render, "assentamento 1");
    Check(p.decide(in).render, "assentamento 2");
    Check(!p.decide(in).render, "depois dos quadros de assentamento espera");
  }
  {   // decidir "espere" nao consome assentamento nem estado
    fp::Pacer p(0);
    fp::Inputs in;
    p.decide(in);
    p.decide(in);
    Check(p.settleLeft() == 0, "esperar nao muda o estado");
  }

  std::printf("%s\n", gFailures == 0 ? "FramePacing OK" : "FramePacing FALHOU");
  return gFailures == 0 ? 0 : 1;
}
