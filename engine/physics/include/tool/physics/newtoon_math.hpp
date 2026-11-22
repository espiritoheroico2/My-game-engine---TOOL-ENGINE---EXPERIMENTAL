// Newtoon Physics (c) 2026 Joao Vitor Dias da Silva - MIT. Repositorio original: github.com/espiritoheroico2/Newtoon-Physics-for-engine
// Integrado ao ToolEngine com autorizacao do autor. Esta versao completa o esqueleto original (colisoes, solver, raycast, eventos).
//
// Matematica minima: Vec3, Quat, Mat3. Tudo inline, sem alocacao, alinhado a 16 bytes onde ajuda o SIMD do compilador.
#pragma once
#include <cmath>

namespace Newtoon {

// Vetor 3D em float (16 bytes alinhados). Usado para posicao, velocidade e direcao.
struct alignas(16) Vec3 {
  float x = 0, y = 0, z = 0;
  // Comprimento do vetor.
  float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
  // Quadrado do comprimento (mais barato que Magnitude).
  float SqrMagnitude() const { return x * x + y * y + z * z; }
  // Soma de vetores.
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  // Subtracao de vetores.
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  // Vetor oposto.
  Vec3 operator-() const { return {-x, -y, -z}; }
  // Multiplicacao por escalar.
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
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
inline Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// Vetor unitario na mesma direcao de v. Devolve (0,0,0) se v tem comprimento quase zero.
inline Vec3 Normalized(const Vec3& v) { const float m = v.Magnitude(); return m > 1e-12f ? v * (1.f / m) : Vec3{0, 0, 0}; }
// Componente a componente, o menor valor entre a e b.
inline Vec3 Min(const Vec3& a, const Vec3& b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z}; }
// Componente a componente, o maior valor entre a e b.
inline Vec3 Max(const Vec3& a, const Vec3& b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z}; }
// Componente i do vetor (0 = x, 1 = y, 2 = z).
inline float At(const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// Vetor 2D em float.
struct alignas(16) Vec2 {
  float x = 0, y = 0;
  // Comprimento do vetor.
  float Magnitude() const { return std::sqrt(x * x + y * y); }
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
};

} // namespace Newtoon
