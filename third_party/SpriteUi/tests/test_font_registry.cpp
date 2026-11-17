// Registro de fontes (Text/FontRegistry.hpp): ids estaveis, fonte padrao, fallback e slice por fonte.
// Nao usa um atlas real: basta um TruetypeAtlas vazio, porque o registro so guarda e busca.
#include "SpriteUi/Text/FontRegistry.hpp"

#include <cstdio>
#include <string>

namespace {

using ToolEngine::SpriteUi::Rendering::WhiteTextureSlice;
using ToolEngine::SpriteUi::Text::FontId;
using ToolEngine::SpriteUi::Text::FontRegistry;
using ToolEngine::SpriteUi::Text::TruetypeAtlas;

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

void TestEmptyRegistryHasNoFont() {
    current = "TestEmptyRegistryHasNoFont";
    FontRegistry r;
    CHECK(r.Size() == 0);
    CHECK(!r.Default().IsValid());
    CHECK(r.Resolve(FontId{}) == nullptr);
    CHECK(r.Resolve(FontId{7}) == nullptr);
    CHECK(!r.SetDefault(FontId{1}));
}

void TestFirstAddBecomesDefaultAndIdsAreStable() {
    current = "TestFirstAddBecomesDefaultAndIdsAreStable";
    FontRegistry r;
    const FontId a = r.Add("Sans", TruetypeAtlas{}, 3);
    const FontId b = r.Add("Mono", TruetypeAtlas{}, 4);
    CHECK(a.IsValid() && b.IsValid());
    CHECK(a != b);
    CHECK(r.Default() == a);
    CHECK(r.Size() == 2);
    const auto* fa = r.Find(a);
    CHECK(fa != nullptr && fa->name == "Sans");
    CHECK(fa != nullptr && fa->textureSlice == 3);
    CHECK(fa != nullptr && fa->id == a);
    // Registrar outra fonte nao muda os ids anteriores.
    CHECK(r.Find(a) == fa);
}

void TestEachFontKeepsItsOwnSlice() {
    current = "TestEachFontKeepsItsOwnSlice";
    FontRegistry r;
    const FontId a = r.Add("A", TruetypeAtlas{}, 10);
    const FontId b = r.Add("B", TruetypeAtlas{}, 11);
    CHECK(r.Resolve(a)->textureSlice == 10);
    CHECK(r.Resolve(b)->textureSlice == 11);
    CHECK(r.Resolve(a)->textureSlice != r.Resolve(b)->textureSlice);
}

void TestInvalidOrUnknownIdFallsBackToDefault() {
    current = "TestInvalidOrUnknownIdFallsBackToDefault";
    FontRegistry r;
    const FontId a = r.Add("A", TruetypeAtlas{}, WhiteTextureSlice);
    CHECK(r.Find(FontId{}) == nullptr);
    CHECK(r.Find(FontId{999}) == nullptr);
    CHECK(r.Resolve(FontId{}) == r.Find(a));
    CHECK(r.Resolve(FontId{999}) == r.Find(a));
}

void TestSetDefaultChangesFallback() {
    current = "TestSetDefaultChangesFallback";
    FontRegistry r;
    const FontId a = r.Add("A", TruetypeAtlas{}, 1);
    const FontId b = r.Add("B", TruetypeAtlas{}, 2);
    CHECK(r.SetDefault(b));
    CHECK(r.Default() == b);
    CHECK(r.Resolve(FontId{}) == r.Find(b));
    CHECK(!r.SetDefault(FontId{999}));   // id desconhecido nao muda a padrao
    CHECK(r.Default() == b);
    CHECK(r.Find(a) != nullptr);         // a fonte antiga continua registrada
}

void TestPointersStayValidWhenRegistryGrows() {
    current = "TestPointersStayValidWhenRegistryGrows";
    FontRegistry r;
    const FontId first = r.Add("first", TruetypeAtlas{}, 1);
    const auto* before = r.Find(first);
    for (int i = 0; i < 64; ++i) r.Add("f" + std::to_string(i), TruetypeAtlas{}, 2);
    CHECK(r.Find(first) == before);
    CHECK(before->name == "first");
}

} // namespace

int main() {
    TestEmptyRegistryHasNoFont();
    TestFirstAddBecomesDefaultAndIdsAreStable();
    TestEachFontKeepsItsOwnSlice();
    TestInvalidOrUnknownIdFallsBackToDefault();
    TestSetDefaultChangesFallback();
    TestPointersStayValidWhenRegistryGrows();
    std::printf("font registry: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
