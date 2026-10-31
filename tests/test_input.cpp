#include "test.hpp"
#include "tool/platform/window.hpp"

using namespace tool;

namespace {
constexpr u16 B(PadButton b) { return static_cast<u16>(b); }
constexpr u16 A(PadAxis a) { return static_cast<u16>(a); }
}

TEST(input_deadzone_radial) {
  CHECK(applyRadialDeadzone({0.1f, 0.05f}, 0.15f).x == 0.f);
  Vec2 full = applyRadialDeadzone({1.f, 0.f}, 0.15f);
  CHECK(std::abs(full.x - 1.f) < 1e-5f);
  Vec2 mid = applyRadialDeadzone({0.575f, 0.f}, 0.15f); // meio caminho
  CHECK(std::abs(mid.x - 0.5f) < 1e-4f);
}

TEST(input_gamepad_south_binds_jump_edges) {
  InputSystem in;
  in.bind("jump", {DeviceKind::PadButton, B(PadButton::South), 0});
  in.bind("jump", {DeviceKind::Key, 44, 0}); // espaco
  RawInput r;
  in.update(r);
  CHECK(!in.down("jump"));
  r.pads[0].connected = true; r.pads[0].buttons[B(PadButton::South)] = true;
  in.update(r);
  CHECK(in.pressed("jump") && in.down("jump"));
  in.update(r);
  CHECK(!in.pressed("jump") && in.down("jump"));
  r.pads[0].buttons[B(PadButton::South)] = false;
  in.update(r);
  CHECK(in.released("jump"));
}

TEST(input_keyboard_and_pad_same_action) {
  InputSystem in;
  in.bind("fire", {DeviceKind::Key, 10, 0});
  in.bind("fire", {DeviceKind::PadAxisPositive, A(PadAxis::RightTrigger), 0});
  RawInput r; r.keys[10] = true;
  in.update(r);
  CHECK(in.down("fire"));
  r.keys[10] = false; r.pads[0].connected = true; r.pads[0].axes[A(PadAxis::RightTrigger)] = 0.9f;
  in.update(r);
  CHECK(in.down("fire"));
}

TEST(input_vec2_move_normalized) {
  InputSystem in;
  in.bind("left", {DeviceKind::Key, 1, 0});  in.bind("left", {DeviceKind::PadAxisNegative, A(PadAxis::LeftX), 0});
  in.bind("right", {DeviceKind::Key, 2, 0}); in.bind("right", {DeviceKind::PadAxisPositive, A(PadAxis::LeftX), 0});
  in.bind("up", {DeviceKind::Key, 3, 0});    in.bind("up", {DeviceKind::PadAxisNegative, A(PadAxis::LeftY), 0});
  in.bind("down", {DeviceKind::Key, 4, 0});  in.bind("down", {DeviceKind::PadAxisPositive, A(PadAxis::LeftY), 0});
  in.bindVec2("move", "left", "right", "up", "down");
  RawInput r; r.keys[2] = true; r.keys[4] = true; // diagonal no teclado
  in.update(r);
  Vec2 v = in.vec2("move");
  CHECK(std::abs(std::sqrt(v.x * v.x + v.y * v.y) - 1.f) < 1e-4f); // nao passa de 1
  RawInput s; s.pads[0].connected = true; s.pads[0].axes[A(PadAxis::LeftX)] = -0.8f;
  in.update(s);
  CHECK(in.vec2("move").x < -0.5f);
}

TEST(input_disconnected_pad_is_ignored) {
  InputSystem in;
  in.bind("jump", {DeviceKind::PadButton, B(PadButton::South), 1});
  RawInput r; r.pads[1].buttons[B(PadButton::South)] = true; // lixo, nao conectado
  in.update(r);
  CHECK(!in.down("jump"));
  CHECK(!in.padConnected(1));
}

TEST(platform_null_window_events) {
  NullWindow w({"t", 800, 600, true, true});
  int resizes = 0; u32 lastW = 0;
  w.onEvent = [&](const WindowEvent& e) { if (e.type == WindowEventType::Resize) { ++resizes; lastW = e.width; } };
  RawInput in;
  w.injected.keys[5] = true;
  CHECK(w.pump(in) && in.keys[5]);
  w.resize(1024, 768);
  u32 a, b; w.size(a, b);
  CHECK(resizes == 1 && lastW == 1024 && a == 1024 && b == 768);
  w.requestClose();
  CHECK(!w.pump(in));
}
