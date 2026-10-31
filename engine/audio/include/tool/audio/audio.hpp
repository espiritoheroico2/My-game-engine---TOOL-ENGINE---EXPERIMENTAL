// ToolEngine - audio: mixer em software (barramentos, vozes, 3D simples, WAV).
// A saida real (miniaudio) so implementa IAudioDevice e chama Mixer::render().
// Assim toda a logica e testavel sem placa de som.
#pragma once
#include "tool/core/handle.hpp"
#include "tool/core/types.hpp"
#include "tool/debug/debug.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tool {

struct SoundData {
  u32 sampleRate = 48000;
  u32 channels = 1;         // 1 ou 2
  std::vector<f32> samples; // intercalado
  size_t frames() const { return channels ? samples.size() / channels : 0; }
};
using SoundPtr = std::shared_ptr<const SoundData>;

// Decodifica WAV PCM 16-bit / float32 (mono/estereo) da memoria. Retorna nullptr se invalido.
inline SoundPtr loadWav(const u8* d, size_t n) {
  auto rd16 = [&](size_t o) { return static_cast<u32>(d[o] | (d[o + 1] << 8)); };
  auto rd32 = [&](size_t o) { return static_cast<u32>(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<u32>(d[o + 3]) << 24)); };
  if (n < 44 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return nullptr;
  u32 fmtTag = 0, ch = 0, rate = 0, bits = 0;
  size_t pos = 12;
  auto out = std::make_shared<SoundData>();
  bool haveFmt = false;
  while (pos + 8 <= n) {
    const u32 sz = rd32(pos + 4);
    const size_t body = pos + 8;
    if (body + sz > n) return nullptr; // truncado: rejeita (fail-closed)
    if (std::memcmp(d + pos, "fmt ", 4) == 0 && sz >= 16) {
      fmtTag = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
      haveFmt = true;
    } else if (std::memcmp(d + pos, "data", 4) == 0 && haveFmt) {
      if ((ch != 1 && ch != 2) || rate == 0) return nullptr;
      out->channels = ch; out->sampleRate = rate;
      if (fmtTag == 1 && bits == 16) {
        const size_t cnt = sz / 2;
        out->samples.resize(cnt);
        for (size_t i = 0; i < cnt; ++i) out->samples[i] = static_cast<i32>(static_cast<int16_t>(rd16(body + i * 2))) / 32768.f;
      } else if (fmtTag == 3 && bits == 32) {
        const size_t cnt = sz / 4;
        out->samples.resize(cnt);
        for (size_t i = 0; i < cnt; ++i) { u32 v = rd32(body + i * 4); std::memcpy(&out->samples[i], &v, 4); }
      } else return nullptr;
      return out;
    }
    pos = body + sz + (sz & 1);
  }
  return nullptr;
}

enum class BusId : u8 { Master, Music, Sfx, Voice, Count };

struct Listener { f32 x = 0, y = 0, z = 0; f32 rightX = 1, rightZ = 0; }; // direita do ouvinte no plano XZ

struct PlayParams {
  BusId bus = BusId::Sfx;
  f32 volume = 1.f, pitch = 1.f, pan = 0.f; // pan -1..1
  bool loop = false;
  bool spatial = false;
  f32 x = 0, y = 0, z = 0;
  f32 minDistance = 1.f, maxDistance = 50.f;
  int priority = 0; // maior = mais importante (roubo de voz)
};

using VoiceId = Handle;

class Mixer {
public:
  explicit Mixer(u32 sampleRate = 48000, size_t maxVoices = 64) : rate_(sampleRate), max_(maxVoices) {}

  void setBusVolume(BusId b, f32 v) { std::lock_guard lk(mu_); bus_[static_cast<int>(b)].volume = std::clamp(v, 0.f, 2.f); }
  void setBusMuted(BusId b, bool m) { std::lock_guard lk(mu_); bus_[static_cast<int>(b)].muted = m; }
  void setListener(const Listener& l) { std::lock_guard lk(mu_); listener_ = l; }

