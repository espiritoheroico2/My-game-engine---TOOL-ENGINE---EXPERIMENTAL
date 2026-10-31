#include "test.hpp"
#include "tool/core/handle.hpp"
#include "tool/core/tags.hpp"

using namespace tool;

TEST(handle_generation_invalidates_old) {
  HandleAllocator a;
  Handle h1 = a.create();
  CHECK(a.alive(h1));
  CHECK(a.destroy(h1));
  CHECK(!a.alive(h1));
  Handle h2 = a.create(); // reusa o slot
  CHECK(h2.index == h1.index);
  CHECK(h2.generation != h1.generation);
  CHECK(!a.alive(h1));
  CHECK(a.alive(h2));
  CHECK(!a.destroy(h1));
}

TEST(handle_renew_keeps_slot_alive) {
  HandleAllocator a;
  Handle h = a.create();
  Handle r = a.renew(h);
  CHECK(!a.alive(h));
  CHECK(a.alive(r));
  CHECK(a.liveCount() == 1);
}

TEST(tags_intern_and_mask) {
  TagRegistry r;
  TagId a = r.intern("enemy"), b = r.intern("boss");
  CHECK(a != b);
  CHECK(r.intern("enemy") == a);
  CHECK(r.name(b) == "boss");
  TagMask m;
  m.set(a);
  m.set(100);
  CHECK(m.test(a) && m.test(100) && !m.test(b));
  TagMask q;
  q.set(100);
  CHECK(m.containsAll(q));
  m.clear(100);
  CHECK(!m.containsAll(q));
}

TEST(tags_limit_128) {
  TagRegistry r;
  for (u32 i = 0; i < kMaxTags; ++i) CHECK(r.intern("t" + std::to_string(i)) == i);
  CHECK(r.intern("extra") == kInvalidIndex);
}

TEST(tag_registry_find_and_rename) {
  TagRegistry t; const TagId a = t.intern("player"), b = t.intern("enemy");
  CHECK(t.find("player") == a && t.find("nada") == kInvalidIndex && t.size() == 2);
  CHECK(t.rename(a, "jogador") && t.name(a) == "jogador" && t.find("jogador") == a && t.find("player") == kInvalidIndex);
  CHECK(!t.rename(a, "enemy") && !t.rename(a, "") && !t.rename(a, std::string(41, 'x')) && !t.rename(99, "x")); // duplicado/vazio/longo/id invalido
  CHECK(t.name(b) == "enemy" && t.intern("jogador") == a);
}
