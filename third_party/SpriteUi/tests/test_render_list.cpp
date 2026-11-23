// Lote de draw calls e cache de lista (RenderListCache.hpp).
// Cobre: troca de textura quebra o lote (A-B-A vira tres lotes, A-A vira um), e o cache refaz so os nos
// sujos. Sem janela.
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/Primitives.hpp"
#include "SpriteUi/Rendering/RenderListCache.hpp"
#include "SpriteUi/Rendering/Sprite.hpp"

#include <array>
#include <cstdio>
#include <vector>

namespace {

using namespace ToolEngine::SpriteUi;
namespace R = ToolEngine::SpriteUi::Rendering;

int run = 0;
int failed = 0;

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        ++run;                                                                                               \
        if (!(expr)) {                                                                                       \
            ++failed;                                                                                        \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);                           \
        }                                                                                                    \
    } while (0)

// Um sprite de teste num atlas dado. Usa o caminho real de MakeSpriteItem.
R::UIRenderItem SpriteOnAtlas(std::uint32_t atlas, std::uint32_t id) {
    R::UISprite sprite;
    sprite.atlasId = atlas;
    sprite.sizePixels = {4.0f, 4.0f};
    sprite.anchors = {{0.0f, 0.0f}};
    return *R::MakeSpriteItem(UIHandle{id, 1}, sprite, {0.0f, 0.0f});
}

void TestTextureSwitchBreaksBatch() {
    const std::vector<R::UIRenderItem> aba{
        SpriteOnAtlas(1, 1), SpriteOnAtlas(2, 2), SpriteOnAtlas(1, 3)};
    R::ExtractionStats stats;
    const R::FramePacket packet = R::RenderExtractor{}.Build(1, aba, &stats);
    CHECK(packet.batches.size() == 3);
    CHECK(stats.batchCount == 3);

    const std::vector<R::UIRenderItem> aa{
        SpriteOnAtlas(1, 1), SpriteOnAtlas(1, 2), SpriteOnAtlas(1, 3)};
    const R::FramePacket same = R::RenderExtractor{}.Build(2, aa);
    CHECK(same.batches.size() == 1);
    CHECK(same.batches.front().instanceCount == 3);
}

// Monta um no: um item de cor solida por no, com cor que depende de um valor externo.
std::vector<R::UIRenderItem> NodeItems(std::size_t node, float tint) {
    std::vector<R::UIRenderItem> items;
    items.push_back(R::MakeSolidRect(UIHandle{static_cast<std::uint32_t>(node + 1), 1},
                                     {{static_cast<float>(node) * 10.0f, 0.0f}, {5.0f, 5.0f}},
                                     {tint, tint, tint, 1.0f}));
    return items;
}

void TestCacheWithoutChangeRebuildsNothing() {
    R::RenderListCache cache;
    cache.Reset(3);
    std::size_t calls = 0;
    const auto builder = [&](std::size_t node, std::vector<R::UIRenderItem>& out) {
        ++calls;
        out = NodeItems(node, 0.5f);
    };

    CHECK(cache.Update(builder).size() == 3);
    CHECK(calls == 3 && cache.LastRebuildCount() == 3);

    calls = 0;
    CHECK(cache.Update(builder).size() == 3);
    CHECK(calls == 0 && cache.LastRebuildCount() == 0);
}

void TestCacheRebuildsOnlyDirtyNode() {
    R::RenderListCache cache;
    cache.Reset(3);
    float tintOfNode1 = 0.2f;
    std::vector<std::size_t> rebuilt;
    const auto builder = [&](std::size_t node, std::vector<R::UIRenderItem>& out) {
        rebuilt.push_back(node);
        out = NodeItems(node, node == 1 ? tintOfNode1 : 0.5f);
    };
    cache.Update(builder);

    rebuilt.clear();
    tintOfNode1 = 0.9f;
    cache.MarkDirty(1);
    const auto& items = cache.Update(builder);

    CHECK(rebuilt.size() == 1 && rebuilt.front() == 1);
    CHECK(cache.LastRebuildCount() == 1);
    CHECK(items.size() == 3);
    CHECK(items[1].tintPremultipliedLinear.x == 0.9f);
    CHECK(items[0].tintPremultipliedLinear.x == 0.5f);
    CHECK(items[2].tintPremultipliedLinear.x == 0.5f);
}

