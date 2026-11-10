#pragma once
// Mat4: armazenamento row-major, convenção de vetor-coluna (v' = M * v), mão direita, Y para cima.
// Profundidade [0,1] (D3D/Vulkan/Metal). Projeção em perspectiva usa Z reverso (near=1, far=0).
// No HLSL: declare `row_major float4x4` e use mul(M, v).
#include <cmath>

#include "LightPrism/Math.hpp"

namespace LightPrism {

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static constexpr Mat4 Identity() { return Mat4{}; }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }

    Mat4 Transposed() const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r.m[i][j] = m[j][i];
        return r;
    }

    // Aplica com w=1 e divide por w (se w != 0).
    Vec3 TransformPoint(const Vec3& p) const {
        const float x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
        const float y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
        const float z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3];
        const float w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
        return (std::fabs(w) > 1e-12f && w != 1.0f) ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
    }

    Vec3 TransformDirection(const Vec3& d) const {
        return {m[0][0] * d.x + m[0][1] * d.y + m[0][2] * d.z,
                m[1][0] * d.x + m[1][1] * d.y + m[1][2] * d.z,
                m[2][0] * d.x + m[2][1] * d.y + m[2][2] * d.z};
    }
};

// Câmera olhando de `eye` para `target`. Se `up` for quase paralelo à direção, escolhe outro eixo.
inline Mat4 LookAtRH(const Vec3& eye, const Vec3& target, Vec3 up = {0, 1, 0}) {
    const Vec3 f = Normalize(target - eye);
    if (std::fabs(Dot(f, Normalize(up))) > 0.999f) up = std::fabs(f.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 s = Normalize(Cross(f, up));
    const Vec3 u = Cross(s, f);
    Mat4 r;
    r.m[0][0] = s.x;  r.m[0][1] = s.y;  r.m[0][2] = s.z;  r.m[0][3] = -Dot(s, eye);
    r.m[1][0] = u.x;  r.m[1][1] = u.y;  r.m[1][2] = u.z;  r.m[1][3] = -Dot(u, eye);
    r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = Dot(f, eye);
    return r;
}

// Perspectiva RH, depth [0,1], Z reverso: z=-near -> 1, z=-far -> 0.
inline Mat4 PerspectiveRH_ReverseZ(float fovYRad, float aspect, float zNear, float zFar) {
    const float t = 1.0f / std::tan(fovYRad * 0.5f);
    Mat4 r;
    r.m[0][0] = t / aspect;
    r.m[1][1] = t;
    r.m[2][2] = zNear / (zFar - zNear);
    r.m[2][3] = zNear * zFar / (zFar - zNear);
    r.m[3][2] = -1.0f;
    r.m[3][3] = 0.0f;
    return r;
}

// Ortográfica RH, depth [0,1] convencional: z=-zNear -> 0, z=-zFar -> 1.
// (Para reverso, passe zNear e zFar trocados.)
inline Mat4 OrthographicRH(float l, float r_, float b, float t, float zNear, float zFar) {
    Mat4 r;
    r.m[0][0] = 2.0f / (r_ - l);  r.m[0][3] = -(r_ + l) / (r_ - l);
    r.m[1][1] = 2.0f / (t - b);   r.m[1][3] = -(t + b) / (t - b);
    r.m[2][2] = 1.0f / (zNear - zFar);
    r.m[2][3] = zNear / (zNear - zFar);
    return r;
}

// Inversa de uma transformação afim (última linha 0,0,0,1): rotação/escala/translação/ortográfica.
// Pré-condição: bloco 3x3 invertível (determinante != 0).
inline Mat4 InverseAffine(const Mat4& a) {
    const float a00 = a.m[0][0], a01 = a.m[0][1], a02 = a.m[0][2];
    const float a10 = a.m[1][0], a11 = a.m[1][1], a12 = a.m[1][2];
    const float a20 = a.m[2][0], a21 = a.m[2][1], a22 = a.m[2][2];
    const float c00 = a11 * a22 - a12 * a21, c01 = a02 * a21 - a01 * a22, c02 = a01 * a12 - a02 * a11;
    const float c10 = a12 * a20 - a10 * a22, c11 = a00 * a22 - a02 * a20, c12 = a02 * a10 - a00 * a12;
    const float c20 = a10 * a21 - a11 * a20, c21 = a01 * a20 - a00 * a21, c22 = a00 * a11 - a01 * a10;
    const float inv = 1.0f / (a00 * c00 + a01 * c10 + a02 * c20);
    Mat4 r;
    r.m[0][0] = c00 * inv; r.m[0][1] = c01 * inv; r.m[0][2] = c02 * inv;
    r.m[1][0] = c10 * inv; r.m[1][1] = c11 * inv; r.m[1][2] = c12 * inv;
    r.m[2][0] = c20 * inv; r.m[2][1] = c21 * inv; r.m[2][2] = c22 * inv;
    const float tx = a.m[0][3], ty = a.m[1][3], tz = a.m[2][3];
    r.m[0][3] = -(r.m[0][0] * tx + r.m[0][1] * ty + r.m[0][2] * tz);
    r.m[1][3] = -(r.m[1][0] * tx + r.m[1][1] * ty + r.m[1][2] * tz);
    r.m[2][3] = -(r.m[2][0] * tx + r.m[2][1] * ty + r.m[2][2] * tz);
    return r;
}

} // namespace LightPrism
