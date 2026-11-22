#include "spriteui_ecs.hpp"

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void Check(bool ok, const char* expression) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", expression);
  }
}
#define CHECK(expr) Check((expr), #expr)

using ToolEngine::SpriteUi::ElementVisual;
using ToolEngine::SpriteUi::FlexItem;
using ToolEngine::SpriteUi::LayoutDirection;
using ToolEngine::SpriteUi::Length;
using ToolEngine::SpriteUi::Rect;
using ToolEngine::SpriteUi::SizeMode;
using ToolEngine::SpriteUi::Vec2;

FlexItem Fixed(float width, float height) {
  FlexItem style;
  style.width = {SizeMode::Fixed, width};
  style.height = {SizeMode::Fixed, height};
  style.direction = LayoutDirection::Column;
  return style;
}

} // namespace

int main() {
  tool::World world;
  editor::spriteui_ecs::Registry ecs(world);

  ElementVisual visual;
  visual.tintPremultipliedLinear = {0.2f, 0.4f, 0.8f, 1.f};
  visual.focusable = true;

  FlexItem rootStyle;
  rootStyle.width = {SizeMode::Percent, 1.f};
  rootStyle.height = {SizeMode::Percent, 1.f};
  rootStyle.direction = LayoutDirection::Column;
  tool::Entity root = ecs.Create(rootStyle, {}, visual);
  tool::Entity child = ecs.Create(Fixed(120.f, 40.f), root, visual);
  CHECK(world.alive(root));
  CHECK(world.alive(child));
  CHECK(ecs.Count() == 2);
  CHECK(ecs.HandleOf(root).IsValid());
  CHECK(ecs.EntityOf(ecs.HandleOf(child)) == child);

  ecs.Layout({{0.f, 0.f}, {640.f, 360.f}});
  const auto* rootNode = world.get<editor::spriteui_ecs::UINode>(root);
  const auto* childNode = world.get<editor::spriteui_ecs::UINode>(child);
  CHECK(rootNode != nullptr);
  CHECK(childNode != nullptr);
  CHECK(ecs.Tree().Get(rootNode->handle)->node.rect.size.x == 640.f);
  CHECK(ecs.Tree().Get(rootNode->handle)->node.rect.size.y == 360.f);
  CHECK(ecs.Tree().Get(childNode->handle)->node.rect.size.x == 120.f);
  CHECK(ecs.Tree().Get(childNode->handle)->node.rect.size.y == 40.f);

  const auto extracted = ecs.Extract();
  CHECK(extracted.renderItems.size() == 2);
  CHECK(extracted.hitTargets.size() == 2);
  CHECK(extracted.hitTargets[1].focusable);

  CHECK(ecs.SetStyle(child, Fixed(200.f, 50.f)));
  ecs.Layout({{0.f, 0.f}, {640.f, 360.f}});
  CHECK(ecs.Tree().Get(childNode->handle)->node.rect.size.x == 200.f);
  CHECK(ecs.Tree().Get(childNode->handle)->node.rect.size.y == 50.f);

  CHECK(ecs.SetVisible(child, false));
  ecs.Layout({{0.f, 0.f}, {640.f, 360.f}});
  const auto invisible = ecs.Extract();
  CHECK(invisible.renderItems.size() == 1);
  CHECK(invisible.hitTargets.size() == 1);

  CHECK(ecs.SetVisible(child, true));
  CHECK(ecs.Destroy(root));
  CHECK(!world.alive(root));
  CHECK(!world.alive(child));
  CHECK(ecs.Count() == 0);
  CHECK(!ecs.HandleOf(root).IsValid());

  tool::Entity pooled = ecs.Create(Fixed(100.f, 100.f), {}, visual);
  world.destroy(pooled);
  ecs.PruneDestroyed();
  CHECK(ecs.Count() == 0);
  CHECK(!ecs.HandleOf(pooled).IsValid());

  std::printf("Editor.SpriteUiEcs: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
