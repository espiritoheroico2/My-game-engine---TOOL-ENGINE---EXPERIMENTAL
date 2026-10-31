// Backend SDL3: desktop, Android e Web (Emscripten). Gamepad por posicao (South/East/West/North).
// UWP exige um backend proprio (SDL3 removeu WinRT) - ver docs.
#include "tool/platform/sdl_window.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace tool {

namespace {
constexpr SDL_GamepadButton kButtonMap[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,       SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,      SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,    SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_GAMEPAD_BUTTON_DPAD_LEFT,  SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
static_assert(sizeof(kButtonMap) / sizeof(kButtonMap[0]) == static_cast<size_t>(PadButton::Count));
constexpr SDL_GamepadAxis kAxisMap[] = {SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
                                        SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
} // namespace

struct SdlWindow::Impl {
  SDL_Window* window = nullptr;
  SDL_Gamepad* pads[kMaxPads] = {};
  SDL_JoystickID padIds[kMaxPads] = {};
  bool open = true;
};

SdlWindow::SdlWindow(const WindowDesc& d) : impl_(new Impl) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    ok_ = false;
    return;
  }
  SDL_WindowFlags flags = 0;
  if (d.resizable) flags |= SDL_WINDOW_RESIZABLE;
  impl_->window = SDL_CreateWindow(d.title.c_str(), static_cast<int>(d.width), static_cast<int>(d.height), flags);
  ok_ = impl_->window != nullptr;
}

SdlWindow::~SdlWindow() {
  for (auto* p : impl_->pads)
    if (p) SDL_CloseGamepad(p);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
  SDL_Quit();
  delete impl_;
}

bool SdlWindow::pump(RawInput& in) {
  std::memset(in.mouse, 0, sizeof(in.mouse));
  in.mouseDelta = {};
  in.mouseWheel = {};
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      impl_->open = false;
      if (onEvent) onEvent({WindowEventType::Close, 0, 0});
      break;
    case SDL_EVENT_WINDOW_RESIZED:
      if (onEvent) onEvent({WindowEventType::Resize, static_cast<u32>(e.window.data1), static_cast<u32>(e.window.data2)});
      break;
    case SDL_EVENT_MOUSE_MOTION: in.mouseDelta.x += e.motion.xrel; in.mouseDelta.y += e.motion.yrel; break;
    case SDL_EVENT_MOUSE_WHEEL: in.mouseWheel.x += e.wheel.x; in.mouseWheel.y += e.wheel.y; break;
    case SDL_EVENT_GAMEPAD_ADDED:
      for (size_t i = 0; i < kMaxPads; ++i)
        if (!impl_->pads[i]) {
          impl_->pads[i] = SDL_OpenGamepad(e.gdevice.which);
          impl_->padIds[i] = e.gdevice.which;
          if (onEvent) onEvent({WindowEventType::DeviceChanged, 0, 0});
          break;
        }
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      for (size_t i = 0; i < kMaxPads; ++i)
        if (impl_->pads[i] && impl_->padIds[i] == e.gdevice.which) {
          SDL_CloseGamepad(impl_->pads[i]);
          impl_->pads[i] = nullptr;
          if (onEvent) onEvent({WindowEventType::DeviceChanged, 0, 0});
        }
      break;
    default: break;
    }
  }
  int n = 0;
  const bool* ks = SDL_GetKeyboardState(&n);
  for (int i = 0; i < n && i < static_cast<int>(kMaxKeys); ++i) in.keys[i] = ks[i];
  const SDL_MouseButtonFlags mb = SDL_GetMouseState(nullptr, nullptr);
  for (int i = 0; i < 5; ++i) in.mouse[i] = (mb & SDL_BUTTON_MASK(i + 1)) != 0;
  for (size_t p = 0; p < kMaxPads; ++p) {
    PadRaw& r = in.pads[p];
    r = PadRaw{};
    if (!impl_->pads[p]) continue;
    r.connected = true;
    for (size_t b = 0; b < static_cast<size_t>(PadButton::Count); ++b) r.buttons[b] = SDL_GetGamepadButton(impl_->pads[p], kButtonMap[b]);
    for (size_t a = 0; a < 6; ++a) {
      const f32 v = static_cast<f32>(SDL_GetGamepadAxis(impl_->pads[p], kAxisMap[a])) / 32767.f;
      r.axes[a] = a >= 4 ? std::max(0.f, v) : std::max(-1.f, v);
    }
  }
  return impl_->open;
}

void SdlWindow::size(u32& w, u32& h) const {
  int iw = 0, ih = 0;
  if (impl_->window) SDL_GetWindowSize(impl_->window, &iw, &ih);
  w = static_cast<u32>(iw);
  h = static_cast<u32>(ih);
}

void* SdlWindow::nativeHandle() const { return impl_->window; }

} // namespace tool
