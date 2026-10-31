// ToolEngine - janela e eventos de plataforma (interface). Implementacoes:
// NullWindow (testes/servidor), SdlWindow (desktop/Android/Web via SDL3; ver docs).
#pragma once
#include "tool/core/types.hpp"
#include "tool/input/input.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tool {

struct WindowDesc {
  std::string title = "ToolEngine";
  u32 width = 1280, height = 720;
  bool resizable = true; // "janelas ajustaveis"
  bool vsync = true;
};

enum class WindowEventType : u8 { None, Close, Resize, FocusLost, FocusGained, DeviceChanged };
struct WindowEvent {
  WindowEventType type = WindowEventType::None;
  u32 width = 0, height = 0;
};

class IWindow {
public:
  virtual ~IWindow() = default;
  // Processa eventos do sistema; devolve false quando a janela deve fechar.
  virtual bool pump(RawInput& input) = 0;
  virtual void size(u32& w, u32& h) const = 0;
  virtual void* nativeHandle() const = 0; // HWND / ANativeWindow / canvas, para o RHI
  virtual const char* backendName() const = 0;
  std::function<void(const WindowEvent&)> onEvent;
};

// Janela sem tela: permite rodar tudo (jogo, testes, servidor dedicado) sem GPU nem display.
class NullWindow final : public IWindow {
public:
  explicit NullWindow(const WindowDesc& d) : w_(d.width), h_(d.height) {}
  bool pump(RawInput& input) override {
    input = injected;
    injected.mouseDelta = {};
    injected.mouseWheel = {};
    return !closed_;
  }
  void size(u32& w, u32& h) const override { w = w_; h = h_; }
  void* nativeHandle() const override { return nullptr; }
  const char* backendName() const override { return "null"; }

  // Controle pelos testes.
  RawInput injected;
  void requestClose() { closed_ = true; if (onEvent) onEvent({WindowEventType::Close, 0, 0}); }
  void resize(u32 w, u32 h) { w_ = w; h_ = h; if (onEvent) onEvent({WindowEventType::Resize, w, h}); }

private:
  u32 w_, h_;
  bool closed_ = false;
};

inline std::unique_ptr<IWindow> createNullWindow(const WindowDesc& d) { return std::make_unique<NullWindow>(d); }

} // namespace tool
