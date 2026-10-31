#include "test.hpp"
#include "tool/pack/pack.hpp"

using namespace tool;
using namespace tool::pack;

namespace { struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } }; }

TEST(sha256_known_vectors) {
  const std::string abc = "abc";
  CHECK(toHex(sha256(reinterpret_cast<const u8*>(abc.data()), abc.size())) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(toHex(sha256(nullptr, 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // 56 bytes: caso de dois blocos
  CHECK(toHex(sha256(reinterpret_cast<const u8*>(two.data()), two.size())) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(pack_roundtrip_and_determinism) {
  Quiet q; Builder b1, b2;
  b1.add("shaders/pbr.spv", {1, 2, 3}); b1.add("textures/a.ktx2", std::vector<u8>(1000, 7)); b1.add("level.json", {'{', '}'});
  b2.add("level.json", {'{', '}'}); b2.add("textures/a.ktx2", std::vector<u8>(1000, 7)); b2.add("shaders/pbr.spv", {1, 2, 3}); // outra ordem
  std::vector<u8> p1, p2; Builder::Report r;
  CHECK(b1.build(p1, r) && b2.build(p2, r));
  CHECK(p1 == p2);                      // build reproduzivel
  auto rd = Reader::open(p1.data(), p1.size());
  CHECK(rd && rd->entries().size() == 3);
  auto f = rd->read("textures/a.ktx2");
  CHECK(f && f->size() == 1000 && (*f)[999] == 7);
  CHECK(!rd->read("nao_existe"));
}

TEST(pack_budget_fails_and_reports_largest) {
  Quiet q; Builder b; b.add("big.bin", std::vector<u8>(5000, 1)); b.add("small.bin", {1});
  std::vector<u8> out; Builder::Report r;
  CHECK(!b.build(out, r, 1000));
  CHECK(r.overBudget && r.largest[0].first == "big.bin" && debug().hasCode(code::kBudget));
  CHECK(b.build(out, r, 10000));
}

TEST(pack_rejects_path_traversal_and_duplicates) {
  Quiet q; Builder b;
  CHECK(!b.add("../etc/passwd", {1}) && !b.add("/abs", {1}) && !b.add("a\\b", {1}) && !b.add("", {1}));
  CHECK(b.add("ok", {1}) && !b.add("ok", {2}));
}

TEST(pack_detects_tampering) {
  Quiet q; Builder b; b.add("a", std::vector<u8>(100, 5)); b.add("b", std::vector<u8>(100, 6));
  std::vector<u8> p; Builder::Report r; b.build(p, r);
  auto bad = p; bad[bad.size() - 1] ^= 1;           // adultera um byte de dados
  auto rd = Reader::open(bad.data(), bad.size());
  CHECK(rd.has_value());                            // tabela intacta
  CHECK(!rd->read("b") && debug().hasCode(code::kHashMismatch));
  CHECK(rd->read("a").has_value());
  auto bad2 = p; bad2[60] ^= 1;                     // adultera a tabela
  CHECK(!Reader::open(bad2.data(), bad2.size()).has_value());
}

TEST(pack_fuzz_truncations_and_bitflips_never_crash) {
  Quiet q; Builder b; for (int i = 0; i < 5; ++i) b.add("f" + std::to_string(i), std::vector<u8>(50 + static_cast<size_t>(i), static_cast<u8>(i)));
  std::vector<u8> p; Builder::Report r; b.build(p, r);
  for (size_t n = 0; n < p.size(); ++n) { auto rd = Reader::open(p.data(), n); if (rd) for (auto& e : rd->entries()) (void)rd->read(e.path); }
  u32 s = 99; auto rnd = [&] { s = s * 1664525u + 1013904223u; return s >> 8; };
  for (int i = 0; i < 4000; ++i) {
    auto m = p; m[rnd() % m.size()] ^= static_cast<u8>(1u << (rnd() % 8));
    auto rd = Reader::open(m.data(), m.size()); if (rd) for (auto& e : rd->entries()) (void)rd->read(e.path);
  }
  CHECK(true);
}