void TestCacheUpdatesSameSizeInPlace() {
    R::RenderListCache cache;
    cache.Reset(4);
    std::array<float, 4> tints{0.1f, 0.2f, 0.3f, 0.4f};
    const auto builder = [&](std::size_t node, std::vector<R::UIRenderItem>& out) {
        out = NodeItems(node, tints[node]);
    };

    cache.Update(builder);
    const auto& initial = cache.Update(builder);
    const auto* stableItem = &initial[2];

    tints[0] = 0.8f;
    tints[2] = 0.9f;
    cache.MarkDirty(0);
    cache.MarkDirty(2);
    const auto& updated = cache.Update(builder);

    CHECK(&updated[2] == stableItem);
    CHECK(cache.LastRebuildCount() == 2);
    CHECK(updated.size() == 4);
    CHECK(updated[0].tintPremultipliedLinear.x == 0.8f);
    CHECK(updated[1].tintPremultipliedLinear.x == 0.2f);
    CHECK(updated[2].tintPremultipliedLinear.x == 0.9f);
    CHECK(updated[3].tintPremultipliedLinear.x == 0.4f);
}

void TestCacheHandlesNodeSizeChange() {
    R::RenderListCache cache;
    cache.Reset(2);
    std::size_t countOfNode0 = 1;
    const auto builder = [&](std::size_t node, std::vector<R::UIRenderItem>& out) {
        if (node == 0) {
            for (std::size_t i = 0; i < countOfNode0; ++i) {
                out.push_back(R::MakeSolidRect(UIHandle{1, 1}, {}, {1.0f, 1.0f, 1.0f, 1.0f}));
            }
        } else {
            out.push_back(R::MakeSolidRect(UIHandle{2, 1}, {}, {0.0f, 0.0f, 0.0f, 1.0f}));
        }
    };
    CHECK(cache.Update(builder).size() == 2);

    countOfNode0 = 3;
    cache.MarkDirty(0);
    // O no 1 nao esta sujo e deve continuar no fim da lista, depois dos tres itens novos do no 0.
    const auto& items = cache.Update(builder);
    CHECK(items.size() == 4);
    CHECK(items.back().handle.Pack() == (UIHandle{2, 1}).Pack());
}

void TestBuildIntoReusesStorageAndClearsData() {
    std::vector<R::UIRenderItem> items;
    for (std::uint32_t i = 0; i < 8; ++i) {
        auto item = R::MakeSolidRect(UIHandle{i + 1, 1}, {}, {1.0f, 1.0f, 1.0f, 1.0f});
        item.paintOrder = i;
        items.push_back(item);
    }

    R::FramePacket packet;
    R::RenderExtractionScratch scratch;
    R::RenderExtractor extractor;
    extractor.BuildInto(packet, 1, items, nullptr, {R::ClipRectGPU{0, 0, 10, 10}}, &scratch);
    const auto instanceCapacity = packet.instances.capacity();
    const auto batchCapacity = packet.batches.capacity();
    const auto clipCapacity = packet.clipRects.capacity();
    const auto scratchCapacity = scratch.ordered.capacity();

    items.resize(1);
    extractor.BuildInto(packet, 2, items, nullptr, {}, &scratch);
    CHECK(packet.frameId == 2);
    CHECK(packet.instances.size() == 1 && packet.batches.size() == 1 && packet.clipRects.empty());
    CHECK(packet.instances.capacity() == instanceCapacity);
    CHECK(packet.batches.capacity() == batchCapacity);
    CHECK(packet.clipRects.capacity() == clipCapacity);
    CHECK(scratch.ordered.capacity() == scratchCapacity);
    CHECK(R::IsFramePacketValid(packet));
}

void TestBuildIntoPreservesPaintOrder() {
    auto first = R::MakeSolidRect(UIHandle{1, 1}, {}, {1.0f, 1.0f, 1.0f, 1.0f});
    auto second = R::MakeSolidRect(UIHandle{2, 1}, {}, {1.0f, 1.0f, 1.0f, 1.0f});
    first.paintOrder = 2;
    second.paintOrder = 1;
    const std::vector<R::UIRenderItem> items{first, second};

    R::FramePacket packet;
    R::RenderExtractionScratch scratch;
    R::RenderExtractor{}.BuildInto(packet, 1, items, nullptr, {}, &scratch);
    CHECK(packet.instances.size() == 2);
    CHECK(packet.instances[0].transformRow0[2] == second.layout.resolvedPosition.x);
}

} // namespace

int main() {
    TestTextureSwitchBreaksBatch();
    TestCacheWithoutChangeRebuildsNothing();
    TestCacheRebuildsOnlyDirtyNode();
    TestCacheUpdatesSameSizeInPlace();
    TestCacheHandlesNodeSizeChange();
    TestBuildIntoReusesStorageAndClearsData();
    TestBuildIntoPreservesPaintOrder();

    std::printf("RenderList: %d checks, %d falhas\n", run, failed);
    return failed == 0 ? 0 : 1;
}
