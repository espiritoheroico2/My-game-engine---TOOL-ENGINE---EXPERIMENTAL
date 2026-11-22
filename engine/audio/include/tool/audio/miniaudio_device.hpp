#pragma once
#include "tool/audio/audio.hpp"

namespace tool {

// Saida de audio via miniaudio. Implementa IAudioDevice e chama Mixer::render() na thread de audio.
class MiniaudioDevice final : public IAudioDevice {
public:
  // Cria o estado interno. O dispositivo so e aberto em start().
  MiniaudioDevice();
  // Para o dispositivo e libera o estado interno.
  ~MiniaudioDevice() override;
  // Nao copiavel.
  MiniaudioDevice(const MiniaudioDevice&) = delete;
  // Nao atribuivel.
  MiniaudioDevice& operator=(const MiniaudioDevice&) = delete;
  // Abre a saida (f32, estereo, 48 kHz) e inicia o callback. Devolve false se falhar. Chamar de novo nao faz nada.
  bool start(Mixer& mixer) override;
  // Para e fecha o dispositivo. Seguro chamar mais de uma vez.
  void stop() override;
  // Nome do backend de audio (miniaudio).
  const char* backendName() const override { return "miniaudio"; }

private:
  struct Impl;
  Impl* impl_;
};

} // namespace tool
