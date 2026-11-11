#pragma once

#include "SpriteUi/Core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace ToolEngine::SpriteUi::Config {

inline constexpr std::uint32_t CurrentSchemaVersion = 1;

[[nodiscard]] inline float ReadFiniteFloat(
    float value,
    float defaultValue,
    float minValue = -std::numeric_limits<float>::infinity(),
    float maxValue = std::numeric_limits<float>::infinity()) noexcept
{
    if (!std::isfinite(value) || !std::isfinite(defaultValue)) {
        return defaultValue;
    }
    const float lo = std::isfinite(minValue) ? minValue : -std::numeric_limits<float>::max();
    const float hi = std::isfinite(maxValue) ? maxValue : std::numeric_limits<float>::max();
    if (lo > hi) return defaultValue;
    return std::clamp(value, lo, hi);
}

struct UIWindowConfig {
    std::string name;
    Vec2 offsetMin{0.0f, 0.0f};
    Vec2 offsetMax{150.0f, 100.0f};
    bool isMinimized = false;
};

struct UISettingsConfig {
    std::uint32_t schemaVersion = CurrentSchemaVersion;
    float globalScale = 1.0f;
    std::int32_t activeTheme = 0;
    bool enableVSync = true;
    std::vector<UIWindowConfig> windows;

    [[nodiscard]] bool IsValid() const noexcept {
        return schemaVersion == CurrentSchemaVersion &&
               std::isfinite(globalScale) && globalScale > 0.0f;
    }
};

class UIConfigManager {
public:
    // Atomic validation: verifies schemaVersion and cleanses all attributes.
    // If validation fails, outStaged is untouched and returns false.
    [[nodiscard]] static bool ValidateAndStage(
        std::string_view jsonText,
        UISettingsConfig& outStaged);

    // Commits staged changes transactionally into target config.
    static void Commit(UISettingsConfig& target, const UISettingsConfig& staged) noexcept;

    // Serializes config into valid JSON string with schemaVersion header.
    [[nodiscard]] static std::string Serialize(const UISettingsConfig& config);
};

} // namespace ToolEngine::SpriteUi::Config

