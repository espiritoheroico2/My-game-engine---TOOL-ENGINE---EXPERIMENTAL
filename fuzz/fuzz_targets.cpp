// Alvos libFuzzer (clang -fsanitize=fuzzer,address,undefined). Um alvo por -DFUZZ_TARGET=<nome>.
// Cobrem todo parser que recebe dados nao confiaveis: JSON, WAV, .tpak, pacotes de rede, grafo narrativo.
#include "tool/audio/audio.hpp"
#include "tool/core/json.hpp"
#include "tool/mod/mod.hpp"
#include "tool/narrative/narrative.hpp"
#include "tool/net/net.hpp"
#include "tool/pack/pack.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace tool;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
  Logger::get().setMinLevel(LogLevel::Error);
  static bool quiet = (Logger::get().setSink([](LogLevel, std::string_view) {}), true);
  (void)quiet;
  debug().reset();
#if defined(FUZZ_JSON)
  Json::parse(std::string_view(reinterpret_cast<const char*>(d), n));
#elif defined(FUZZ_WAV)
  loadWav(d, n);
#elif defined(FUZZ_PACK)
  if (auto r = pack::Reader::open(d, n)) for (auto& e : r->entries()) (void)r->read(e.path);
#elif defined(FUZZ_NET)
  static net::SimLink link; static net::Connection c(link.b);
  link.a.send(net::Bytes(d, d + n)); c.update();
#elif defined(FUZZ_NARRATIVE)
  if (auto j = Json::parse(std::string_view(reinterpret_cast<const char*>(d), n))) {
    narr::Program p; std::string err;
    if (narr::Compiler().compile(*j, p, err)) { narr::Runtime rt(p); rt.fire("e"); rt.update(1); rt.load(*j); }
  }
#elif defined(FUZZ_MOD)
  if (auto j = Json::parse(std::string_view(reinterpret_cast<const char*>(d), n))) { mod::ModTool t; mod::Manifest m; t.parseManifest(*j, mod::Trust::Community, m); }
#endif
  return 0;
}

#ifdef FUZZ_STANDALONE
// Driver proprio (sem libFuzzer): parte de sementes validas e aplica mutacoes aleatorias
// (bit-flip, byte aleatorio, truncar, duplicar trecho, inserir lixo). Uso: fuzz_x [iteracoes] [semente-rng]
#include <cstdlib>
#include <cstdio>
#include <vector>

static std::vector<std::vector<uint8_t>> seeds() {
  std::vector<std::vector<uint8_t>> v;
  auto str = [&](const char* t) { std::string_view s(t); v.emplace_back(s.begin(), s.end()); };
#if defined(FUZZ_JSON) || defined(FUZZ_MOD)
  str(R"({"id":"my-mod","version":"1.2.3","api":1,"permissions":["read_world","audio"],"a":[1,2.5,true,null,"x\u00e9"]})");
#elif defined(FUZZ_NARRATIVE)
  str(R"({"variables":{"hp":{"value":100}},"nodes":[{"id":"p","type":"onvar","var":"hp","op":"<","value":50,"hysteresis":5,"mode":"reactivable"},{"id":"b","type":"branch","cond":"c"},{"id":"c","type":"compare","var":"hp","op":">","value":1},{"id":"a","type":"action","name":"x","arg":"y"},{"id":"t","type":"trigger","event":"e"}],"links":[{"from":"p","to":"b"},{"from":"b","port":"then","to":"a"},{"from":"t","to":"a"}],"version":1,"vars":{"hp":3},"flows":[{"pc":0,"wait":0,"stack":[]}]})");
#elif defined(FUZZ_WAV)
  v.push_back({'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xBB,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x40,0,0xC0});
#elif defined(FUZZ_PACK)
  pack::Builder b; b.add("a/b.bin", std::vector<uint8_t>(40, 3)); b.add("c.txt", {1, 2, 3});
  std::vector<uint8_t> out; pack::Builder::Report r; b.build(out, r); v.push_back(out);
#elif defined(FUZZ_NET)
  net::Writer w; w.u16_(net::Connection::kMagic); w.u8_(1); w.u32_(0); w.u8_(1); w.u32_(0); w.u16_(3); w.bytes({1, 2, 3});
  w.u32_(net::crc32(w.b.data(), w.b.size())); v.push_back(w.b);
#endif
  return v;
}

int main(int argc, char** argv) {
  const long iters = argc > 1 ? std::atol(argv[1]) : 200000;
  uint64_t st = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
  auto rnd = [&] { st = st * 6364136223846793005ull + 1442695040888963407ull; return static_cast<uint32_t>(st >> 33); };
  auto corpus = seeds();
  for (auto& s : corpus) LLVMFuzzerTestOneInput(s.data(), s.size());
  for (long i = 0; i < iters; ++i) {
    std::vector<uint8_t> m = corpus[rnd() % corpus.size()];
    const int ops = 1 + static_cast<int>(rnd() % 6);
    for (int k = 0; k < ops && !m.empty(); ++k) {
      switch (rnd() % 6) {
      case 0: m[rnd() % m.size()] ^= static_cast<uint8_t>(1u << (rnd() % 8)); break;
      case 1: m[rnd() % m.size()] = static_cast<uint8_t>(rnd()); break;
      case 2: m.resize(rnd() % (m.size() + 1)); break;
      case 3: { size_t a = rnd() % m.size(), len = rnd() % 16; for (size_t j = 0; j < len; ++j) m.insert(m.begin() + static_cast<std::ptrdiff_t>(a), static_cast<uint8_t>(rnd())); break; }
      case 4: { size_t a = rnd() % m.size(), len = std::min<size_t>(rnd() % 32, m.size() - a); std::vector<uint8_t> chunk(m.begin() + static_cast<std::ptrdiff_t>(a), m.begin() + static_cast<std::ptrdiff_t>(a + len)); m.insert(m.begin() + static_cast<std::ptrdiff_t>(a), chunk.begin(), chunk.end()); break; }
      default: m[rnd() % m.size()] = "{}[]\",:0\\-"[rnd() % 11]; break;
      }
    }
    if (m.size() > 8192) m.resize(8192);
    LLVMFuzzerTestOneInput(m.data(), m.size());
    if (i % 50000 == 0) { corpus.push_back(m); if (corpus.size() > 64) corpus.erase(corpus.begin() + 1); }
  }
  std::printf("%ld execucoes sem falhas\n", iters);
  return 0;
}
#endif
