#include "test.hpp"
#include "tool/audio/audio.hpp"

using namespace tool;

namespace {
SoundPtr tone(size_t frames, f32 v, u32 ch = 1) {
  auto s = std::make_shared<SoundData>();
  s->channels = ch; s->sampleRate = 48000;
  s->samples.assign(frames * ch, v);
  return s;
}
f32 peak(const std::vector<f32>& b) { f32 m = 0; for (f32 x : b) m = std::max(m, std::abs(x)); return m; }
}

TEST(audio_play_and_finish) {
  Mixer m; std::vector<f32> out(128 * 2);
  VoiceId v = m.play(tone(100, 0.5f));
  CHECK(m.playing(v) && m.activeVoices() == 1);
  m.render(out.data(), 128);
  CHECK(peak(out) > 0.1f);
  CHECK(!m.playing(v) && m.activeVoices() == 0); // acabou
}

TEST(audio_loop_keeps_playing) {
  Mixer m; std::vector<f32> out(512 * 2);
  VoiceId v = m.play(tone(100, 0.5f), {.loop = true});
  m.render(out.data(), 512);
  CHECK(m.playing(v));
  CHECK(out[1000] != 0.f);
}

TEST(audio_bus_volume_and_mute) {
  Mixer m; std::vector<f32> out(64 * 2);
  m.setBusVolume(BusId::Sfx, 0.f);
  m.play(tone(200, 1.f), {.bus = BusId::Sfx});
  m.render(out.data(), 64);
  CHECK(peak(out) == 0.f);
  m.setBusVolume(BusId::Sfx, 1.f);
  m.setBusMuted(BusId::Master, true);
  m.render(out.data(), 64);
  CHECK(peak(out) == 0.f);
}

TEST(audio_pan_left_right) {
  Mixer m; std::vector<f32> out(32 * 2);
  m.play(tone(100, 1.f), {.pan = -1.f});
  m.render(out.data(), 32);
  CHECK(out[0] > 0.9f && std::abs(out[1]) < 0.01f);
}

TEST(audio_spatial_attenuation_and_side) {
  Mixer m; std::vector<f32> out(16 * 2);
  m.setListener({0, 0, 0, 1, 0});
  m.play(tone(100, 1.f), {.spatial = true, .x = 10.f, .z = 0.f, .minDistance = 1.f, .maxDistance = 50.f});
  m.render(out.data(), 16);
  CHECK(out[1] > out[0]);                 // fonte a direita
  CHECK(out[1] < 0.2f && out[1] > 0.f);   // atenuada por ~1/10
  Mixer far; std::vector<f32> o2(16 * 2);
  far.play(tone(100, 1.f), {.spatial = true, .x = 60.f});
  far.render(o2.data(), 16);
  CHECK(peak(o2) == 0.f);                 // alem do alcance maximo
}

TEST(audio_voice_stealing_by_priority) {
  Mixer m(48000, 2);
  VoiceId a = m.play(tone(1000, 1.f), {.priority = 1});
  m.play(tone(1000, 1.f), {.priority = 5});
  VoiceId c = m.play(tone(1000, 1.f), {.priority = 3}); // rouba a prioridade 1
  CHECK(!m.playing(a) && m.playing(c) && m.stolenVoices() == 1);
  VoiceId d = m.play(tone(1000, 1.f), {.priority = 0}); // todas mais importantes
  CHECK(d.isNull());
}

TEST(audio_pitch_changes_duration) {
  Mixer m; std::vector<f32> out(64 * 2);
  VoiceId v = m.play(tone(100, 1.f), {.pitch = 2.f});
  m.render(out.data(), 40); // 40 quadros * 2 = 80 amostras > 100? ainda toca
  CHECK(m.playing(v));
  m.render(out.data(), 40);
  CHECK(!m.playing(v));
}

TEST(audio_wav_loader_valid_and_invalid) {
  std::vector<u8> w = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xBB,0,0,0,0x77,1,0,2,0,16,0,
                       'd','a','t','a',4,0,0,0,0,0x40,0,0xC0};
  auto s = loadWav(w.data(), w.size());
  CHECK(s && s->channels == 1 && s->sampleRate == 48000 && s->frames() == 2);
  CHECK(s && std::abs(s->samples[0] - 0.5f) < 1e-3f && std::abs(s->samples[1] + 0.5f) < 1e-3f);
  CHECK(loadWav(w.data(), 20) == nullptr);
  w[40] = 200; // chunk maior que o arquivo
  CHECK(loadWav(w.data(), w.size()) == nullptr);
}

TEST(audio_null_device_pulls) {
  Mixer m; NullAudioDevice dev; dev.start(m);
  m.play(tone(50, 0.5f));
  std::vector<f32> out(32 * 2);
  dev.pull(out.data(), 32);
  CHECK(peak(out) > 0.1f);
}
