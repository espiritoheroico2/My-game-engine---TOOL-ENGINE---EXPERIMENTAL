#pragma once

#include <cstdint>

namespace ToolEngine::SpriteUi {

enum class UIDirtyBits : std::uint32_t {
    None         = 0,
    Measure      = 1u << 0,
    Arrange      = 1u << 1,
    Transform    = 1u << 2,
    HitTargets   = 1u << 3,
    TextShape    = 1u << 4,
    TextAtlas    = 1u << 5,
    DrawData     = 1u << 6,
    Blur         = 1u << 7
};

// Nomes normativos do documento V4.2 (secao 6): DirtyNone, DirtyMeasure, ...
// O tipo e um enum class para evitar conversoes implicitas; estas constantes
// preservam o vocabulario do documento sem poluir o namespace com enumeradores soltos.
inline constexpr UIDirtyBits DirtyNone       = UIDirtyBits::None;
inline constexpr UIDirtyBits DirtyMeasure    = UIDirtyBits::Measure;
inline constexpr UIDirtyBits DirtyArrange    = UIDirtyBits::Arrange;
inline constexpr UIDirtyBits DirtyTransform  = UIDirtyBits::Transform;
inline constexpr UIDirtyBits DirtyHitTargets = UIDirtyBits::HitTargets;
inline constexpr UIDirtyBits DirtyTextShape  = UIDirtyBits::TextShape;
inline constexpr UIDirtyBits DirtyTextAtlas  = UIDirtyBits::TextAtlas;
inline constexpr UIDirtyBits DirtyDrawData   = UIDirtyBits::DrawData;
inline constexpr UIDirtyBits DirtyBlur       = UIDirtyBits::Blur;

[[nodiscard]] constexpr UIDirtyBits operator|(UIDirtyBits a, UIDirtyBits b) noexcept {
    return static_cast<UIDirtyBits>(static_cast<std::uint32_t>(a) |
                                    static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr UIDirtyBits operator&(UIDirtyBits a, UIDirtyBits b) noexcept {
    return static_cast<UIDirtyBits>(static_cast<std::uint32_t>(a) &
                                    static_cast<std::uint32_t>(b));
}

struct UIDirtyState {
    std::uint32_t bits = static_cast<std::uint32_t>(UIDirtyBits::Measure) |
                         static_cast<std::uint32_t>(UIDirtyBits::Arrange) |
                         static_cast<std::uint32_t>(UIDirtyBits::Transform) |
                         static_cast<std::uint32_t>(UIDirtyBits::HitTargets) |
                         static_cast<std::uint32_t>(UIDirtyBits::DrawData);

    void Mark(UIDirtyBits value) noexcept {
        bits |= static_cast<std::uint32_t>(value);
    }

    void Clear(UIDirtyBits value) noexcept {
        bits &= ~static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] bool Has(UIDirtyBits value) const noexcept {
        return (bits & static_cast<std::uint32_t>(value)) != 0;
    }

    [[nodiscard]] std::uint32_t Consume() noexcept {
        const std::uint32_t pending = bits;
        bits = 0;
        return pending;
    }
};

} // namespace ToolEngine::SpriteUi
