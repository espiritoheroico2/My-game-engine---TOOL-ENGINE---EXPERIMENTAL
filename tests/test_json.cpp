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

// F-06: gramatica numerica estrita.
TEST(json_number_grammar_is_strict) {
  for (const char* bad : {"01", "-01", "-", "+1", ".5", "1.", "1e", "1e+", "1.2.3", "0x10", "-.5", "00", "1E"})
    CHECK(!Json::parse(bad).has_value());
  for (const char* ok : {"0", "-0", "10", "-1.5e+3", "1E-2", "0.5", "123456789"})
    CHECK(Json::parse(ok).has_value());
  auto v = Json::parse("-1.5e+3");
  CHECK(v && v->asNumber() == -1500.0);
}

// F-06: pares substitutos UTF-16 (fora do BMP) e erros de surrogate.
TEST(json_utf16_surrogates) {
  auto emoji = Json::parse(R"("😀")");           // U+1F600
  CHECK(emoji && emoji->asString() == "\xF0\x9F\x98\x80");
  auto bmp = Json::parse(R"("é")");                   // BMP continua igual
  CHECK(bmp && bmp->asString() == "\xC3\xA9");
  for (const char* bad : {R"("\ud83d")",                    // alto sem baixo
                          R"("\ud83dA")",              // alto seguido de nao-baixo
                          R"("\ude00")",                    // baixo isolado
                          R"("\ud83d\ud83d")",              // dois altos
                          R"("\ud83d\ude0")",               // baixo truncado
                          R"("\ud83d)"})                    // alto no fim da entrada
    CHECK(!Json::parse(bad).has_value());
}

// F-06: truncados nunca viram valor.
TEST(json_truncated_inputs_rejected) {
  for (const char* bad : {"{\"a\":1", "[1,2", "\"abc", "\"\\u00", "\"\\", "{\"a", "[", "{"})
    CHECK(!Json::parse(bad).has_value());
}

// F-06: chave duplicada - padrao mantem o ultimo valor; rejectDuplicateKeys recusa.
TEST(json_duplicate_keys_options) {
  const char* dup = R"({"a":1,"a":2})";
  auto last = Json::parse(dup);
  CHECK(last && (*last)["a"].asNumber() == 2);             // comportamento padrao preservado
  Json::Options strict; strict.rejectDuplicateKeys = true;
  CHECK(!Json::parse(dup, Json::Limits{}, strict).has_value());
  auto unique = Json::parse(R"({"a":1,"b":2})", Json::Limits{}, strict);
  CHECK(unique && (*unique)["b"].asNumber() == 2);         // sem duplicata, aceita
}
