// Custo do quadro: cache de layout de texto, layout que nao refaz a arvore sem mudanca e culling de itens fora do
// recorte. Executavel proprio, sem o harness principal.

#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
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

// Tamanho fixo que nao encolhe: sem shrink = 0, filhos maiores que o pai perdem largura e o teste nao mede o que
// queria.
FlexItem Fixed(float w, float h) {
    FlexItem style;
    style.width = {SizeMode::Fixed, w};
    style.height = {SizeMode::Fixed, h};
    style.shrink = 0.0f;
    return style;
}

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

// Sem mudanca, Layout nao refaz a arvore; uma mudanca de estilo ou de viewport recalcula.
void TestLayoutSkipsWhenNothingChanged() {
    current = "TestLayoutSkipsWhenNothingChanged";
    LayoutTree tree;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 200.0f));
    const UIHandle a = tree.Create(root, Fixed(100.0f, 20.0f));
    CHECK(tree.IsLayoutDirty());
    tree.Layout({{0.0f, 0.0f}, {300.0f, 200.0f}});
    CHECK(!tree.IsLayoutDirty());
    const float x1 = tree.Get(a)->node.rect.size.x;
    LayoutStats stats{};
    tree.Layout({{0.0f, 0.0f}, {300.0f, 200.0f}}, &stats);
    CHECK(!tree.IsLayoutDirty());
    CHECK(stats.measured == 0);                    // pulou: nenhuma medida feita
    CHECK(tree.Get(a)->node.rect.size.x == x1);    // resultado anterior preservado

    FlexItem wider = Fixed(150.0f, 20.0f);
    CHECK(tree.SetStyle(a, wider));
    CHECK(tree.IsLayoutDirty());
    tree.Layout({{0.0f, 0.0f}, {300.0f, 200.0f}});
    CHECK(tree.Get(a)->node.rect.size.x == 150.0f); // mudanca de estilo chega ao resultado

    tree.Layout({{0.0f, 0.0f}, {400.0f, 200.0f}}); // viewport diferente recalcula
    CHECK(!tree.IsLayoutDirty());
    CHECK(tree.SetVisible(a, false));
    CHECK(tree.IsLayoutDirty());
}

// Elemento todo fora do recorte nao gera item de desenho, mas continua no acerto. Dentro do recorte, gera item.
void TestCulledElementHasNoRenderItemButKeepsHitTarget() {
    current = "TestCulledElementHasNoRenderItemButKeepsHitTarget";
    LayoutTree tree;
    LayoutSceneBridge bridge;
    const UIHandle root = tree.Create({}, Fixed(300.0f, 200.0f));
    const UIHandle panel = tree.Create(root, Fixed(100.0f, 100.0f));
    // Layout em linha: o primeiro filho fica em x = 0..40 (dentro do painel 0..100); o segundo, com margem,
    // fica em x = 150..200 (fora).
    const UIHandle inside = tree.Create(panel, Fixed(40.0f, 40.0f));
    FlexItem outsideStyle = Fixed(50.0f, 50.0f);
    outsideStyle.margin = {110.0f, 0.0f, 0.0f, 0.0f};
    const UIHandle outside = tree.Create(panel, outsideStyle);
    ElementVisual clip;
    clip.clipOverflow = true;
    bridge.SetVisual(panel, clip);
    ElementVisual v;
    v.focusable = true;
    bridge.SetVisual(outside, v);
    bridge.SetVisual(inside, v);
    tree.Layout({{0.0f, 0.0f}, {300.0f, 200.0f}});
    const SceneExtractionResult scene = bridge.Extract(tree);

    bool outsideDrawn = false;
    bool insideDrawn = false;
    for (const auto& item : scene.renderItems) {
        outsideDrawn = outsideDrawn || item.handle == outside;
        insideDrawn = insideDrawn || item.handle == inside;
    }
    CHECK(!outsideDrawn);
    CHECK(insideDrawn);
    CHECK(scene.culledRenderItems == 1);
    bool outsideTarget = false;
    for (const auto& target : scene.hitTargets) outsideTarget = outsideTarget || target.handle == outside;
    CHECK(outsideTarget);   // o acerto nao muda: a ordem de quem sobra continua a mesma
}

} // namespace

int main() {
    TestSameKeyBuildsOnce();
    TestAnyLayoutInputChangesTheKey();
    TestEvictionKeepsTheRecentlyUsedEntry();
    TestLayoutSkipsWhenNothingChanged();
    TestCulledElementHasNoRenderItemButKeepsHitTarget();
    std::printf("frame cost: %d/%d checks passed\n", run - failed, run);
    return failed == 0 ? 0 : 1;
}
