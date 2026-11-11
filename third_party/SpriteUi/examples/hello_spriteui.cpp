#include "SpriteUi/SpriteUi.hpp"

#include <iomanip>
#include <iostream>
#include <vector>

using namespace ToolEngine::SpriteUi;

int main() {
    using namespace ToolEngine::SpriteUi::Rendering;

    std::vector<UIRenderItem> items;

    UIRenderItem background;
    background.handle = {1, 1};
    background.layout = {{0.0f, 0.0f}, {1280.0f, 720.0f}, true, false};
    background.transform.scale = {1.0f, 1.0f};
    background.tintPremultipliedLinear = SrgbStraightToLinearPremultiplied(
        {0.055f, 0.075f, 0.11f, 1.0f});
    background.paintOrder = 0;
    background.textureSlice = WhiteTextureSlice;
    items.push_back(background);

    UIRenderItem panel;
    panel.handle = {2, 1};
    panel.layout = {{420.0f, 170.0f}, {440.0f, 380.0f}, true, false};
    panel.transform.pivot = {0.5f, 0.5f};
    panel.tintPremultipliedLinear = SrgbStraightToLinearPremultiplied(
        {0.12f, 0.17f, 0.24f, 1.0f});
    panel.materialIndex = 1;
    panel.paintOrder = 10;
    items.push_back(panel);

    UIRenderItem button;
    button.handle = {3, 1};
    button.layout = {{520.0f, 365.0f}, {240.0f, 54.0f}, true, false};
    button.transform.pivot = {0.5f, 0.5f};
    button.tintPremultipliedLinear = SrgbStraightToLinearPremultiplied(
        {0.12f, 0.55f, 0.95f, 1.0f});
    button.materialIndex = 2;
    button.paintOrder = 20;
    items.push_back(button);

    RenderExtractor extractor;
    ExtractionStats extractionStats{};
    const FramePacket frame = extractor.Build(1, items, &extractionStats);

    NullRenderBackend renderer;
    renderer.Submit(frame);

    std::cout << "SpriteUI MVP - frame " << frame.frameId << '\n';
    std::cout << "  Instancias: " << extractionStats.submittedItems << '\n';
    std::cout << "  Batches consecutivos: " << extractionStats.batchCount << '\n';
    std::cout << "  ABI SpriteInstanceGPU: " << sizeof(SpriteInstanceGPU) << " bytes\n";
    std::cout << "  Buffer de instancias: "
              << frame.instances.size() * sizeof(SpriteInstanceGPU) << " bytes\n";
    std::cout << "  Backend: NullRenderBackend (sem GPU real nesta etapa)\n";

    return 0;
}
