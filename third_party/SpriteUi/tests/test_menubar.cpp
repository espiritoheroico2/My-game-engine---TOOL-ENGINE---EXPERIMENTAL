#include "SpriteUi/Widgets/MenuBar.hpp"

#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Widgets;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

std::vector<MenuBarEntry> Menus() {
    return {
        {"File", {{"New", {}, MenuItemKind::Action, false},
                  {"", {}, MenuItemKind::Separator, false},
                  {"Disabled", {}, MenuItemKind::Action, true},
                  {"Save", "Ctrl+S", MenuItemKind::Action, false}}},
        {"Edit", {{"Undo", "Ctrl+Z", MenuItemKind::Action, false}}},
    };
}

void TestLayoutClampsHeadersAndPopupToViewport() {
    current = "TestLayoutClampsHeadersAndPopupToViewport";
    MenuBar bar;
    bar.SetMenus(Menus());
    bar.popupWidth = 400.0f;
    bar.Layout(Rect{{0, 0}, {120, 80}}, 20.0f, {90.0f, 90.0f});
    CHECK(bar.HeaderRect(0).size.x == 90.0f);
    CHECK(bar.HeaderRect(1).size.x == 30.0f);

    CHECK(bar.PointerDown({10, 10}).handled);
    const Rect popup = bar.PopupRect();
    CHECK(popup.size.x == 120.0f);
    CHECK(popup.position.y + popup.size.y <= 80.0f);
}

void TestPointerSelectsEnabledActionOnly() {
    current = "TestPointerSelectsEnabledActionOnly";
    MenuBar bar;
    bar.SetMenus(Menus());
    bar.Layout(Rect{{0, 0}, {300, 120}}, 20.0f, {60.0f, 60.0f});
    CHECK(bar.PointerDown({10, 10}).handled);

    Rect row = bar.RowRect(0, 0);
    CHECK(bar.PointerDown({row.position.x + 5.0f, row.position.y + 5.0f}).handled);
    MenuBarEvents activated = bar.PointerUp({row.position.x + 5.0f, row.position.y + 5.0f});
    CHECK(activated.activated);
    CHECK(activated.menuIndex == 0 && activated.itemIndex == 0);
    CHECK(!bar.IsOpen());

    CHECK(bar.PointerDown({10, 10}).handled);
    row = bar.RowRect(0, 2);
    CHECK(bar.PointerDown({row.position.x + 5.0f, row.position.y + 5.0f}).handled);
    activated = bar.PointerUp({row.position.x + 5.0f, row.position.y + 5.0f});
    CHECK(!activated.activated);
    CHECK(bar.IsOpen());
}

void TestKeyboardSkipsSeparatorsAndDisabledItems() {
    current = "TestKeyboardSkipsSeparatorsAndDisabledItems";
    MenuBar bar;
    bar.SetMenus(Menus());
    bar.Layout(Rect{{0, 0}, {300, 160}}, 20.0f, {60.0f, 60.0f});

    CHECK(bar.PressAlt().handled);
    CHECK(bar.ActiveItem() == 0);
    CHECK(bar.KeyPressed(MenuKey::Down).handled);
    CHECK(bar.ActiveItem() == 3);
    CHECK(bar.KeyPressed(MenuKey::Home).handled);
    CHECK(bar.ActiveItem() == 0);
    CHECK(bar.KeyPressed(MenuKey::Right).handled);
    CHECK(bar.ActiveMenu() == 1);
    const MenuBarEvents activated = bar.KeyPressed(MenuKey::Enter);
    CHECK(activated.activated);
    CHECK(activated.menuIndex == 1 && activated.itemIndex == 0);
    CHECK(!bar.IsOpen());
}

} // namespace

int main() {
    TestLayoutClampsHeadersAndPopupToViewport();
    TestPointerSelectsEnabledActionOnly();
    TestKeyboardSkipsSeparatorsAndDisabledItems();
    std::printf("SpriteUI MenuBar tests: %d checks, %d failures\n", run, failed);
    return failed == 0 ? 0 : 1;
}
