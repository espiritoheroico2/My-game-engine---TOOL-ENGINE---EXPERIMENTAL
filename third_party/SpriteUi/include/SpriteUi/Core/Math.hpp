#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ToolEngine::SpriteUi {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xValue, float yValue) : x{xValue}, y{yValue} {}

    constexpr Vec2 operator+(Vec2 rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(Vec2 rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(float scalar) const noexcept { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(float scalar) const noexcept { return {x / scalar, y / scalar}; }
    constexpr Vec2& operator+=(Vec2 rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(Vec2 rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue)
        : x{xValue}, y{yValue}, z{zValue}, w{wValue} {}

    constexpr Vec4 operator+(Vec4 rhs) const noexcept {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }
    constexpr Vec4 operator-(Vec4 rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }
    constexpr Vec4 operator*(float scalar) const noexcept {
        return {x * scalar, y * scalar, z * scalar, w * scalar};
    }
};

struct Rect {
    Vec2 position{};
    Vec2 size{};

    [[nodiscard]] constexpr Vec2 Max() const noexcept { return position + size; }

    // Half-open bounds prevent two adjacent controls from both owning the same edge.
    [[nodiscard]] bool Contains(Vec2 point) const noexcept {
        if (size.x <= 0.0f || size.y <= 0.0f) return false;
        return point.x >= position.x && point.y >= position.y &&
               point.x < position.x + size.x && point.y < position.y + size.y;
    }
};

[[nodiscard]] inline bool IsFinite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] inline bool IsFinite(Vec2 value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] inline bool IsFinite(Vec4 value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] constexpr float Dot(Vec2 a, Vec2 b) noexcept {
    return a.x * b.x + a.y * b.y;
}

[[nodiscard]] constexpr float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] inline Vec2 Lerp(Vec2 a, Vec2 b, float t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] inline Vec4 Lerp(Vec4 a, Vec4 b, float t) noexcept {
    return a + (b - a) * t;
}

// Affine 2D transform:
// x' = m00*x + m01*y + tx
// y' = m10*x + m11*y + ty
struct Affine2D {
    float m00 = 1.0f;
    float m01 = 0.0f;
    float tx = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float ty = 0.0f;

    [[nodiscard]] bool IsFinite() const noexcept {
        return ToolEngine::SpriteUi::IsFinite(m00) &&
               ToolEngine::SpriteUi::IsFinite(m01) &&
               ToolEngine::SpriteUi::IsFinite(tx) &&
               ToolEngine::SpriteUi::IsFinite(m10) &&
               ToolEngine::SpriteUi::IsFinite(m11) &&
               ToolEngine::SpriteUi::IsFinite(ty);
    }

    [[nodiscard]] Vec2 TransformPoint(Vec2 point) const noexcept {
        return {
            m00 * point.x + m01 * point.y + tx,
            m10 * point.x + m11 * point.y + ty
        };
    }

    [[nodiscard]] std::optional<Affine2D> Inverse(float epsilon = 1.0e-8f) const noexcept {
        if (!IsFinite() || !ToolEngine::SpriteUi::IsFinite(epsilon) || epsilon < 0.0f) {
            return std::nullopt;
        }
        const float determinant = m00 * m11 - m01 * m10;
        if (!ToolEngine::SpriteUi::IsFinite(determinant) || std::abs(determinant) <= epsilon) {
            return std::nullopt;
        }

        const float inverseDet = 1.0f / determinant;
        Affine2D result;
        result.m00 =  m11 * inverseDet;
        result.m01 = -m01 * inverseDet;
        result.m10 = -m10 * inverseDet;
        result.m11 =  m00 * inverseDet;
        result.tx = -(result.m00 * tx + result.m01 * ty);
        result.ty = -(result.m10 * tx + result.m11 * ty);
        return result;
    }
};

} // namespace ToolEngine::SpriteUi
