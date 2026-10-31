#include "test.hpp"
#include "tool/net/net.hpp"

using namespace tool;
using namespace tool::net;

namespace { struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } }; }

namespace {
Bytes msg(u32 i) { return {static_cast<u8>(i), static_cast<u8>(i >> 8), 0xAB}; }
// Roda ate tudo ser confirmado (ou estourar o limite de ticks).
void pump(Connection& a, Connection& b, int ticks = 2000) { for (int i = 0; i < ticks && (a.unacked() || b.unacked()); ++i) { a.update(); b.update(); } }
}

TEST(net_reliable_ordered_over_clean_link) {
  SimLink link; Connection a(link.a), b(link.b);
  std::vector<u32> got; b.onMessage = [&](const Bytes& m) { got.push_back(m[0] | (m[1] << 8)); };
  for (u32 i = 0; i < 50; ++i) a.send(msg(i));
  pump(a, b);
  CHECK(got.size() == 50);
  for (u32 i = 0; i < got.size(); ++i) CHECK(got[i] == i);
}

TEST(net_survives_loss_duplication_reordering) {
  Quiet q; SimLink link({0.3, 0.2, 0.3, 7}); Connection a(link.a), b(link.b);
  std::vector<u32> got; b.onMessage = [&](const Bytes& m) { got.push_back(m[0] | (m[1] << 8)); };
  for (u32 i = 0; i < 200; ++i) a.send(msg(i));
  pump(a, b, 20000);
  CHECK(got.size() == 200);
  bool inOrder = true; for (u32 i = 0; i < got.size(); ++i) inOrder &= got[i] == i;
  CHECK(inOrder);                       // ordem preservada, sem duplicatas entregues
  CHECK(b.duplicatesDropped() > 0);
}

TEST(net_bidirectional) {
  SimLink link({0.1, 0, 0.1, 3}); Connection a(link.a), b(link.b);
  int ga = 0, gb = 0; a.onMessage = [&](const Bytes&) { ++ga; }; b.onMessage = [&](const Bytes&) { ++gb; };
  for (int i = 0; i < 30; ++i) { a.send(msg(i)); b.send(msg(i)); }
  pump(a, b, 5000);
  CHECK(ga == 30 && gb == 30);
}

TEST(net_rejects_corrupted_and_oversized) {
  Quiet q; SimLink link; Connection a(link.a), b(link.b);
  int got = 0; b.onMessage = [&](const Bytes&) { ++got; };
  CHECK(!a.send(Bytes(2000, 1)));                  // maior que o limite
  a.send(msg(1)); a.update();
  auto pkt = link.b.receive(); CHECK(pkt.has_value());
  Bytes bad = *pkt; bad[8] ^= 0xFF;                // corrompe
  link.a.send(bad);                                // reinjeta do lado errado so p/ chegar em b? (a->b)
  b.update();
  CHECK(got == 0 && b.badPackets() >= 1);
}

TEST(net_fuzz_garbage_never_crashes) {
  Quiet q; SimLink link; Connection b(link.b);
  u32 s = 12345; auto rnd = [&] { s = s * 1664525u + 1013904223u; return s >> 16; };
  for (int i = 0; i < 3000; ++i) {
    Bytes junk(rnd() % 1300); for (auto& x : junk) x = static_cast<u8>(rnd());
    link.a.send(junk);
    if (i % 50 == 0) b.update();
  }
  b.update();
  CHECK(true); // sobreviveu (ASan/UBSan conferem acessos fora dos limites)
}

TEST(net_forged_packet_with_valid_crc_but_bad_lengths) {
  Quiet q; SimLink link; Connection b(link.b);
  Writer w; w.u16_(Connection::kMagic); w.u8_(1); w.u32_(0); w.u8_(1); w.u32_(0); w.u16_(60000); // len gigante, sem dados
  w.u32_(crc32(w.b.data(), w.b.size()));
  int got = 0; b.onMessage = [&](const Bytes&) { ++got; };
  link.a.send(w.b); b.update();
  CHECK(got == 0 && b.badPackets() == 1);
}

TEST(net_session_ownership_blocks_spoofing) {
  Quiet q; NetSession host(true);
  NetId player1 = host.registerObject(1);
  Json st = Json::object(); st.set("x", 5);
  CHECK(host.applyUpdate(1, player1, st, 0));                  // dono
  CHECK(!host.applyUpdate(2, player1, st, 0));                 // outro peer: spoof
  CHECK(debug().hasCode(code::kSpoof));
  CHECK(host.applyUpdate(NetSession::kHostPeer, player1, st, 0)); // host e autoridade
  CHECK(!host.applyUpdate(1, 999, st, 0));                     // objeto inexistente
  CHECK(!host.applyUpdate(1, player1, Json(5), 0));            // estado nao-objeto
}

TEST(net_session_rate_limit) {
  Quiet q; NetSession host(true);
  NetId id = host.registerObject(1);
  host.setPeerRate(1, 10, 3); // 10/s, rajada 3
  Json st = Json::object(); st.set("x", 1);
  int ok = 0; for (int i = 0; i < 10; ++i) ok += host.applyUpdate(1, id, st, 0.0);
  CHECK(ok == 3 && debug().hasCode(code::kRateLimit));
  CHECK(host.applyUpdate(1, id, st, 1.0)); // recarregou
}

TEST(net_snapshot_delta) {
  NetSession host(true);
  NetId a = host.registerObject(1), b = host.registerObject(2);
  Json s = Json::object(); s.set("hp", 10);
  host.applyUpdate(1, a, s, 0); host.applyUpdate(2, b, s, 0);
  Json full = host.snapshot({});
  CHECK(full.members().size() == 2);
  std::map<NetId, u32> known = {{a, host.version(a)}, {b, host.version(b)}};
  CHECK(host.snapshot(known).members().empty());           // nada mudou
  s.set("hp", 7); host.applyUpdate(1, a, s, 0);
  Json delta = host.snapshot(known);
  CHECK(delta.members().size() == 1 && delta.has(std::to_string(a)));
}
