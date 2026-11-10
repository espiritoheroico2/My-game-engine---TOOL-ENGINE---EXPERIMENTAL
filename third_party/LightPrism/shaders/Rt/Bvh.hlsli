// Travessia do BVH (espelha include/LightPrism/Rt/Bvh.hpp + src/Rt/Bvh.cpp). Funciona em compute/pixel shader SEM DXR:
// serve a GPUs sem ray tracing por hardware (mobile, low end). Com DXR, a engine pode trocar por TraceRay mantendo as
// funções de efeito de RtEffects.hlsli. NÃO COMPILADO (sem dxc no ambiente de desenvolvimento): validar na engine.
//
// Bindings esperados (a engine escolhe registradores):
//   StructuredBuffer<BvhNode> g_BvhNodes;   // Bvh::Nodes()            (32 B/nó)
//   StructuredBuffer<float3>  g_BvhVerts;   // Bvh::LeafOrderTriangles() reinterpretado: 3 float3 por triângulo (36 B)
#ifndef LP_RT_BVH_HLSLI
#define LP_RT_BVH_HLSLI

#define LP_BVH_STACK 64

struct BvhNode {
    float3 bmin; uint leftOrFirst;   // interno: filho esquerdo (direito = +1); folha: primeiro triângulo
    float3 bmax; uint count;         // 0 = interno
};

struct RtRay { float3 origin; float tMin; float3 dir; float tMax; };

StructuredBuffer<BvhNode> g_BvhNodes;
StructuredBuffer<float3>  g_BvhVerts;

// Distância de entrada na caixa, ou 1e38 se não há interseção em [tMin,tMax]. Comparações toleram NaN (0*inf), como no C++.
float BvhSlabNear(BvhNode n, float3 o, float3 inv, float tMin, float tMax)
{
    float3 ta = (n.bmin - o) * inv;
    float3 tb = (n.bmax - o) * inv;
    float3 lo = min(ta, tb);
    float3 hi = max(ta, tb) * 1.0000004f;
    float t0 = tMin, t1 = tMax;
    t0 = lo.x > t0 ? lo.x : t0;  t1 = hi.x < t1 ? hi.x : t1;
    t0 = lo.y > t0 ? lo.y : t0;  t1 = hi.y < t1 ? hi.y : t1;
    t0 = lo.z > t0 ? lo.z : t0;  t1 = hi.z < t1 ? hi.z : t1;
    return t0 <= t1 ? t0 : 1e38f;
}

float3 BvhInvDir(float3 d)
{
    return float3(d.x != 0.0f ? 1.0f / d.x : 1e30f, d.y != 0.0f ? 1.0f / d.y : 1e30f, d.z != 0.0f ? 1.0f / d.z : 1e30f);
}

// Möller–Trumbore dos dois lados. Devolve t em [tMin, tMax] ou -1.
float BvhTriangle(uint tri, RtRay r, float tMax)
{
    float3 v0 = g_BvhVerts[tri * 3 + 0], v1 = g_BvhVerts[tri * 3 + 1], v2 = g_BvhVerts[tri * 3 + 2];
    float3 e1 = v1 - v0, e2 = v2 - v0;
    float3 p = cross(r.dir, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-12f) return -1.0f;
    float inv = 1.0f / det;
    float3 s = r.origin - v0;
    float u = dot(s, p) * inv;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return -1.0f;
    float3 q = cross(s, e1);
    float v = dot(r.dir, q) * inv;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return -1.0f;
    float t = dot(e2, q) * inv;
    return (t < r.tMin || t > tMax) ? -1.0f : t;
}

// Interseção mais próxima dentro de [tMin, tMax] (tMax = LIMITE DE DISTÂNCIA: poda subárvores). Devolve t ou -1.
float BvhIntersect(RtRay r)
{
    float3 inv = BvhInvDir(r.dir);
    float tMax = r.tMax;
    float best = -1.0f;
    uint stack[LP_BVH_STACK]; float stackT[LP_BVH_STACK]; uint sp = 0;
    if (BvhSlabNear(g_BvhNodes[0], r.origin, inv, r.tMin, tMax) > tMax) return -1.0f;
    uint cur = 0;
    [loop] for (;;)
    {
        BvhNode n = g_BvhNodes[cur];
        bool popNext = true;
        if (n.count > 0)
        {
            for (uint i = 0; i < n.count; ++i)
            {
                float t = BvhTriangle(n.leftOrFirst + i, r, tMax);
                if (t >= 0.0f) { tMax = t; best = t; }
            }
        }
        else
        {
            uint c0 = n.leftOrFirst, c1 = c0 + 1;
            float ta = BvhSlabNear(g_BvhNodes[c0], r.origin, inv, r.tMin, tMax);
            float tb = BvhSlabNear(g_BvhNodes[c1], r.origin, inv, r.tMin, tMax);
            bool ha = ta <= tMax, hb = tb <= tMax;
            if (ha && hb)
            {
                bool aFirst = ta <= tb;
                stack[sp] = aFirst ? c1 : c0; stackT[sp] = aFirst ? tb : ta; ++sp;
                cur = aFirst ? c0 : c1; popNext = false;
            }
            else if (ha || hb) { cur = ha ? c0 : c1; popNext = false; }
        }
        if (popNext)
        {
            bool found = false;
            while (sp > 0 && !found) { --sp; if (stackT[sp] <= tMax) { cur = stack[sp]; found = true; } }
            if (!found) return best;
        }
    }
    return best;
}

// Qualquer interseção em [tMin, tMax] (sombras/AO binário): sai no primeiro acerto.
bool BvhOccluded(RtRay r)
{
    float3 inv = BvhInvDir(r.dir);
    uint stack[LP_BVH_STACK]; uint sp = 0;
    if (BvhSlabNear(g_BvhNodes[0], r.origin, inv, r.tMin, r.tMax) > r.tMax) return false;
    uint cur = 0;
    [loop] for (;;)
    {
        BvhNode n = g_BvhNodes[cur];
        bool popNext = true;
        if (n.count > 0)
        {
            for (uint i = 0; i < n.count; ++i)
                if (BvhTriangle(n.leftOrFirst + i, r, r.tMax) >= 0.0f) return true;
        }
        else
        {
            uint c0 = n.leftOrFirst, c1 = c0 + 1;
            float ta = BvhSlabNear(g_BvhNodes[c0], r.origin, inv, r.tMin, r.tMax);
            float tb = BvhSlabNear(g_BvhNodes[c1], r.origin, inv, r.tMin, r.tMax);
            bool ha = ta <= r.tMax, hb = tb <= r.tMax;
            if (ha && hb)
            {
                bool aFirst = ta <= tb;
                stack[sp++] = aFirst ? c1 : c0;
                cur = aFirst ? c0 : c1; popNext = false;
            }
            else if (ha || hb) { cur = ha ? c0 : c1; popNext = false; }
        }
        if (popNext)
        {
            if (sp == 0) return false;
            cur = stack[--sp];
        }
    }
    return false;
}

#endif
