// Saida de audio real via miniaudio (so compilada com -DTOOL_WITH_MINIAUDIO=ON).
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "tool/audio/decode.hpp"
#include "tool/audio/miniaudio_device.hpp"

#include <filesystem>

namespace tool {

struct MiniaudioDevice::Impl {
  ma_device dev{};
  bool started = false;
};

static void dataCallback(ma_device* d, void* out, const void*, ma_uint32 frames) {
  auto* mixer = static_cast<Mixer*>(d->pUserData);
  mixer->render(static_cast<f32*>(out), frames);
}

MiniaudioDevice::MiniaudioDevice() : impl_(new Impl) {}
MiniaudioDevice::~MiniaudioDevice() { stop(); delete impl_; }

bool MiniaudioDevice::start(Mixer& mixer) {
  if (impl_->started) return true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 48000;
  cfg.dataCallback = dataCallback;
  cfg.pUserData = &mixer;
  if (ma_device_init(nullptr, &cfg, &impl_->dev) != MA_SUCCESS) return false;
  if (ma_device_start(&impl_->dev) != MA_SUCCESS) { ma_device_uninit(&impl_->dev); return false; }
  impl_->started = true;
  return true;
}

void MiniaudioDevice::stop() {
  if (!impl_->started) return;
  ma_device_uninit(&impl_->dev);
  impl_->started = false;
}


SoundPtr decodeAudioFile(const std::string& path, std::string* err) {
  auto fail = [&](const std::string& m) -> SoundPtr { if (err) *err = m; return nullptr; };
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return fail("file not found: " + path);
  if (size > (256u << 20)) return fail("audio file too large (limit 256 MB)");
  // ma_decoder le WAV/MP3 e converte para float; mantemos a taxa original e no maximo 2 canais.
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
  ma_decoder dec;
#if defined(_WIN32)
  const std::wstring wpath = std::filesystem::path(path).wstring();
  if (ma_decoder_init_file_w(wpath.c_str(), &cfg, &dec) != MA_SUCCESS) return fail("unsupported or corrupt audio file (WAV/MP3 only)");
#else
  if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return fail("unsupported or corrupt audio file (WAV/MP3 only)");
#endif
  auto out = std::make_shared<SoundData>();
  out->sampleRate = dec.outputSampleRate;
  out->channels = dec.outputChannels > 1 ? 2u : 1u;
  const ma_uint32 ch = dec.outputChannels;
  const size_t maxFrames = static_cast<size_t>(dec.outputSampleRate) * 600; // 10 minutos
  std::vector<f32> buf(4096 * ch);
  bool ok = true;
  for (;;) {
    ma_uint64 got = 0;
    const ma_result r = ma_decoder_read_pcm_frames(&dec, buf.data(), 4096, &got);
    if (got == 0) break;
    if (out->samples.size() / out->channels + got > maxFrames) { ok = false; break; }
    for (ma_uint64 i = 0; i < got; ++i) {
      if (ch <= 2) { for (ma_uint32 c = 0; c < ch; ++c) out->samples.push_back(buf[i * ch + c]); }
      else { out->samples.push_back(buf[i * ch]); out->samples.push_back(buf[i * ch + 1]); } // mais de 2 canais: so frente E/D
    }
    if (r != MA_SUCCESS) break; // fim do arquivo
  }
  ma_decoder_uninit(&dec);
  if (!ok) return fail("audio too long (limit 10 minutes)");
  if (out->samples.empty()) return fail("audio file has no samples");
  return out;
}

} // namespace tool
