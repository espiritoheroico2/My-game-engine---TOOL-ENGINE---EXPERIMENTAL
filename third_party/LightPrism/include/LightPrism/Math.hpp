#pragma once
// Matemática mínima, sem dependências. Layouts de 12/16 bytes compatíveis com HLSL.
#include <algorithm>
#include <cmath>

namespace LightPrism {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
};

constexpr float kPi = 3.14159265358979323846f;

constexpr float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& v) noexcept { return std::sqrt(Dot(v, v)); }
inline Vec3 Normalize(const Vec3& v) noexcept {
    const float len = Length(v);
    return len > 1e-8f ? v * (1.0f / len) : Vec3{0.0f, -1.0f, 0.0f};
}
constexpr float Saturate(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
constexpr float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, float t) noexcept { return a + (b - a) * t; }
constexpr float MaxComponent(const Vec3& v) noexcept { return v.x > v.y ? (v.x > v.z ? v.x : v.z) : (v.y > v.z ? v.y : v.z); }

} // namespace LightPrism
