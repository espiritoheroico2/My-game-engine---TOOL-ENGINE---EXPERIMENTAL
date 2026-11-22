// ToolEngine - matematica 3D minima: Vec3, Mat4 (coluna-maior, mao direita, profundidade 0..1),
// AABB e frustum. Sem dependencias.
#pragma once
#include "tool/core/types.hpp"

#include <algorithm>
#include <cmath>

namespace tool {

// Vetor 3D em float, usado para posicoes, direcoes e cores.
struct Vec3 {
  f32 x = 0, y = 0, z = 0;
  // Soma componente a componente.
  constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  // Subtracao componente a componente.
  constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  // Multiplica cada componente por s.
  constexpr Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
};
// Produto escalar.
constexpr f32 dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Produto vetorial (perpendicular a a e b).
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// Comprimento do vetor.
inline f32 length(Vec3 a) { return std::sqrt(dot(a, a)); }
// Vetor unitario na mesma direcao. Devolve (0,0,0) se o vetor for quase nulo.
inline Vec3 normalize(Vec3 a) { const f32 l = length(a); return l > 1e-12f ? a * (1.f / l) : Vec3{}; }

// Vetor 4D em float (usado nas transformacoes de Mat4).
struct Vec4 { f32 x = 0, y = 0, z = 0, w = 0; };

// m[coluna][linha]
struct Mat4 {
  f32 m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

  // Produto de matrizes: aplica b e depois a.
  friend Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        f32 s = 0;
        for (int k = 0; k < 4; ++k) s += a.m[k][row] * b.m[c][k];
        r.m[c][row] = s;
      }
    return r;
  }
  // Multiplica a matriz pelo vetor v de 4 componentes.
  Vec4 mul(Vec4 v) const {
    return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
            m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
            m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
            m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
  }
  // Transforma um ponto (w = 1) e faz a divisao perspectiva.
  Vec3 transformPoint(Vec3 p) const { Vec4 r = mul({p.x, p.y, p.z, 1}); return {r.x / r.w, r.y / r.w, r.z / r.w}; }

  // Matriz de translacao por t.
  static Mat4 translation(Vec3 t) { Mat4 r; r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z; return r; }
  // Matriz de rotacao em torno do eixo X, em radianos.
  static Mat4 rotationX(f32 a) { Mat4 r; const f32 c = std::cos(a), s = std::sin(a); r.m[1][1] = c; r.m[2][1] = -s; r.m[1][2] = s; r.m[2][2] = c; return r; }
  // Matriz de rotacao em torno do eixo Y, em radianos.
  static Mat4 rotationY(f32 a) { Mat4 r; const f32 c = std::cos(a), s = std::sin(a); r.m[0][0] = c; r.m[2][0] = s; r.m[0][2] = -s; r.m[2][2] = c; return r; }
  // Matriz de rotacao em torno do eixo Z, em radianos.
  static Mat4 rotationZ(f32 a) { Mat4 r; const f32 c = std::cos(a), s = std::sin(a); r.m[0][0] = c; r.m[1][0] = -s; r.m[0][1] = s; r.m[1][1] = c; return r; }
  // Matriz de escala por s.
  static Mat4 scale(Vec3 s) { Mat4 r; r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z; return r; }

  // Perspectiva mao direita (camera olha para -Z), z de clip em 0..1 (D3D/Vulkan/WebGPU).
  static Mat4 perspective(f32 fovY, f32 aspect, f32 zn, f32 zf) {
    const f32 f = 1.f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.m[0][0] = f / aspect; r.m[1][1] = f;
    r.m[2][2] = zf / (zn - zf); r.m[2][3] = -1.f;
    r.m[3][2] = zn * zf / (zn - zf); r.m[3][3] = 0.f;
    return r;
  }
  // Projecao ortografica com volume (l, rgt, b, t, zn, zf).
  static Mat4 ortho(f32 l, f32 rgt, f32 b, f32 t, f32 zn, f32 zf) {
    Mat4 r;
    r.m[0][0] = 2.f / (rgt - l); r.m[1][1] = 2.f / (t - b); r.m[2][2] = 1.f / (zn - zf);
    r.m[3][0] = -(rgt + l) / (rgt - l); r.m[3][1] = -(t + b) / (t - b); r.m[3][2] = zn / (zn - zf);
    return r;
  }
  // Matriz de camera que olha de eye para target, com 'up' como referencia vertical.
  static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
    r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
    r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
    r.m[3][0] = -dot(s, eye); r.m[3][1] = -dot(u, eye); r.m[3][2] = dot(f, eye);
    return r;
  }
};

// Caixa alinhada aos eixos. Comeca vazia (min > max) e cresce com grow().
struct AABB {
  Vec3 min{1e30f, 1e30f, 1e30f}, max{-1e30f, -1e30f, -1e30f};
  // Expande a caixa para incluir o ponto p.
  void grow(Vec3 p) {
    min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
    max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
  }
};

// Plano n·x + d = 0 (normal n normalizada). dist() > 0 indica o lado da normal.
struct Plane { Vec3 n; f32 d = 0; f32 dist(Vec3 p) const { return dot(n, p) + d; } };

// Frustum de 6 planos (Gribb-Hartmann) para clip z em 0..1.
struct Frustum {
  Plane p[6];
  // Extrai os 6 planos do frustum a partir de uma matriz view-projection.
  static Frustum fromViewProj(const Mat4& vp) {
    auto row = [&](int r) { return Vec4{vp.m[0][r], vp.m[1][r], vp.m[2][r], vp.m[3][r]}; };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto mk = [](Vec4 a, Vec4 b, bool add) {
      Vec4 v = add ? Vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w} : Vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
      const f32 l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
      return Plane{{v.x / l, v.y / l, v.z / l}, v.w / l};
    };
    Frustum f;
    f.p[0] = mk(r3, r0, true);  // esquerda
    f.p[1] = mk(r3, r0, false); // direita
    f.p[2] = mk(r3, r1, true);  // baixo
    f.p[3] = mk(r3, r1, false); // cima
    { // perto (z >= 0): linha 2 sozinha
      const f32 l = std::sqrt(r2.x * r2.x + r2.y * r2.y + r2.z * r2.z);
      f.p[4] = Plane{{r2.x / l, r2.y / l, r2.z / l}, r2.w / l};
    }
    f.p[5] = mk(r3, r2, false); // longe
    return f;
  }
  // True se a esfera de centro c e raio r toca o frustum (pode dar falso positivo nas bordas).
  bool intersectsSphere(Vec3 c, f32 r) const {
    for (const Plane& pl : p)
      if (pl.dist(c) < -r) return false;
    return true;
  }
  // True se a caixa toca o frustum (pode dar falso positivo nas bordas).
  bool intersectsAABB(const AABB& b) const {
    for (const Plane& pl : p) {
      const Vec3 v{pl.n.x >= 0 ? b.max.x : b.min.x, pl.n.y >= 0 ? b.max.y : b.min.y, pl.n.z >= 0 ? b.max.z : b.min.z};
      if (pl.dist(v) < 0) return false;
    }
    return true;
  }
};

} // namespace tool
