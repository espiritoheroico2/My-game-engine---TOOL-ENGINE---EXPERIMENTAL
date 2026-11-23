#pragma once

// Roteador de paineis do editor. Decide quem recebe um evento de ponteiro usando a regra da SpriteUI (camada,
// zOrder, ordem de pintura), a mesma do clique. Cada painel SpriteUI registra a sua area no quadro. Um clique que cai
// numa area registrada so vai ao painel que a cobre por cima: menu aberto (Popup) bloqueia as janelas de tras.
//
// Pontos fora de qualquer area registrada continuam com o comportamento antigo (ImGui e os paineis decidem sozinhos).
// Por isso `mayReceive` devolve true quando nada registrado cobre o ponto.

#include "SpriteUi/Input/InputRouter.hpp"

#include <cstdint>
#include <vector>

namespace editor::panel_router {

namespace SU = ToolEngine::SpriteUi;

// Identificadores fixos dos paineis que participam do roteamento.
inline constexpr SU::UIHandle kMenuBarHandle{0xE0000001u, 1u};
inline constexpr SU::UIHandle kSettingsHandle{0xE0000002u, 1u};
inline constexpr SU::UIHandle kScenesHandle{0xE0000003u, 1u};
inline constexpr SU::UIHandle kAssetsHandle{0xE0000004u, 1u};
inline constexpr SU::UIHandle kCodingHandle{0xE0000005u, 1u};
inline constexpr SU::UIHandle kNarrativeHandle{0xE0000006u, 1u};

struct Area {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

class PanelRouter {
 public:
  // Limpa o quadro. Registrar na ordem de pintura: quem e registrado depois fica por cima dentro da mesma camada.
  void beginFrame() {
    targets_.clear();
    order_ = 0;
  }

  void add(SU::UIHandle handle, Area area, SU::UILayer layer, float zOrder = 0.f) {
    if (!(area.w > 0.f) || !(area.h > 0.f)) return;
    SU::Input::HitTarget target;
    target.handle = handle;
    target.localSize = {area.w, area.h};
    target.localToScreen.tx = area.x;
    target.localToScreen.ty = area.y;
    target.layer = static_cast<std::int32_t>(layer);
    target.zOrder = zOrder;
    target.paintOrder = ++order_;
    targets_.push_back(target);
  }

  // true: o painel `handle` pode receber o ponteiro em (x, y). false: outro painel registrado cobre o ponto.
  [[nodiscard]] bool mayReceive(SU::UIHandle handle, float x, float y) const {
    const SU::UIHandle top = SU::Input::UIEventRouter::TopmostAt(targets_, {x, y});
    return !top.IsValid() || top == handle;
  }

  // true quando um painel SpriteUI registrado cobre o ponto. Nesse caso o ImGui nao deve receber o clique.
  [[nodiscard]] bool coveredBySui(float x, float y) const {
    return SU::Input::UIEventRouter::TopmostAt(targets_, {x, y}).IsValid();
  }

 private:
  std::vector<SU::Input::HitTarget> targets_;
  std::uint64_t order_ = 0;
};

}  // namespace editor::panel_router
