// ToolEngine - janela e eventos de plataforma (interface). Implementacoes:
// NullWindow (testes/servidor), SdlWindow (desktop/Android/Web via SDL3; ver docs).
#pragma once
#include "tool/core/types.hpp"
#include "tool/input/input.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tool {

// Parametros da janela: titulo, tamanho inicial, redimensionamento e vsync.
struct WindowDesc {
  std::string title = "ToolEngine";
  u32 width = 1280, height = 720;
  bool resizable = true; // "janelas ajustaveis"
  bool vsync = true;
};

// Tipo de evento de janela entregue ao callback onEvent.
enum class WindowEventType : u8 { None, Close, Resize, FocusLost, FocusGained, DeviceChanged };
// Evento de janela. width e height so valem para Resize.
struct WindowEvent {
  WindowEventType type = WindowEventType::None;
  u32 width = 0, height = 0;
};

// Interface de janela: processa eventos, informa o tamanho e entrega o handle nativo ao RHI.
// Implementacoes: NullWindow (testes e servidor) e SdlWindow (SDL3).
class IWindow {
public:
  // Destrutor virtual.
  virtual ~IWindow() = default;
  // Processa eventos do sistema; devolve false quando a janela deve fechar.
  virtual bool pump(RawInput& input) = 0;
  // Tamanho atual da area de desenho, em pixels.
  virtual void size(u32& w, u32& h) const = 0;
  // Handle nativo da janela (HWND, ANativeWindow, canvas), usado pelo RHI. Nulo sem tela.
  virtual void* nativeHandle() const = 0; // HWND / ANativeWindow / canvas, para o RHI
  // Nome do backend de plataforma, para diagnostico.
  virtual const char* backendName() const = 0;
  std::function<void(const WindowEvent&)> onEvent;
};

// Janela sem tela: permite rodar tudo (jogo, testes, servidor dedicado) sem GPU nem display.
class NullWindow final : public IWindow {
public:
  // Cria uma janela virtual com o tamanho de d.
  explicit NullWindow(const WindowDesc& d) : w_(d.width), h_(d.height) {}
  // Entrega a entrada injetada pelos testes. Devolve false depois de requestClose().
  bool pump(RawInput& input) override {
    input = injected;
    injected.mouseDelta = {};
    injected.mouseWheel = {};
    return !closed_;
  }
  // Tamanho virtual, definido por resize().
  void size(u32& w, u32& h) const override { w = w_; h = h_; }
  // Sem handle nativo (offscreen).
  void* nativeHandle() const override { return nullptr; }
  // Backend de plataforma sem tela.
  const char* backendName() const override { return "null"; }

  // Controle pelos testes.
  RawInput injected;
  // Simula o fechamento da janela e dispara o evento Close.
  void requestClose() { closed_ = true; if (onEvent) onEvent({WindowEventType::Close, 0, 0}); }
  // Simula um redimensionamento e dispara o evento Resize.
  void resize(u32 w, u32 h) { w_ = w; h_ = h; if (onEvent) onEvent({WindowEventType::Resize, w, h}); }

private:
  u32 w_, h_;
  bool closed_ = false;
};

// Cria uma NullWindow como IWindow.
inline std::unique_ptr<IWindow> createNullWindow(const WindowDesc& d) { return std::make_unique<NullWindow>(d); }

} // namespace tool
