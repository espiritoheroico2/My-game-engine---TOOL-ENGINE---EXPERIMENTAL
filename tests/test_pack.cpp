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

// Lista compartilhada de caminhos que NUNCA podem ser aceitos, pelo Builder nem pelo Reader.
static const std::vector<std::string> kUnsafePaths = {
    "../x", "a/../b", "..", "/root", "\\root", "C:/x", "C:arq", "a\\b", "a//b", "a/./b", "./x", "a/", "",
    std::string("a\0b", 3), std::string("a\x01" "b", 3), std::string(kMaxPath + 1, 'a'),
};

TEST(pack_safe_path_rules) {
  for (const auto& p : kUnsafePaths) CHECK(!isSafePackPath(p));
  CHECK(isSafePackPath("a/b.txt"));
  CHECK(isSafePackPath("x"));
  CHECK(isSafePackPath("dir/sub/file.bin"));
  CHECK(isSafePackPath("a..b"));                        // nome legitimo com dois pontos seguidos
  CHECK(isSafePackPath(std::string(kMaxPath, 'a')));    // limite exato e aceito
}

TEST(pack_builder_rejects_every_unsafe_path) {
  Quiet q; Builder b;
  for (const auto& p : kUnsafePaths) CHECK(!b.add(p, {1}));
  CHECK(b.add("ok/ok.bin", {1}));
}

// Monta um .tpak bruto com hash de tabela VALIDO. Simula pacote de origem nao confiavel:
// o Builder nunca produziria estes caminhos, mas o Reader precisa recusa-los mesmo assim.
namespace {
void put32(std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(x >> (8 * i))); }
void put64(std::vector<u8>& v, u64 x) { put32(v, static_cast<u32>(x)); put32(v, static_cast<u32>(x >> 32)); }
std::vector<u8> rawPack(const std::vector<std::pair<std::string, std::vector<u8>>>& files) {
  std::vector<u8> table, data;
  u64 offset = 0;
  for (const auto& [path, bytes] : files) {
    put32(table, static_cast<u32>(path.size()));
    table.insert(table.end(), path.begin(), path.end());
    put64(table, offset); put64(table, bytes.size());
    const auto h = sha256(bytes.data(), bytes.size());
    table.insert(table.end(), h.begin(), h.end());
    data.insert(data.end(), bytes.begin(), bytes.end());
    offset += bytes.size();
  }
  std::vector<u8> out;
  put32(out, kMagic); put32(out, kVersion); put32(out, static_cast<u32>(files.size())); put64(out, table.size());
  const auto th = sha256(table.data(), table.size());
  out.insert(out.end(), th.begin(), th.end());
  out.insert(out.end(), table.begin(), table.end());
  out.insert(out.end(), data.begin(), data.end());
  return out;
}
} // namespace

TEST(pack_reader_rejects_unsafe_paths_in_table) {
  Quiet q;
  // Controle: pacote bem formado com caminho seguro e aceito.
  const auto good = rawPack({{"ok/file.bin", {1, 2, 3}}});
  CHECK(Reader::open(good.data(), good.size()).has_value());
  // Cada caminho inseguro, sozinho, em um pacote com hash de tabela valido, deve ser recusado.
  for (const auto& p : kUnsafePaths) {
    const auto bad = rawPack({{p, {9}}});
    CHECK(!Reader::open(bad.data(), bad.size()).has_value());
  }
  CHECK(debug().hasCode(code::kBadPack));
  // Caminho duplicado na tabela continua recusado.
  const auto dup = rawPack({{"x", {1}}, {"x", {2}}});
  CHECK(!Reader::open(dup.data(), dup.size()).has_value());
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
