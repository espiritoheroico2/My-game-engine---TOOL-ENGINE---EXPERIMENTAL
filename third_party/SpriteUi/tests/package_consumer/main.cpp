#include <SpriteUi/SpriteUi.hpp>

#include <vector>

#if defined(SPRITEUI_PACKAGE_HAS_SDL3)
#include <SpriteUi/Platform/SDL3InputAdapter.hpp>
#endif

int main() {
    ToolEngine::SpriteUi::Rendering::RenderExtractor extractor;
    const std::vector<ToolEngine::SpriteUi::Rendering::UIRenderItem> items;
    const auto packet = extractor.Build(1, items);
    if (!ToolEngine::SpriteUi::Rendering::IsFramePacketValid(packet)) return 1;
#if defined(SPRITEUI_PACKAGE_HAS_SDL3)
    ToolEngine::SpriteUi::Platform::SDL3InputAdapter adapter(nullptr);
    if (adapter.IsValid()) return 1;
#endif
    return 0;
}
