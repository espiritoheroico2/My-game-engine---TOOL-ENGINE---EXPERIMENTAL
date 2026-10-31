#include "test.hpp"
#include "tool/core/json.hpp"

using namespace tool;

TEST(json_roundtrip) {
  auto j = Json::parse(R"({"a":[1,2.5,true,null,"x\n\u00e9"],"b":{"c":-3}})");
  CHECK(j.has_value());
  CHECK((*j)["a"].items().size() == 5 && (*j)["b"]["c"].asNumber() == -3);
  auto j2 = Json::parse(j->dump());
  CHECK(j2 && j2->dump() == j->dump());
}

TEST(json_rejects_malformed) {
  for (const char* bad : {"", "{", "[1,]", "{\"a\":}", "{\"a\" 1}", "\"abc", "[1 2]", "tru", "{\"a\":1} x", "\"\\q\"", "[\"\\u12\"]", "nan", "1e999"})
    CHECK(!Json::parse(bad).has_value());
}

TEST(json_depth_limit_blocks_stack_bomb) {
  std::string s(100000, '[');
  CHECK(!Json::parse(s).has_value());
  std::string ok = std::string(60, '[') + std::string(60, ']');
  CHECK(Json::parse(ok).has_value());
}

TEST(json_size_limit) {
  Json::Limits l; l.maxBytes = 10;
  CHECK(!Json::parse("[1,2,3,4,5,6,7,8]", l).has_value());
}
