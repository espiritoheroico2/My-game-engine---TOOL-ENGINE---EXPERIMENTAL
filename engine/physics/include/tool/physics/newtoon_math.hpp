// Newtoon Physics (c) 2026 Joao Vitor Dias da Silva - MIT. Repositorio original: github.com/espiritoheroico2/Newtoon-Physics-for-engine
// Integrado ao ToolEngine com autorizacao do autor. Esta versao completa o esqueleto original (colisoes, solver, raycast, eventos).
//
// Matematica minima: Vec3, Quat, Mat3. Tudo inline, sem alocacao, alinhado a 16 bytes com SIMD SSE2 opcional.
#pragma once
#include <cmath>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NEWTOON_SIMD_SSE2 1
#include <immintrin.h>
#endif

namespace Newtoon {

// Vetor 3D em float (16 bytes alinhados, 4o lane de padding para loads/stores SIMD de 128 bits).
struct alignas(16) Vec3 {
  float x = 0, y = 0, z = 0, _pad = 0.f;
  // Comprimento do vetor.
  float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
  // Quadrado do comprimento (mais barato que Magnitude).
  float SqrMagnitude() const { return x * x + y * y + z * z; }
  // Soma de vetores.
  Vec3 operator+(const Vec3& o) const {
#ifdef NEWTOON_SIMD_SSE2
    Vec3 r;
    _mm_storeu_ps(&r.x, _mm_add_ps(_mm_loadu_ps(&x), _mm_loadu_ps(&o.x)));
    r._pad = 0.f;
    return r;
#else
    return {x + o.x, y + o.y, z + o.z};
#endif
  }
  // Subtracao de vetores.
  Vec3 operator-(const Vec3& o) const {
#ifdef NEWTOON_SIMD_SSE2
    Vec3 r;
    _mm_storeu_ps(&r.x, _mm_sub_ps(_mm_loadu_ps(&x), _mm_loadu_ps(&o.x)));
    r._pad = 0.f;
    return r;
#else
    return {x - o.x, y - o.y, z - o.z};
#endif
  }
  // Vetor oposto.
  Vec3 operator-() const { return {-x, -y, -z}; }
  // Multiplicacao por escalar.
  Vec3 operator*(float s) const {
#ifdef NEWTOON_SIMD_SSE2
    Vec3 r;
    _mm_storeu_ps(&r.x, _mm_mul_ps(_mm_loadu_ps(&x), _mm_set1_ps(s)));
    r._pad = 0.f;
    return r;
#else
    return {x * s, y * s, z * s};
#endif
  }
  // Soma no proprio vetor.
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  // Subtracao no proprio vetor.
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  // Multiplicacao por escalar no proprio vetor.
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
// Multiplicacao escalar * vetor.
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
// Produto escalar entre a e b.
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Produto vetorial a x b (perpendicular aos dois).
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
#ifdef NEWTOON_SIMD_SSE2
  const __m128 va = _mm_loadu_ps(&a.x);
  const __m128 vb = _mm_loadu_ps(&b.x);
  const __m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3, 0, 2, 1));
  const __m128 b_yzx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3, 0, 2, 1));
  const __m128 c = _mm_sub_ps(_mm_mul_ps(va, b_yzx), _mm_mul_ps(a_yzx, vb));
  Vec3 r;
  _mm_storeu_ps(&r.x, _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1)));
  r._pad = 0.f;
  return r;
