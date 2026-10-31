// Janela SDL3 (so compilada com -DTOOL_WITH_SDL3=ON).
#pragma once
#include "tool/platform/window.hpp"

namespace tool {

class SdlWindow final : public IWindow {
public:
  explicit SdlWindow(const WindowDesc& d);
  ~SdlWindow() override;
  SdlWindow(const SdlWindow&) = delete;
  SdlWindow& operator=(const SdlWindow&) = delete;

  bool ok() const { return ok_; }
  bool pump(RawInput& input) override;
  void size(u32& w, u32& h) const override;
  void* nativeHandle() const override;
  const char* backendName() const override { return "sdl3"; }

private:
  struct Impl;
  Impl* impl_;
  bool ok_ = false;
};

} // namespace tool
