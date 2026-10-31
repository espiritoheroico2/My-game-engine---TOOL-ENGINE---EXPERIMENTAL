#pragma once
#include "tool/audio/audio.hpp"

namespace tool {

class MiniaudioDevice final : public IAudioDevice {
public:
  MiniaudioDevice();
  ~MiniaudioDevice() override;
  MiniaudioDevice(const MiniaudioDevice&) = delete;
  MiniaudioDevice& operator=(const MiniaudioDevice&) = delete;
  bool start(Mixer& mixer) override;
  void stop() override;
  const char* backendName() const override { return "miniaudio"; }

private:
  struct Impl;
  Impl* impl_;
};

} // namespace tool
