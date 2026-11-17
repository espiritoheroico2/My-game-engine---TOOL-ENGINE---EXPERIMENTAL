// Cache de atlas de fonte (Text/FontAtlasCache.hpp). Nao usa fonte real: os atlas sao sinteticos, porque o
// cache so guarda, busca e despeja. Roda sem stb_truetype e sem GPU.
#include "SpriteUi/Text/FontAtlasCache.hpp"

#include <cstdio>
#include <vector>

namespace {

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

// Atlas sintetico com 'bytes' pixels: o custo de memoria dele e exatamente 'bytes' (sem glifos).
std::function<bool(TruetypeAtlas&)> Builder(std::size_t bytes, int* calls = nullptr) {
    return [bytes, calls](TruetypeAtlas& atlas) {
        if (calls) ++*calls;
        atlas.width = static_cast<std::uint32_t>(bytes);
        atlas.height = 1;
        atlas.pixels.assign(bytes, 0);
        return true;
    };
}

FontAtlasKey KeyFor(std::uint32_t charsetId) {
    FontAtlasKey key;
    key.file = 0xABCDu;
    key.rasterBucket = 48;
    key.charsetId = charsetId;
    return key;
}

// Duas chaves iguais tem o mesmo hash; cada campo diferente torna a chave diferente.
void TestKeyEqualityAndHash() {
    current = "TestKeyEqualityAndHash";
    FontAtlasKey a = KeyFor(1);
    FontAtlasKey b = KeyFor(1);
    CHECK(a == b);
    CHECK(FontAtlasKeyHash{}(a) == FontAtlasKeyHash{}(b));

    FontAtlasKey weight = a; weight.weight = 700;
    FontAtlasKey style = a; style.style = FontStyle::Italic;
    FontAtlasKey mode = a; mode.mode = TextRenderMode::Msdf;
    FontAtlasKey bucket = a; bucket.rasterBucket = 64;
    FontAtlasKey face = a; face.faceIndex = 2;
    FontAtlasKey file = a; file.file = 0x1234u;
    CHECK(!(a == weight));
    CHECK(!(a == style));
    CHECK(!(a == mode));
    CHECK(!(a == bucket));
    CHECK(!(a == face));
    CHECK(!(a == file));
}

// O hash de fonte depende so do conteudo: bytes iguais batem, um byte diferente muda o hash, e vazio e o offset basis.
void TestFontHashFollowsContent() {
    current = "TestFontHashFollowsContent";
    const std::vector<std::uint8_t> one{1, 2, 3, 4};
    const std::vector<std::uint8_t> same{1, 2, 3, 4};
    const std::vector<std::uint8_t> other{1, 2, 3, 5};
    CHECK(HashFontBytes(one) == HashFontBytes(same));
    CHECK(HashFontBytes(one) != HashFontBytes(other));
    CHECK(HashFontBytes({}) == 0xcbf29ce484222325ULL);
}

// Mesma chave devolve o mesmo atlas, sem reconstruir.
void TestReuseDoesNotRebuild() {
    current = "TestReuseDoesNotRebuild";
    FontAtlasCache cache;
    int calls = 0;
    const auto first = cache.GetOrBuild(KeyFor(1), Builder(100, &calls));
    const auto second = cache.GetOrBuild(KeyFor(1), Builder(100, &calls));
    CHECK(first != nullptr);
    CHECK(first == second);
    CHECK(calls == 1);
    CHECK(cache.Size() == 1);
    CHECK(cache.Bytes() == 100);
}

// Falha de construcao devolve nullptr e nao deixa entrada parcial.
void TestFailedBuildLeavesNothing() {
    current = "TestFailedBuildLeavesNothing";
    FontAtlasCache cache;
    const auto result = cache.GetOrBuild(KeyFor(2), [](TruetypeAtlas&) { return false; });
    CHECK(result == nullptr);
    CHECK(cache.Size() == 0);
    CHECK(cache.Bytes() == 0);
    CHECK(cache.Find(KeyFor(2)) == nullptr);
}

// Limite de entradas: ao inserir a terceira, sai a menos usada, e o callback avisa o host com a chave certa.
void TestEvictsLeastRecentlyUsedByEntryCount() {
    current = "TestEvictsLeastRecentlyUsedByEntryCount";
    std::vector<FontAtlasKey> evicted;
    FontAtlasCache::Config config;
    config.maxEntries = 2;
    config.onEvict = [&evicted](const FontAtlasKey& key) { evicted.push_back(key); };
    FontAtlasCache cache(config);

    static_cast<void>(cache.GetOrBuild(KeyFor(1), Builder(10)));   // A
    static_cast<void>(cache.GetOrBuild(KeyFor(2), Builder(10)));   // B
    static_cast<void>(cache.Find(KeyFor(1)));                      // A volta a ser a mais recente
    static_cast<void>(cache.GetOrBuild(KeyFor(3), Builder(10)));   // C: sai B
    CHECK(cache.Size() == 2);
    CHECK(cache.Find(KeyFor(2)) == nullptr);
    CHECK(cache.Find(KeyFor(1)) != nullptr);
    CHECK(cache.Find(KeyFor(3)) != nullptr);
    CHECK(evicted.size() == 1);
    CHECK(!evicted.empty() && evicted[0] == KeyFor(2));
}

// Entrada com referencia viva (em uso) nunca sai, mesmo acima do orcamento. Soltada, sai no Trim.
void TestInUseEntryIsNeverEvicted() {
    current = "TestInUseEntryIsNeverEvicted";
    FontAtlasCache::Config config;
    config.maxEntries = 1;
    FontAtlasCache cache(config);

    auto held = cache.GetOrBuild(KeyFor(1), Builder(10));   // A, em uso
    static_cast<void>(cache.GetOrBuild(KeyFor(2), Builder(10)));                // B, sem uso externo depois do return
    CHECK(cache.Find(KeyFor(1)) != nullptr);                 // A segue no cache, apesar do limite de 1
    CHECK(cache.Size() == 2);                                // acima do orcamento enquanto A esta em uso

    held.reset();                                            // soltou A
    cache.Trim();
    // A foi tocada por Find acima, entao e a mais recente: quem sai e B, a menos usada recentemente.
    CHECK(cache.Size() == 1);
    CHECK(cache.Find(KeyFor(2)) == nullptr);
    CHECK(cache.Find(KeyFor(1)) != nullptr);
}

// Limite de bytes: dois atlas de 1000 bytes com teto de 1500 deixam so um.
void TestBytesBudgetEvicts() {
    current = "TestBytesBudgetEvicts";
    FontAtlasCache::Config config;
    config.maxBytes = 1500;
    FontAtlasCache cache(config);
    static_cast<void>(cache.GetOrBuild(KeyFor(1), Builder(1000)));
    CHECK(cache.Bytes() == 1000);
    static_cast<void>(cache.GetOrBuild(KeyFor(2), Builder(1000)));   // 2000 > 1500: sai A (a mais antiga sem uso)
    CHECK(cache.Size() == 1);
    CHECK(cache.Bytes() == 1000);
    CHECK(cache.Find(KeyFor(1)) == nullptr);
}

// Clear: sai tudo que nao esta em uso e mantem o que esta.
void TestClearKeepsInUseEntries() {
    current = "TestClearKeepsInUseEntries";
    int evictions = 0;
    FontAtlasCache::Config config;
    config.onEvict = [&evictions](const FontAtlasKey&) { ++evictions; };
    FontAtlasCache cache(config);
    auto keep = cache.GetOrBuild(KeyFor(1), Builder(10));
    static_cast<void>(cache.GetOrBuild(KeyFor(2), Builder(10)));
    cache.Clear();
    CHECK(cache.Size() == 1);
    CHECK(evictions == 1);
    CHECK(cache.Find(KeyFor(1)) != nullptr);
}

} // namespace

int main() {
    TestKeyEqualityAndHash();
    TestFontHashFollowsContent();
    TestReuseDoesNotRebuild();
    TestFailedBuildLeavesNothing();
    TestEvictsLeastRecentlyUsedByEntryCount();
    TestInUseEntryIsNeverEvicted();
    TestBytesBudgetEvicts();
    TestClearKeepsInUseEntries();
    std::printf("font atlas cache: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
