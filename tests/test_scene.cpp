#include "test.hpp"
#include "tool/scene/scene.hpp"

using namespace tool;

namespace {
struct Pos { float x = 0; };
struct Hp { int v = 100; };
struct Quiet { Quiet() { Logger::get().setSink([](LogLevel, std::string_view) {}); debug().reset(); } ~Quiet() { Logger::get().setSink(nullptr); } };
Blueprint enemyBp(Scene& s) { Blueprint b; b.tag(s.tag("enemy")).with<Pos>(3.f).with<Hp>(50); return b; }
}

TEST(scene_spawn_and_access) {
  Scene s; auto bp = enemyBp(s);
  auto o = s.spawn(bp);
  CHECK(o.valid() && o.has<Hp>() && o.get<Hp>()->v == 50);
  CHECK(o.hasTag(s.tag("enemy")));
}

TEST(scene_delete_and_reuse_reuses_identical) {
  Quiet q; Scene s; auto bp = enemyBp(s);
  auto a = s.spawn(bp);
  a.get<Hp>()->v = 0;
  a.DeleteAndReuse();
  CHECK(!a.valid());              // handle antigo morre
  CHECK(s.dormantCount() == 1);
  auto b = s.spawn(bp);           // identico -> reaproveita
  CHECK(s.stats().reused == 1);
  CHECK(s.dormantCount() == 0);
  CHECK(b.entity().index == a.entity().index);
  CHECK(b.get<Hp>()->v == 50);    // valores de criacao restaurados
  CHECK(!a.valid());              // o antigo continua invalido
  CHECK(s.world().liveCount() == 1);
}

TEST(scene_different_signature_not_reused) {
  Quiet q; Scene s; auto bp = enemyBp(s);
  Blueprint other; other.with<Pos>(1.f);
  auto a = s.spawn(bp);
  a.DeleteAndReuse();
  auto b = s.spawn(other);
  CHECK(s.stats().reused == 0);
  CHECK(s.dormantCount() == 1);
  CHECK(b.entity().index != a.entity().index);
}

TEST(scene_reuse_strips_extra_components_and_tags) {
  Quiet q; Scene s; auto bp = enemyBp(s);
  struct Extra { int x = 1; };
  auto a = s.spawn(bp);
  s.world().add<Extra>(a.entity());
  a.addTag(s.tag("temp"));
  a.DeleteAndReuse();
  auto b = s.spawn(bp);
  CHECK(!s.world().has<Extra>(b.entity()));
  CHECK(!b.hasTag(s.tag("temp")));
}

TEST(scene_pool_expires_by_idle_time) {
  Quiet q; Scene s; s.setPoolPolicy({10.0, 100, 100});
  auto bp = enemyBp(s);
  s.setClock(0); auto a = s.spawn(bp); a.DeleteAndReuse();
  s.collectPool(5.0);  CHECK(s.dormantCount() == 1);
  s.collectPool(11.0); CHECK(s.dormantCount() == 0);
  CHECK(s.stats().freed == 1);
  CHECK(s.world().liveCount() == 0);   // memoria liberada de fato
}

TEST(scene_pool_limits_and_flush) {
  Quiet q; Scene s; s.setPoolPolicy({1000.0, 2, 3});
  auto bp = enemyBp(s);
  std::vector<ToolObject> v;
  for (int i = 0; i < 6; ++i) v.push_back(s.spawn(bp));
  for (auto& o : v) o.DeleteAndReuse();
  s.collectPool(0);
  CHECK(s.dormantCount() <= 2);
  s.flushPool();
  CHECK(s.dormantCount() == 0);
  CHECK(s.world().liveCount() == 0);
}

TEST(scene_delete_reuse_stale_handle_warns) {
  Quiet q; Scene s; auto bp = enemyBp(s);
  auto a = s.spawn(bp);
  a.DeleteAndReuse();
  a.DeleteAndReuse();
  CHECK(debug().hasCode(diag::kStaleHandle));
}
