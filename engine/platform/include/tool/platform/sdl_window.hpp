// Janela SDL3 (so compilada com -DTOOL_WITH_SDL3=ON).
#pragma once
#include "tool/platform/window.hpp"

namespace tool {

// Janela SDL3 com suporte a gamepads. O estado interno do SDL fica em Impl (pimpl).
class SdlWindow final : public IWindow {
public:
  // Cria a janela e inicializa SDL (video e gamepad). Use ok() para verificar.
  explicit SdlWindow(const WindowDesc& d);
  // Fecha os gamepads, destroi a janela e chama SDL_Quit().
  ~SdlWindow() override;
  // Nao copiavel: a janela e um recurso unico.
  SdlWindow(const SdlWindow&) = delete;
  // Nao atribuivel, pelo mesmo motivo.
  SdlWindow& operator=(const SdlWindow&) = delete;

  // Verdadeiro se o SDL e a janela foram criados com sucesso.
  bool ok() const { return ok_; }
  // Processa os eventos SDL e preenche input com teclado, mouse e gamepads. Devolve false quando deve fechar.
  bool pump(RawInput& input) override;
  // Tamanho da janela em pixels.
  void size(u32& w, u32& h) const override;
  // Devolve o SDL_Window*. Atenção: IWindow promete HWND ou canvas; nenhum chamador do repo usa isto ainda.
  void* nativeHandle() const override;
  // Nome do backend de plataforma (sdl3).
  const char* backendName() const override { return "sdl3"; }

private:
  struct Impl;
  Impl* impl_;
  bool ok_ = false;
};

} // namespace tool
