#pragma once

// Icone "?" da moldura (SpriteUI): um circulo com o simbolo ao lado do nome da janela. O circulo e um painel com
// cantos no raio da metade do lado, entao vira redondo. A geometria (onde fica e o que o clique faz) esta em
// window_frame.hpp; aqui so o desenho, para o painel de cada janela chamar com as suas cores e a sua ordem de z.
// Clique: window_frame::Event::Help, que o painel transforma no pedido de abrir a Help na secao da janela.

#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <cstdint>
#include <vector>

namespace editor::window_help_icon {

namespace SU = ToolEngine::SpriteUi;
namespace El = ToolEngine::SpriteUi::Elements;

// Itens do circulo. paint e o contador de ordem de pintura do quadro (igual aos outros builders).
inline std::vector<SU::Rendering::UIRenderItem> BuildCircleItems(SU::UIHandle owner, SU::Rect box, SU::Vec4 background, float z,
                                                                 std::uint64_t& paint) {
  El::PanelStyle style;
  style.cornerRadius = 0.5f * box.size.y;   // metade do lado: circulo
  style.background = background;
  style.border = background;
  style.borderWidth = 0.f;
  El::Panel circle;
  circle.border = box;
  std::vector<SU::Rendering::UIRenderItem> items = El::BuildPanelItems(circle, style, z, paint);
  paint += items.size() + 1;
  for (SU::Rendering::UIRenderItem& item : items) item.handle = owner;
  return items;
}

}  // namespace editor::window_help_icon