  VoiceId play(SoundPtr s, const PlayParams& p = {}) {
    std::lock_guard lk(mu_);
    if (!s || s->frames() == 0) {
      debug().report("TOOL-W110", Severity::Warning, "play() com som vazio/nulo");
      return kNullHandle;
    }
    if (voices_.size() >= max_) { // rouba a voz menos importante (e mais antiga)
      size_t victim = 0;
      for (size_t i = 1; i < voices_.size(); ++i)
        if (voices_[i].p.priority < voices_[victim].p.priority) victim = i;
      if (voices_[victim].p.priority > p.priority) return kNullHandle; // todas mais importantes
      ids_.destroy(voices_[victim].id);
      voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(victim));
      ++stolen_;
    }
    Voice v;
    v.id = ids_.create();
    v.s = std::move(s);
    v.p = p;
    voices_.push_back(std::move(v));
    return voices_.back().id;
  }

  void stop(VoiceId id) {
    std::lock_guard lk(mu_);
    for (size_t i = 0; i < voices_.size(); ++i)
      if (voices_[i].id == id) { ids_.destroy(id); voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i)); return; }
  }
  bool playing(VoiceId id) const { std::lock_guard lk(mu_); return ids_.alive(id); }
  size_t activeVoices() const { std::lock_guard lk(mu_); return voices_.size(); }
  size_t stolenVoices() const { return stolen_; }

  // Gera `frames` quadros estereo intercalados em `out`.
  void render(f32* out, size_t frames) {
    std::lock_guard lk(mu_);
    std::fill(out, out + frames * 2, 0.f);
    const f32 master = bus_[0].muted ? 0.f : bus_[0].volume;
    for (size_t vi = 0; vi < voices_.size();) {
      Voice& v = voices_[vi];
      const BusState& b = bus_[static_cast<int>(v.p.bus)];
      const f32 busGain = b.muted ? 0.f : b.volume;
      f32 gl = 1.f, gr = 1.f;
      f32 gain = v.p.volume * busGain * master;
      f32 pan = v.p.pan;
      if (v.p.spatial) {
        const f32 dx = v.p.x - listener_.x, dy = v.p.y - listener_.y, dz = v.p.z - listener_.z;
        const f32 dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const f32 d = std::clamp(dist, v.p.minDistance, v.p.maxDistance);
        gain *= v.p.minDistance / d * (dist >= v.p.maxDistance ? 0.f : 1.f);
        if (dist > 1e-4f) pan = std::clamp((dx * listener_.rightX + dz * listener_.rightZ) / dist, -1.f, 1.f);
      }
      // pan de potencia constante
      const f32 ang = (std::clamp(pan, -1.f, 1.f) + 1.f) * 0.25f * 3.14159265f;
      gl = std::cos(ang); gr = std::sin(ang);
      const f64 step = static_cast<f64>(v.s->sampleRate) / rate_ * v.p.pitch;
      const size_t total = v.s->frames();
      bool finished = false;
      for (size_t i = 0; i < frames; ++i) {
        size_t i0 = static_cast<size_t>(v.pos);
        if (i0 >= total) {
          if (v.p.loop) { v.pos = std::fmod(v.pos, static_cast<f64>(total)); i0 = static_cast<size_t>(v.pos); }
          else { finished = true; break; }
        }
        const size_t i1 = (i0 + 1 < total) ? i0 + 1 : (v.p.loop ? 0 : i0);
        const f32 fr = static_cast<f32>(v.pos - static_cast<f64>(i0));
        f32 l, r;
        if (v.s->channels == 1) {
          l = r = v.s->samples[i0] * (1 - fr) + v.s->samples[i1] * fr;
        } else {
          l = v.s->samples[i0 * 2] * (1 - fr) + v.s->samples[i1 * 2] * fr;
          r = v.s->samples[i0 * 2 + 1] * (1 - fr) + v.s->samples[i1 * 2 + 1] * fr;
        }
        out[i * 2] += l * gain * gl;
        out[i * 2 + 1] += r * gain * gr;
        v.pos += step;
      }
      if (finished) { ids_.destroy(v.id); voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(vi)); }
      else ++vi;
    }
    for (size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.f, 1.f); // limitador simples
  }

private:
  struct Voice { VoiceId id; SoundPtr s; PlayParams p; f64 pos = 0; };
  struct BusState { f32 volume = 1.f; bool muted = false; };

  mutable std::mutex mu_; // protege vozes entre a thread do jogo e a de audio
  u32 rate_;
  size_t max_, stolen_ = 0;
  BusState bus_[static_cast<int>(BusId::Count)];
  Listener listener_;
  HandleAllocator ids_;
  std::vector<Voice> voices_;
};

// Dispositivo de saida: o backend (miniaudio) chama o callback na thread de audio.
class IAudioDevice {
public:
  virtual ~IAudioDevice() = default;
  virtual bool start(Mixer& mixer) = 0;
  virtual void stop() = 0;
  virtual const char* backendName() const = 0;
};

// Dispositivo nulo: o teste/servidor puxa os quadros manualmente.
class NullAudioDevice final : public IAudioDevice {
public:
  bool start(Mixer& m) override { mixer_ = &m; return true; }
  void stop() override { mixer_ = nullptr; }
  const char* backendName() const override { return "null"; }
  void pull(f32* out, size_t frames) { if (mixer_) mixer_->render(out, frames); }
private:
  Mixer* mixer_ = nullptr;
};

} // namespace tool