#else
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
#endif
}
// Interpolacao linear entre a e b (t em [0, 1]).
inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
// Vetor unitario na mesma direcao de v. Devolve (0,0,0) se v tem comprimento quase zero.
inline Vec3 Normalized(const Vec3& v) { const float m = v.Magnitude(); return m > 1e-12f ? v * (1.f / m) : Vec3{0, 0, 0}; }
// Componente a componente, o menor valor entre a e b.
inline Vec3 Min(const Vec3& a, const Vec3& b) {
#ifdef NEWTOON_SIMD_SSE2
  Vec3 r;
  _mm_storeu_ps(&r.x, _mm_min_ps(_mm_loadu_ps(&a.x), _mm_loadu_ps(&b.x)));
  r._pad = 0.f;
  return r;
#else
  return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
#endif
}
// Componente a componente, o maior valor entre a e b.
inline Vec3 Max(const Vec3& a, const Vec3& b) {
#ifdef NEWTOON_SIMD_SSE2
  Vec3 r;
  _mm_storeu_ps(&r.x, _mm_max_ps(_mm_loadu_ps(&a.x), _mm_loadu_ps(&b.x)));
  r._pad = 0.f;
  return r;
#else
  return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
#endif
}
// Componente i do vetor (0 = x, 1 = y, 2 = z).
inline float At(const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// Vetor 2D em float: evita o 3o eixo (Z) para simulacoes 2D mais leves.
struct Vec2 {
  float x = 0, y = 0;
  float Magnitude() const { return std::sqrt(x * x + y * y); }
  float SqrMagnitude() const { return x * x + y * y; }
  Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
  Vec2 operator-() const { return {-x, -y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
  Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};
inline Vec2 operator*(float s, const Vec2& v) { return v * s; }
inline float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
// Produto vetorial 2D (escalar no eixo Z): a.x * b.y - a.y * b.x.
inline float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
// Produto vetorial 2D entre vetor e escalar angular (w x r = (-w*ry, w*rx)).
inline Vec2 Cross(float s, const Vec2& v) { return {-s * v.y, s * v.x}; }
inline Vec2 Cross(const Vec2& v, float s) { return {s * v.y, -s * v.x}; }
inline Vec2 Perp(const Vec2& v) { return {-v.y, v.x}; }
inline Vec2 Normalized(const Vec2& v) { const float m = v.Magnitude(); return m > 1e-12f ? v * (1.f / m) : Vec2{0, 0}; }
inline Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
inline Vec2 Min(const Vec2& a, const Vec2& b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y}; }
inline Vec2 Max(const Vec2& a, const Vec2& b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y}; }

// Rotacao 2D pre-calculada (cos, sin): evita chamar std::cos/std::sin repetidamente no solver/narrow-phase.
struct Rot2 {
  float c = 1.f, s = 0.f;
  static Rot2 FromAngle(float rad) { return {std::cos(rad), std::sin(rad)}; }
  float Angle() const { return std::atan2(s, c); }
  Vec2 Rotate(const Vec2& v) const { return {c * v.x - s * v.y, s * v.x + c * v.y}; }
  Vec2 InvRotate(const Vec2& v) const { return {c * v.x + s * v.y, -s * v.x + c * v.y}; }
  Rot2 operator*(const Rot2& o) const { return {c * o.c - s * o.s, s * o.c + c * o.s}; }
  static float LerpAngle(float a, float b, float t) {
    float diff = std::fmod(b - a, 6.28318530718f);
    if (diff > 3.14159265359f) diff -= 6.28318530718f;
    if (diff < -3.14159265359f) diff += 6.28318530718f;
    return a + diff * t;
  }
};

// Matriz 3x3 (linhas). Usada para rotacoes e para o tensor de inercia no mundo.
struct Mat3 {
  Vec3 r[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  // Aplica a matriz ao vetor v.
  Vec3 operator*(const Vec3& v) const { return {Dot(r[0], v), Dot(r[1], v), Dot(r[2], v)}; }
  // Coluna c da matriz.
  Vec3 Col(int c) const { return {At(r[0], c), At(r[1], c), At(r[2], c)}; }
  // Matriz transposta.
  Mat3 Transposed() const { Mat3 m; for (int i = 0; i < 3; ++i) m.r[i] = Col(i); return m; }
};

// Quaternion (x, y, z, w) usado para rotacoes. Use Normalized() antes de guardar rotacoes.
struct alignas(16) Quat {
  float x = 0, y = 0, z = 0, w = 1;
  // Quaternion de rotacao de rad radianos em torno do eixo axis.
  static Quat FromAxisAngle(Vec3 axis, float rad) { const Vec3 a = ::Newtoon::Normalized(axis); const float s = std::sin(rad * 0.5f); return {a.x * s, a.y * s, a.z * s, std::cos(rad * 0.5f)}; }
  // Composicao de rotacoes: aplica o (o) e depois este (produto de Hamilton).
  Quat operator*(const Quat& o) const {
    return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
  }
  // Quaternion unitario de mesmo sentido. Devolve identidade se o quaternion for quase nulo.
  Quat Normalized() const { const float m = std::sqrt(x * x + y * y + z * z + w * w); return m > 1e-12f ? Quat{x / m, y / m, z / m, w / m} : Quat{}; }
  // Converte o quaternion (unitario) em matriz de rotacao 3x3.
  Mat3 ToMat3() const {
    const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    Mat3 m;
    m.r[0] = {1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)};
    m.r[1] = {2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)};
    m.r[2] = {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)};
    return m;
  }
  // Gira o vetor v pelo quaternion (rotacao unitaria).
  Vec3 Rotate(const Vec3& v) const { return ToMat3() * v; }
  // Interpolacao linear normalizada (NLERP) com menor caminho (shortest path), ideal para render interpolation.
  static Quat Nlerp(const Quat& a, const Quat& b, float t) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.f ? -1.f : 1.f;
    return Quat{
        a.x + (b.x * s - a.x) * t,
        a.y + (b.y * s - a.y) * t,
        a.z + (b.z * s - a.z) * t,
        a.w + (b.w * s - a.w) * t,
    }.Normalized();
  }
  // Interpolacao esferica (SLERP) com menor caminho.
  static Quat Slerp(const Quat& a, const Quat& b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = 1.f;
    if (dot < 0.f) { dot = -dot; s = -1.f; }
    if (dot > 0.9995f) return Nlerp(a, b, t);
    const float theta = std::acos(dot > 1.f ? 1.f : dot);
    const float sinTheta = std::sin(theta);
    const float w1 = std::sin((1.f - t) * theta) / sinTheta;
    const float w2 = std::sin(t * theta) / sinTheta * s;
    return {w1 * a.x + w2 * b.x, w1 * a.y + w2 * b.y, w1 * a.z + w2 * b.z, w1 * a.w + w2 * b.w};
  }
};

} // namespace Newtoon
