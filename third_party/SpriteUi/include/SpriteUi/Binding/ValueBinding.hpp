#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace ToolEngine::SpriteUi::Binding {

// Reactive dirty-check binding for floating point values (Section 28 & Appendix F.3).
// Only triggers visual layout or property updates when value exceeds epsilon delta.
struct UIFloatBinding {
    UIHandle targetEntity{};
    float lastKnownValue = 0.0f;
    float threshold = 0.001f;
    bool initialized = false; // A primeira atualizacao valida sempre dispara.

    [[nodiscard]] bool Update(float currentValue) noexcept {
        if (!IsFinite(currentValue)) return false;
        if (!initialized || std::abs(currentValue - lastKnownValue) > threshold) {
            lastKnownValue = currentValue;
            initialized = true;
            return true;
        }
        return false;
    }
};

struct UIIntBinding {
    UIHandle targetEntity{};
    int lastKnownValue = 0;
    bool initialized = false;

    [[nodiscard]] bool Update(int currentValue) noexcept {
        if (!initialized || currentValue != lastKnownValue) {
            lastKnownValue = currentValue;
            initialized = true;
            return true;
        }
        return false;
    }
};

struct UIBoolBinding {
    UIHandle targetEntity{};
    bool lastKnownValue = false;
    bool initialized = false;

    [[nodiscard]] bool Update(bool currentValue) noexcept {
        if (!initialized || currentValue != lastKnownValue) {
            lastKnownValue = currentValue;
            initialized = true;
            return true;
        }
        return false;
    }
};

struct UIStringBinding {
    UIHandle targetEntity{};
    std::string lastKnownValue;
    bool initialized = false;

    [[nodiscard]] bool Update(std::string_view currentValue) {
        if (!initialized || currentValue != lastKnownValue) {
            lastKnownValue = currentValue;
            initialized = true;
            return true;
        }
        return false;
    }
};

} // namespace ToolEngine::SpriteUi::Binding

