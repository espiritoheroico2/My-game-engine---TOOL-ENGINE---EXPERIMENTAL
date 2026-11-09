#include "test.hpp"
#include <atomic>
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

TEST(ecs_nested_each_is_safe) {
  // each dentro de each: a reserva de indices de cada nivel nao pode se sobrepor (sem ponteiro pendurado)
  World w;
  for (int k = 0; k < 20; ++k) { Entity e = w.create(); w.add<Pos>(e, float(k), 0.f); w.add<Vel>(e, 1.f, 0.f); }
  int pairs = 0;
  w.each<Pos, Vel>([&](Entity, Pos&, Vel&) {
    w.each<Pos, Vel>([&](Entity, Pos&, Vel&) { ++pairs; });
  });
  CHECK(pairs == 20 * 20);
}

TEST(ecs_for_each_dense_visits_every_component) {
  World w;
  Entity a = w.create(), b = w.create(), c = w.create();
  w.add<Pos>(a, 1.f, 0.f); w.add<Pos>(b, 2.f, 0.f); w.add<Pos>(c, 3.f, 0.f);
  w.setDormant(b, true);
  float sum = 0; int n = 0;
  w.forEachDense<Pos>([&](Entity, Pos& p) { sum += p.x; ++n; });
  CHECK(n == 2);           // b esta dormente e fica de fora
  CHECK(sum == 4.f);       // 1 + 3
  w.forEachDense<Vel>([&](Entity, Vel&) { CHECK(false); }); // sem Vel: nada a visitar
}

TEST(ecs_parallel_each_matches_each) {
  World w;
  const int N = 5000;
  for (int k = 0; k < N; ++k) {
    Entity e = w.create();
    w.add<Pos>(e, float(k), 0.f);
    if (k % 3 != 0) w.add<Vel>(e, 1.f, 2.f);
  }
  // referencia serial
  int serialN = 0; double serialSum = 0;
  w.each<Pos, Vel>([&](Entity, Pos& p, Vel& v) { p.y += v.y; ++serialN; serialSum += p.x; });
  // paralelo: cada entidade escreve so no proprio Pos; contagem por atomico
  std::atomic<int> parN{0};
  w.parallelEach<Pos, Vel>([&](Entity, Pos& p, Vel& v) { p.y += v.y; parN.fetch_add(1); });
  CHECK(serialN == parN.load());
  CHECK(serialN == N - (N + 2) / 3);
  CHECK(w.count<Pos>() == static_cast<size_t>(N));
  CHECK(w.count<Vel>() == static_cast<size_t>(N - (N + 2) / 3));
}

TEST(ecs_simulating_flag_default_and_toggle) {
  World w;
  Entity e = w.create();
  CHECK(w.isSimulating(e));       // padrao: simula
  w.setSimulating(e, false);
  CHECK(!w.isSimulating(e));
  w.setSimulating(e, true);
  CHECK(w.isSimulating(e));
  w.destroy(e);
  Entity f = w.create();
  CHECK(w.isSimulating(f));       // indice reaproveitado volta ao padrao
}
