// Roteamento de paineis: menu aberto bloqueia o Settings; fora das areas registradas, nada muda.
#include "panel_router.hpp"

#include <cstdio>

namespace {

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr, int line) {
  ++run;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "[FAIL] line %d: %s\n", line, expr);
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

using namespace editor::panel_router;
namespace SU = ToolEngine::SpriteUi;

}  // namespace

int main() {
  // Barra do menu no topo, Settings abaixo, popup do menu aberto sobre o Settings.
  {
    PanelRouter r;
    r.beginFrame();
    r.add(kMenuBarHandle, {0.f, 0.f, 800.f, 24.f}, SU::UILayer::Docked);
    r.add(kSettingsHandle, {0.f, 24.f, 800.f, 300.f}, SU::UILayer::Docked);
    CHECK(r.mayReceive(kSettingsHandle, 100.f, 100.f));
    CHECK(r.mayReceive(kMenuBarHandle, 10.f, 10.f));
    CHECK(!r.mayReceive(kSettingsHandle, 10.f, 10.f));          // barra do menu cobre o ponto
    CHECK(r.coveredBySui(10.f, 10.f));

    // Popup aberto: um fundo na camada Popup cobre tudo, e o menu fica acima do fundo.
    r.beginFrame();
    r.add(kMenuBarHandle, {0.f, 0.f, 800.f, 24.f}, SU::UILayer::Docked);
    r.add(kSettingsHandle, {0.f, 24.f, 800.f, 300.f}, SU::UILayer::Docked);
    r.add(kMenuBarHandle, {0.f, 0.f, 800.f, 600.f}, SU::UILayer::Popup);       // fundo de captura
    r.add(kMenuBarHandle, {0.f, 24.f, 200.f, 150.f}, SU::UILayer::Popup);      // lista do menu
    CHECK(!r.mayReceive(kSettingsHandle, 500.f, 100.f));       // clique fora do menu nao chega ao Settings
    CHECK(r.mayReceive(kMenuBarHandle, 100.f, 100.f));         // dentro da lista: o menu recebe
    CHECK(r.mayReceive(kMenuBarHandle, 500.f, 100.f));         // fundo: o menu recebe para fechar-se
  }

  // Fora de qualquer area registrada: ninguem bloqueia (ImGui e paineis decidem).
  {
    PanelRouter r;
    r.beginFrame();
    r.add(kSettingsHandle, {0.f, 24.f, 800.f, 300.f}, SU::UILayer::Docked);
    CHECK(!r.coveredBySui(10.f, 500.f));
    CHECK(r.mayReceive(kSettingsHandle, 10.f, 500.f));
  }

  // Area vazia nao e registrada.
  {
    PanelRouter r;
    r.beginFrame();
    r.add(kSettingsHandle, {0.f, 0.f, 0.f, 10.f}, SU::UILayer::Docked);
    CHECK(!r.coveredBySui(5.f, 5.f));
  }

  std::printf("panel router: %d/%d checks passed\n", run - failed, run);
  return failed == 0 ? 0 : 1;
}
