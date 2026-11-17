#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "SpriteUi/Widgets/Menu.hpp"
#include "SpriteUi/Widgets/ScrollBar.hpp"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

namespace {

bool near(float a, float b, float epsilon = 0.01f) {
    return std::fabs(a - b) <= epsilon;
}

void testColorPicker() {
    using namespace ToolEngine::SpriteUi;
    using namespace ToolEngine::SpriteUi::Widgets;

    ColorPickerControl picker;
    assert(picker.SetColor({1.0f, 0.0f, 0.0f, 1.0f}));
    assert(picker.ChannelText(ColorChannel::Red) == "255");
    assert(picker.ChannelText(ColorChannel::Green) == "0");
    assert(picker.DragHue(1.0f / 3.0f));
    Vec4 color = picker.Sample();
    assert(near(color.x, 0.0f) && near(color.y, 1.0f) && near(color.z, 0.0f));
    assert(picker.DragSaturationValue(0.5f, 0.5f));
    color = picker.Sample();
    assert(near(color.x, 0.25f) && near(color.y, 0.5f) && near(color.z, 0.25f));

    ColorPickerStyle style;
    const ColorPickerLayout layout = LayoutColorPicker({{4.0f, 8.0f}, {220.0f, 190.0f}}, style);
    assert(layout.saturationValue.size.y > 0.0f);
    assert(layout.hue.position.y >= layout.saturationValue.position.y + layout.saturationValue.size.y);
    for (const Rect& field : layout.channelFields) {
        assert(field.size.x > 0.0f && field.size.y > 0.0f);
        assert(field.position.y + field.size.y <= 198.0f);
    }

    const auto items = BuildColorPickerItems(picker, layout, UIHandle{0, 1}, style, 0.0f, 0);
    assert(items.size() >= 32u * 16u + 24u);
    for (const auto& item : items) assert(item.handle.IsValid());
}

void testMenu() {
    using namespace ToolEngine::SpriteUi::Widgets;

    MenuControl menu;
    menu.Open({{"Open", {}, MenuItemKind::Action, false},
               {"", {}, MenuItemKind::Separator, false},
               {"Unavailable", {}, MenuItemKind::Action, true},
               {"Close", {}, MenuItemKind::Action, false}});
    assert(menu.IsOpen());
    assert(menu.Highlighted(0) == 0);
    assert(menu.Click(0, 1) == MenuEvent::None);
    assert(menu.Click(0, 2) == MenuEvent::None);
    assert(menu.Click(0, 3) == MenuEvent::Activated);
    assert(menu.ActivationPath() == std::vector<std::size_t>{3});
    menu.Close();
    assert(!menu.IsOpen());
}

void testScrollBar() {
    using namespace ToolEngine::SpriteUi;
    using namespace ToolEngine::SpriteUi::Widgets;

    UIScrollViewComponent view;
    view.contentSize = {100.0f, 1000.0f};
    view.viewportSize = {100.0f, 100.0f};
    ScrollBarControl bar;
    bar.SetTrack({{90.0f, 0.0f}, {10.0f, 100.0f}});
    assert(bar.HasOverflow(view));
    assert(bar.Wheel(120.0f, view));
    assert(near(view.scrollOffset.y, 120.0f));
    // Polegar fica perto do topo com a rolagem em 120 (y entre ~11 e ~27). Clique no polegar inicia o arrasto;
    // no trilho (y=95) o clique avancaria uma pagina, como a SpriteUI atual faz.
    assert(!bar.PointerDown({95.0f, 20.0f}, view));
    assert(bar.Dragging());
    assert(bar.PointerMove({95.0f, 50.0f}, view));
    assert(view.scrollOffset.y > 0.0f && view.scrollOffset.y < 900.0f);
    bar.PointerUp();
    assert(!bar.Dragging());
}

} // namespace

int main() {
    testColorPicker();
    testMenu();
    testScrollBar();
}
