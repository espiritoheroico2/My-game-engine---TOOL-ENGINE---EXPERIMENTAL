// Verificacao inicial do modulo de importacao de modelos (a bateria completa vem com os importadores).
#include "test.hpp"
#include "tool/assets/import.hpp"

using namespace tool;
using namespace tool::assets;

namespace { struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } }; }

TEST(assets_inflate_stored_and_garbage) {
  // zlib com um bloco "stored" contendo "abc": 78 01 | 01 03 00 fc ff 'a' 'b' 'c' | adler32 = 024d0127
  const std::vector<u8> z = {0x78, 0x01, 0x01, 0x03, 0x00, 0xfc, 0xff, 'a', 'b', 'c', 0x02, 0x4d, 0x01, 0x27};
  std::vector<u8> out;
  CHECK(zlibDecompress(z.data(), z.size(), out, 100) && out == std::vector<u8>({'a', 'b', 'c'}));
  CHECK(!zlibDecompress(z.data(), z.size(), out, 2)); // acima do limite de saida
  std::vector<u8> bad = z; bad[12] ^= 1;               // Adler-32 errado
  CHECK(!zlibDecompress(bad.data(), bad.size(), out, 100));
}

TEST(assets_rejects_unknown_formats) {
  Quiet q;
  const std::vector<u8> junk = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(!importModelMemory(junk, ".").ok);
  CHECK(!importModelMemory({}, ".").ok);
  const std::string ascii = "; FBX 7.4.0 project file";
  CHECK(!importModelMemory(std::vector<u8>(ascii.begin(), ascii.end()), ".").ok);
}
