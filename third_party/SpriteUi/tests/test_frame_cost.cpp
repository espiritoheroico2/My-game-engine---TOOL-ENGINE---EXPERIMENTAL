// Cache de layout de texto: chaves, reutilizacao e politica de expulsao. Executavel proprio, sem o harness principal.

#include "SpriteUi/Text/TextLayoutCache.hpp"

#include <cstdio>

namespace {

using namespace ToolEngine::SpriteUi;
using namespace ToolEngine::SpriteUi::Text;

int run = 0;
int failed = 0;
const char* current = "";

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s: %s\n", current, expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

TextLayoutResult FakeLayout(float width) {
    TextLayoutResult r;
    r.size = {width, 16.0f};
    r.lineHeight = 16.0f;
    return r;
}

void TestSameKeyBuildsOnce() {
    current = "TestSameKeyBuildsOnce";
    TextLayoutCache cache(8);
    TextLayoutConfig cfg;
    cfg.fontSize = 16.0f;
    const TextLayoutKey key = MakeTextLayoutKey("Comprar", cfg, 7);
    int builds = 0;
    auto build = [&] { ++builds; return FakeLayout(60.0f); };
    const TextLayoutResult& a = cache.GetOrLayout(key, build);
    CHECK(a.size.x == 60.0f);
    for (int i = 0; i < 50; ++i) (void)cache.GetOrLayout(key, build);
    CHECK(builds == 1);
    CHECK(cache.Hits() == 50);
    CHECK(cache.Misses() == 1);
}

void TestAnyLayoutInputChangesTheKey() {
    current = "TestAnyLayoutInputChangesTheKey";
    TextLayoutCache cache(64);
    TextLayoutConfig cfg;
    cfg.fontSize = 16.0f;
    int builds = 0;
    auto build = [&] { ++builds; return FakeLayout(10.0f); };
    (void)cache.GetOrLayout(MakeTextLayoutKey("Ajuda", cfg, 1), build);
    (void)cache.GetOrLayout(MakeTextLayoutKey("Ajuda", cfg, 2), build);             // outra fonte
    (void)cache.GetOrLayout(MakeTextLayoutKey("Ajudas", cfg, 1), build);            // outro texto
    TextLayoutConfig scaled = cfg;
    scaled.devicePixelScale = 1.25f;                                                 // outra escala de interface
    (void)cache.GetOrLayout(MakeTextLayoutKey("Ajuda", scaled, 1), build);
    TextLayoutConfig bigger = cfg;
    bigger.fontSize = 18.0f;
    (void)cache.GetOrLayout(MakeTextLayoutKey("Ajuda", bigger, 1), build);
    CHECK(builds == 5);
    CHECK(cache.Size() == 5);
}

void TestEvictionKeepsTheRecentlyUsedEntry() {
    current = "TestEvictionKeepsTheRecentlyUsedEntry";
    TextLayoutCache cache(2);
    TextLayoutConfig cfg;
    int builds = 0;
    auto build = [&] { ++builds; return FakeLayout(1.0f); };
    (void)cache.GetOrLayout(MakeTextLayoutKey("a", cfg, 1), build);
    (void)cache.GetOrLayout(MakeTextLayoutKey("b", cfg, 1), build);
    (void)cache.GetOrLayout(MakeTextLayoutKey("a", cfg, 1), build);   // "a" fica recente
    (void)cache.GetOrLayout(MakeTextLayoutKey("c", cfg, 1), build);   // sai "b", o mais antigo
    CHECK(cache.Size() == 2);
    CHECK(cache.MaxEntries() == 2);
    const int before = builds;
    (void)cache.GetOrLayout(MakeTextLayoutKey("a", cfg, 1), build);   // ainda la: sem novo build
    CHECK(builds == before);
    (void)cache.GetOrLayout(MakeTextLayoutKey("b", cfg, 1), build);   // foi removida: refaz
    CHECK(builds == before + 1);
}

} // namespace

int main() {
    TestSameKeyBuildsOnce();
    TestAnyLayoutInputChangesTheKey();
    TestEvictionKeepsTheRecentlyUsedEntry();
    std::printf("text layout cache: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
