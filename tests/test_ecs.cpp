#include "test.hpp"
#include "tool/ecs/world.hpp"

using namespace tool;

struct Pos { float x = 0, y = 0; };
struct Vel { float x = 0, y = 0; };

TEST(ecs_add_get_remove) {
  World w;
  Entity e = w.create();
  w.add<Pos>(e, 1.f, 2.f);
  CHECK(w.has<Pos>(e));
  CHECK(w.get<Pos>(e)->y == 2.f);
  w.remove<Pos>(e);
  CHECK(!w.has<Pos>(e));
}

TEST(ecs_each_joins_components) {
  World w;
  Entity a = w.create(), b = w.create(), c = w.create();
  w.add<Pos>(a, 0.f, 0.f); w.add<Vel>(a, 1.f, 1.f);
  w.add<Pos>(b, 5.f, 5.f);
  w.add<Vel>(c, 9.f, 9.f);
  int n = 0;
  w.each<Pos, Vel>([&](Entity, Pos& p, Vel& v) { p.x += v.x; ++n; });
  CHECK(n == 1);
  CHECK(w.get<Pos>(a)->x == 1.f);
  CHECK(w.get<Pos>(b)->x == 5.f);
}

TEST(ecs_destroy_removes_components_and_stale_handle) {
  World w;
  Entity e = w.create();
  w.add<Pos>(e);
  CHECK(w.destroy(e));
  CHECK(!w.alive(e));
  CHECK(w.get<Pos>(e) == nullptr);
  Entity e2 = w.create();
  CHECK(!w.has<Pos>(e2)); // slot reaproveitado vem limpo
  int n = 0;
  w.each<Pos>([&](Entity, Pos&) { ++n; });
  CHECK(n == 0);
}

TEST(ecs_swap_remove_keeps_others) {
  World w;
  Entity a = w.create(), b = w.create(), c = w.create();
  w.add<Pos>(a, 1.f); w.add<Pos>(b, 2.f); w.add<Pos>(c, 3.f);
  w.destroy(a);
  CHECK(w.get<Pos>(b)->x == 2.f);
  CHECK(w.get<Pos>(c)->x == 3.f);
}

TEST(ecs_tags_and_query) {
  World w;
  TagRegistry t;
  TagId en = t.intern("enemy"), boss = t.intern("boss");
  Entity a = w.create(), b = w.create();
  w.addTag(a, en); w.addTag(a, boss); w.addTag(b, en);
  TagMask q; q.set(en); q.set(boss);
  int n = 0;
  w.eachTagged(q, [&](Entity e) { CHECK(e == a); ++n; });
  CHECK(n == 1);
}

TEST(ecs_dormant_hidden_from_queries) {
  World w;
  Entity a = w.create();
  w.add<Pos>(a);
  w.setDormant(a, true);
  int n = 0;
  w.each<Pos>([&](Entity, Pos&) { ++n; });
  CHECK(n == 0);
}
