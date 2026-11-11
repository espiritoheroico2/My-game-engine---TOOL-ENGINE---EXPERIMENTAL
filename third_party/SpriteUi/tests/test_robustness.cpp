#include "SpriteUi/SpriteUi.hpp"

#include "TestHarness.hpp"
#include "TestSuites.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace SpriteUiTests {

namespace {

using namespace ToolEngine::SpriteUi;
namespace Cfg = ToolEngine::SpriteUi::Config;
namespace Rn = ToolEngine::SpriteUi::Rendering;

Cfg::UISettingsConfig SampleConfig() {
    Cfg::UISettingsConfig c;
    c.globalScale = 1.25f;
    c.activeTheme = 2;
    c.enableVSync = false;
    for (int i = 0; i < 3; ++i) {
        Cfg::UIWindowConfig w;
        w.name = "Janela \"" + std::to_string(i) + "\" \\ \xC3\xA7";
        w.offsetMin = {10.0f * static_cast<float>(i), 5.0f};
        w.offsetMax = {200.0f + static_cast<float>(i), 150.0f};
        w.isMinimized = (i == 1);
        c.windows.push_back(w);
    }
    return c;
}

bool SameConfig(const Cfg::UISettingsConfig& a, const Cfg::UISettingsConfig& b) {
    if (a.schemaVersion != b.schemaVersion || a.globalScale != b.globalScale || a.activeTheme != b.activeTheme ||
        a.enableVSync != b.enableVSync || a.windows.size() != b.windows.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.windows.size(); ++i) {
        const auto& x = a.windows[i];
        const auto& y = b.windows[i];
        if (x.name != y.name || x.isMinimized != y.isMinimized || x.offsetMin.x != y.offsetMin.x ||
            x.offsetMin.y != y.offsetMin.y || x.offsetMax.x != y.offsetMax.x || x.offsetMax.y != y.offsetMax.y) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ JSON
void TestConfigRoundTripIsStable() {
    const Cfg::UISettingsConfig original = SampleConfig();
    const std::string json = Cfg::UIConfigManager::Serialize(original);
    Cfg::UISettingsConfig parsed;
    CHECK(Cfg::UIConfigManager::ValidateAndStage(json, parsed));
    CHECK(SameConfig(original, parsed));
    CHECK(Cfg::UIConfigManager::Serialize(parsed) == json); // idempotent text
}

void TestConfigFuzzNeverCrashesAndStaysAtomic() {
    const std::string base = Cfg::UIConfigManager::Serialize(SampleConfig());
    std::srand(60606);
    bool atomicOnFailure = true;
    bool validOnSuccess = true;
    bool stableOnSuccess = true;
    int accepted = 0;
    int rejected = 0;

    Cfg::UISettingsConfig sentinel;
    sentinel.globalScale = 3.75f;
    sentinel.activeTheme = 41;
    sentinel.windows.push_back({"sentinel", {1.0f, 2.0f}, {3.0f, 4.0f}, true});

    for (int it = 0; it < 6000; ++it) {
        std::string text = base;
        const int mutations = 1 + std::rand() % 5;
        for (int m = 0; m < mutations && !text.empty(); ++m) {
            const std::size_t pos = static_cast<std::size_t>(std::rand()) % text.size();
            switch (std::rand() % 6) {
            case 0: text[pos] = static_cast<char>(std::rand() % 256); break;
            case 1: text.erase(pos, static_cast<std::size_t>(std::rand() % 12)); break;
            case 2: text.insert(pos, 1, "{}[]:,\"\\-+.eE0123456789tfn"[std::rand() % 27]); break;
            case 3: text.insert(pos, text.substr(pos, static_cast<std::size_t>(std::rand() % 40))); break;
            case 4: text.resize(pos); break;
            default: text.insert(pos, "1e999"); break;
            }
        }
        Cfg::UISettingsConfig staged = sentinel;
        const bool ok = Cfg::UIConfigManager::ValidateAndStage(text, staged);
        if (ok) {
            ++accepted;
            validOnSuccess = validOnSuccess && staged.IsValid();
            for (const auto& w : staged.windows) {
                validOnSuccess = validOnSuccess && std::isfinite(w.offsetMin.x) && std::isfinite(w.offsetMin.y) &&
                                 std::isfinite(w.offsetMax.x) && std::isfinite(w.offsetMax.y);
            }
            // What was accepted serializes and parses back to the same value.
            const std::string again = Cfg::UIConfigManager::Serialize(staged);
            Cfg::UISettingsConfig reparsed;
            stableOnSuccess = stableOnSuccess && Cfg::UIConfigManager::ValidateAndStage(again, reparsed) && SameConfig(staged, reparsed);
        } else {
            ++rejected;
            atomicOnFailure = atomicOnFailure && SameConfig(staged, sentinel); // a failed stage touches nothing
        }
    }
    CHECK(accepted > 0);
    CHECK(rejected > 0);
    CHECK(atomicOnFailure);
    CHECK(validOnSuccess);
    CHECK(stableOnSuccess);
}

void TestConfigGarbageAndHostileDocuments() {
    std::srand(2024);
    bool atomic = true;
    Cfg::UISettingsConfig sentinel;
    sentinel.activeTheme = 9;
    for (int it = 0; it < 2000; ++it) {
        std::string text(static_cast<std::size_t>(std::rand() % 200), '\0');
        for (char& ch : text) ch = static_cast<char>(std::rand() % 256);
        Cfg::UISettingsConfig staged = sentinel;
        if (!Cfg::UIConfigManager::ValidateAndStage(text, staged)) atomic = atomic && SameConfig(staged, sentinel);
    }
    CHECK(atomic);

    Cfg::UISettingsConfig out = sentinel;
    // Pathological nesting must be rejected, not overflow the stack.
    CHECK(!Cfg::UIConfigManager::ValidateAndStage(std::string(300000, '['), out));
    CHECK(!Cfg::UIConfigManager::ValidateAndStage(std::string(300000, '{'), out));
    std::string nested;
    for (int i = 0; i < 50000; ++i) nested += "{\"a\":";
    CHECK(!Cfg::UIConfigManager::ValidateAndStage(nested, out));
    // Wrong schema, wrong types, numbers out of range, NUL bytes and lone surrogates.
    CHECK(!Cfg::UIConfigManager::ValidateAndStage("{\"schemaVersion\":999}", out));
    CHECK(!Cfg::UIConfigManager::ValidateAndStage("{\"schemaVersion\":\"1\"}", out));
    constexpr char nulJson[] = "{\"schemaVersion\":1,\"x\":\"a\0b\"}";
    CHECK(!Cfg::UIConfigManager::ValidateAndStage(
        std::string_view(nulJson, sizeof(nulJson) - 1), out));
    CHECK(SameConfig(out, sentinel));
    CHECK(!Cfg::UIConfigManager::ValidateAndStage(
        "{\"schemaVersion\":1,\"globalScale\":1e999}", out));
    CHECK(SameConfig(out, sentinel));

    Cfg::UISettingsConfig target;
    Cfg::UISettingsConfig staged = SampleConfig();
    Cfg::UIConfigManager::Commit(target, staged);
    CHECK(SameConfig(target, staged));
}

// ------------------------------------------------------------------ clip hierarchy
void TestClipHierarchyWithHostileParents() {
    std::srand(8080);
    bool survived = true;
    bool finiteWhenValid = true;
    const float nan = std::nanf("");
    for (int it = 0; it < 3000; ++it) {
        const std::size_t count = 1 + static_cast<std::size_t>(std::rand() % 12);
        std::vector<Rn::ClipNode> nodes(count);
        for (std::size_t i = 0; i < count; ++i) {
            const float x = static_cast<float>(std::rand() % 300), y = static_cast<float>(std::rand() % 300);
            nodes[i].rect = {x, y, x + static_cast<float>(std::rand() % 200), y + static_cast<float>(std::rand() % 200)};
            switch (std::rand() % 6) {
            case 0: nodes[i].parentIndex = Rn::NoClipIndex; break;
            case 1: nodes[i].parentIndex = static_cast<std::uint32_t>(i); break;                    // itself
            case 2: nodes[i].parentIndex = static_cast<std::uint32_t>(count + std::rand() % 5); break; // out of range
            case 3: nodes[i].parentIndex = static_cast<std::uint32_t>(std::rand() % count); break;     // maybe forward / cyclic
            default: nodes[i].parentIndex = i > 0 ? static_cast<std::uint32_t>(i - 1) : Rn::NoClipIndex; break;
            }
            if (std::rand() % 8 == 0) nodes[i].rect.minX = nan;
            if (std::rand() % 8 == 0) nodes[i].enabled = false;
        }
        const Rn::ClipHierarchyResult result = Rn::BuildClipHierarchy(nodes);
        survived = survived && result.nodeClipIndices.size() <= count + 1;
        if (result.valid) {
            for (const Rn::ClipRectGPU& r : result.effectiveRects) {
                finiteWhenValid = finiteWhenValid && std::isfinite(r.minX) && std::isfinite(r.minY) &&
                                  std::isfinite(r.maxX) && std::isfinite(r.maxY);
            }
        }
    }
    CHECK(survived);
    CHECK(finiteWhenValid);
}

// ------------------------------------------------------------------ extractor
void TestExtractorWithCorruptedItems() {
    std::srand(1212);
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    bool packetAlwaysValid = true;
    bool accounted = true;
    for (int it = 0; it < 1500; ++it) {
        std::vector<Rn::UIRenderItem> items;
        const int count = std::rand() % 40;
        for (int i = 0; i < count; ++i) {
            Rn::UIRenderItem item;
            item.handle = (std::rand() % 6 == 0) ? UIHandle{} : UIHandle{static_cast<std::uint32_t>(i), 1};
            item.layout = {{static_cast<float>(std::rand() % 400), static_cast<float>(std::rand() % 400)},
                           {static_cast<float>(std::rand() % 120), static_cast<float>(std::rand() % 120)}, std::rand() % 8 != 0, false};
            item.paintOrder = static_cast<std::uint64_t>(std::rand() % 20);
            item.layer = std::rand() % 3 - 1;
            item.textureSlice = static_cast<std::uint32_t>(std::rand() % 4);
            item.tintPremultipliedLinear = {0.2f, 0.3f, 0.4f, 0.5f};
            switch (std::rand() % 9) {
            case 0: item.layout.resolvedSize.x = nan; break;
            case 1: item.layout.resolvedPosition.y = inf; break;
            case 2: item.tintPremultipliedLinear.w = nan; break;
            case 3: item.tintPremultipliedLinear = {0.9f, 0.9f, 0.9f, 0.1f}; break; // not premultiplied
            case 4: item.cornerRadius = nan; break;
            case 5: item.layout.resolvedSize.y = -10.0f; break;
            case 6: item.clipIndex = static_cast<std::uint32_t>(std::rand() % 6); break; // maybe out of range
            default: break;
            }
            items.push_back(item);
        }
        Rn::ExtractionStats stats;
        const Rn::FramePacket packet = Rn::RenderExtractor{}.Build(1, items, &stats);
        packetAlwaysValid = packetAlwaysValid && Rn::IsFramePacketValid(packet);
        accounted = accounted &&
                    stats.submittedItems + stats.skippedInvisible + stats.skippedInvalid == static_cast<std::uint32_t>(items.size());
    }
    CHECK(packetAlwaysValid);
    CHECK(accounted); // every item is either drawn or counted as skipped: nothing disappears silently
}

} // namespace

void RunRobustnessTests() {
    TestConfigRoundTripIsStable();
    TestConfigFuzzNeverCrashesAndStaysAtomic();
    TestConfigGarbageAndHostileDocuments();
    TestClipHierarchyWithHostileParents();
    TestExtractorWithCorruptedItems();
}

} // namespace SpriteUiTests
