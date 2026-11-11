#include "SpriteUi/Config/UIConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t MaxInputBytes = 1024 * 1024;
    if (size > MaxInputBytes || (data == nullptr && size != 0)) return 0;

    const char* input = size == 0 ? "" : reinterpret_cast<const char*>(data);
    ToolEngine::SpriteUi::Config::UISettingsConfig staged;
    if (!ToolEngine::SpriteUi::Config::UIConfigManager::ValidateAndStage(
            std::string_view(input, size), staged)) {
        return 0;
    }

    if (!staged.IsValid()) std::abort();
    const std::string serialized =
        ToolEngine::SpriteUi::Config::UIConfigManager::Serialize(staged);
    ToolEngine::SpriteUi::Config::UISettingsConfig reparsed;
    if (!ToolEngine::SpriteUi::Config::UIConfigManager::ValidateAndStage(
            serialized, reparsed) ||
        ToolEngine::SpriteUi::Config::UIConfigManager::Serialize(reparsed) != serialized) {
        std::abort();
    }
    return 0;
}
