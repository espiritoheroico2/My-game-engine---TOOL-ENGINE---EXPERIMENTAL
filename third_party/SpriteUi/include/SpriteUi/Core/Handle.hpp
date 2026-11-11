#pragma once

#include <cstdint>
#include <limits>

namespace ToolEngine::SpriteUi {

struct UIHandle {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return index != std::numeric_limits<std::uint32_t>::max() && generation != 0;
    }

    [[nodiscard]] constexpr std::uint64_t Pack() const noexcept {
        return (static_cast<std::uint64_t>(generation) << 32u) | index;
    }

    [[nodiscard]] static constexpr UIHandle Unpack(std::uint64_t value) noexcept {
        return {
            static_cast<std::uint32_t>(value & 0xFFFFFFFFull),
            static_cast<std::uint32_t>(value >> 32u)
        };
    }

    friend constexpr bool operator==(UIHandle a, UIHandle b) noexcept {
        return a.index == b.index && a.generation == b.generation;
    }
    friend constexpr bool operator!=(UIHandle a, UIHandle b) noexcept {
        return !(a == b);
    }
};

inline constexpr UIHandle InvalidUIHandle{};

} // namespace ToolEngine::SpriteUi
