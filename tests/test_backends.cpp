// So compilado quando TOOL_WITH_SDL3 / TOOL_WITH_MINIAUDIO estao ligados (preset "full").
#include "test.hpp"

#ifdef TOOL_TEST_SDL3
#include "tool/platform/sdl_window.hpp"
#include <cstdlib>

TEST(sdl3_window_headless_creates_and_pumps) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  tool::SdlWindow w({"teste", 320, 200, true, false});
  CHECK(w.ok());
  tool::RawInput in;
  CHECK(w.pump(in)); // sem evento de fechar
  tool::u32 a = 0, b = 0;
  w.size(a, b);
  CHECK(a == 320 && b == 200);
  CHECK(std::string(w.backendName()) == "sdl3");
}
#endif

#ifdef TOOL_TEST_MINIAUDIO
#include "tool/audio/miniaudio_device.hpp"

TEST(miniaudio_device_start_stop_does_not_crash) {
  tool::Mixer m;
  tool::MiniaudioDevice dev;
  const bool ok = dev.start(m); // sem placa de som no CI: pode ser false, o importante e nao travar
  (void)ok;
  dev.stop();
  CHECK(std::string(dev.backendName()) == "miniaudio");
}
#endif
