// Testes de importacao com ARQUIVOS REAIS (assets/samples, gerados por tools/gen_samples.py):
// glb, gltf com base64, fbx binario comprimido, arquivos corrompidos, limites de seguranca e decodificacao de audio.
#include "test.hpp"
#include "tool/assets/import.hpp"
#include "tool/audio/audio.hpp"
#ifdef TOOL_TEST_MINIAUDIO
#include "tool/audio/decode.hpp"
#endif

#include <cmath>

using namespace tool;
using namespace tool::assets;

namespace {
struct Quiet2 { Quiet2() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet2() { Logger::get().setSink(nullptr); } };
std::string sample(const char* f) { return std::string(TOOL_SAMPLES_DIR) + "/" + f; }

// Toda face deve ter a normal geometrica (sentido anti-horario) do mesmo lado da normal do vertice.
bool windingConsistent(const Mesh& m) {
  for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
    const Vec3 a = m.pos[m.idx[t]], b = m.pos[m.idx[t + 1]], c = m.pos[m.idx[t + 2]];
    if (dot(cross(b - a, c - a), m.normal[m.idx[t]]) <= 0.f) return false;
  }
  return true;
}
bool unitNormals(const Mesh& m) { for (auto& n : m.normal) if (std::fabs(length(n) - 1.f) > 1e-3f) return false; return true; }
bool near(f32 a, f32 b, f32 e = 1e-3f) { return std::fabs(a - b) <= e; }
} // namespace

TEST(import_glb_cube_bakes_node_transform) {
  Quiet2 q;
  ImportResult r = importModelFile(sample("cubo.glb"));
  CHECK(r.ok);
  CHECK(r.model.parts.size() == 1);
  CHECK(r.model.vertexCount() == 24 && r.model.triangleCount() == 12);
  Vec3 mn, mx; CHECK(r.model.bounds(mn, mx));
  CHECK(near(mn.x, -0.5f) && near(mx.x, 0.5f)); // translacao (0,0.5,0) + escala Y 1.5 assadas nos vertices
  CHECK(near(mn.y, -0.25f) && near(mx.y, 1.25f));
  CHECK(windingConsistent(r.model.parts[0].mesh) && unitNormals(r.model.parts[0].mesh));
  CHECK(r.model.parts[0].material.albedo.z > r.model.parts[0].material.albedo.x); // cor azulada do material
}

TEST(import_gltf_pyramid_base64_buffer) {
  Quiet2 q;
  ImportResult r = importModelFile(sample("piramide.gltf"));
  CHECK(r.ok);
  CHECK(r.model.triangleCount() == 6 && r.model.vertexCount() == 18);
  CHECK(windingConsistent(r.model.parts[0].mesh));
}

TEST(import_fbx_box_units_and_translation) {
  Quiet2 q;
  ImportResult r = importModelFile(sample("caixa.fbx"));
  CHECK(r.ok);
  CHECK(r.model.triangleCount() == 12); // 6 quadrilateros -> 12 triangulos
  Vec3 mn, mx; CHECK(r.model.bounds(mn, mx));
  CHECK(near(mn.x, 1.5f, 1e-2f) && near(mx.x, 2.5f, 1e-2f)); // 200 cm de translacao e caixa de 100 cm -> metros
  CHECK(near(mx.y - mn.y, 1.f, 1e-2f));
  CHECK(windingConsistent(r.model.parts[0].mesh));
  ImportOptions raw; raw.applyUnitScale = false;
  ImportResult r2 = importModelFile(sample("caixa.fbx"), raw);
  Vec3 a, b; CHECK(r2.ok && r2.model.bounds(a, b) && near(b.x - a.x, 100.f, 0.1f)); // sem escala: numeros crus (cm)
}

TEST(import_rejects_corrupt_and_oversized) {
  Quiet2 q;
  for (const char* f : {"cubo.glb", "piramide.gltf", "caixa.fbx"}) {
    std::vector<u8> bytes; std::string err;
    CHECK(readFileBytes(sample(f), 1u << 20, bytes, err));
    // truncar em varios pontos e corromper bytes: nunca pode travar/crashar (resultado ok ou erro, tanto faz)
    for (size_t cut : {size_t(0), size_t(10), bytes.size() / 3, bytes.size() / 2, bytes.size() - 1}) {
      std::vector<u8> t(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)); (void)importModelMemory(t, ".");
    }
    for (size_t i = 20; i < bytes.size(); i += 7) { std::vector<u8> c = bytes; c[i] ^= 0xFF; (void)importModelMemory(c, "."); }
    ImportOptions tiny; tiny.limits.maxVertices = 5; // limite de seguranca
    CHECK(!importModelMemory(bytes, ".", tiny).ok);
  }
}

#ifdef TOOL_TEST_MINIAUDIO
TEST(audio_decode_wav_and_mp3) {
  Quiet2 q;
  std::string err;
  SoundPtr w = decodeAudioFile(sample("bip.wav"), &err);
  CHECK(w && w->frames() > 8000 && w->sampleRate == 22050 && w->channels == 1);
  SoundPtr m = decodeAudioFile(sample("tom.mp3"), &err);
  CHECK(m && m->frames() > 15000 && m->channels == 2);
  float peak = 0; for (float s : m->samples) peak = std::max(peak, std::fabs(s));
  CHECK(peak > 0.05f && peak <= 1.01f);
  CHECK(!decodeAudioFile(sample("nao_existe.mp3"), &err) && !err.empty());
  CHECK(!decodeAudioFile(sample("cubo.glb"), &err)); // nao e audio
  Mixer mixer; PlayParams p; mixer.play(m, p);
  std::vector<float> out(2 * 512); mixer.render(out.data(), 512);
  float mx = 0; for (float s : out) mx = std::max(mx, std::fabs(s)); CHECK(mx > 0.01f); // o mixer toca o som decodificado
}
#endif
