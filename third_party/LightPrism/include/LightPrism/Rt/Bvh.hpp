#pragma once
// Ray tracing por software (Fase 5b): BVH de triângulos independente de API gráfica e de DXR/Vulkan RT.
//
// Escolhas (as mais rápidas e leves para uma biblioteca engine-agnostic):
//   - nós de 32 bytes, filhos ADJACENTES (um índice por nó interno), folhas com até 4 triângulos;
//   - dois construtores: SAH binned (melhor traversal; geometria estática) e LBVH de Karras com códigos de Morton
//     (construção O(n log n) paralelizável, ideal para geometria dinâmica);
//   - travessia ordenada (filho mais próximo primeiro), pilha de 64 entradas, poda por `tMax`;
//   - LIMITE DE DISTÂNCIA: `Ray::tMax` poda subárvores inteiras; para AO e sombras de contato custa uma fração de um raio infinito;
//   - Refit (recalcula caixas, mesma topologia) para malhas deformadas/movidas.
// O mesmo layout de nó vai para um StructuredBuffer na GPU (shaders/Rt/Bvh.hlsli).
#include <cstdint>
#include <vector>

#include "LightPrism/Math.hpp"

namespace LightPrism::Rt {

struct Triangle {
    Vec3 v0, v1, v2;
};

struct Aabb {
    Vec3 lo{1e30f, 1e30f, 1e30f};
    Vec3 hi{-1e30f, -1e30f, -1e30f};
    void Grow(const Vec3& p) {
        lo = {p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y, p.z < lo.z ? p.z : lo.z};
        hi = {p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y, p.z > hi.z ? p.z : hi.z};
    }
    void Grow(const Aabb& b) { if (b.Valid()) { Grow(b.lo); Grow(b.hi); } }  // caixa vazia não contamina
    bool Valid() const { return lo.x <= hi.x; }
    Vec3 Extent() const { return hi - lo; }
    float Area() const {
        if (!Valid()) return 0.0f;
        const Vec3 e = Extent();
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
    bool Overlaps(const Aabb& o) const {
        return lo.x <= o.hi.x && hi.x >= o.lo.x && lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z && hi.z >= o.lo.z;
    }
    Aabb Expanded(float r) const {
        Aabb b = *this;
        b.lo = b.lo - Vec3{r, r, r};
        b.hi = b.hi + Vec3{r, r, r};
        return b;
    }
};

// `maxDistance` do pedido do usuário vira tMax: nada além dele existe para este raio.
struct Ray {
    Vec3 origin;
    Vec3 dir;                 // não precisa ser unitária, mas t é em unidades de `dir`
    float tMin = 1e-4f;
    float tMax = 1e30f;
};

struct Hit {
    float t = 0.0f;
    float u = 0.0f, v = 0.0f; // coordenadas baricêntricas
    uint32_t tri = 0;         // índice no vetor ORIGINAL passado ao Build
};

struct alignas(16) BvhNode {  // 32 bytes (espelha struct BvhNode do HLSL)
    float bmin[3]; uint32_t leftOrFirst;  // interno: índice do filho esquerdo (o direito é +1); folha: primeiro triângulo
    float bmax[3]; uint32_t count;        // 0 = interno; >0 = folha com `count` triângulos
};
static_assert(sizeof(BvhNode) == 32, "BvhNode deve ter 32 bytes");

struct TraversalStats {
    uint64_t rays = 0, nodesVisited = 0, triTests = 0;
};

enum class BvhBuild : uint8_t { BinnedSah, Lbvh };

class Bvh {
public:
    void Build(const std::vector<Triangle>& tris, BvhBuild mode = BvhBuild::BinnedSah, uint32_t maxLeafSize = 4);
    // Recalcula as caixas com novas posições (MESMO número e ordem de triângulos do Build). Topologia não muda.
    void Refit(const std::vector<Triangle>& tris);

    bool Intersect(const Ray& r, Hit& hit, TraversalStats* st = nullptr) const;   // mais próximo dentro de [tMin, tMax]
    bool Occluded(const Ray& r, TraversalStats* st = nullptr) const;              // qualquer interseção (sombras/AO)

    bool Empty() const { return m_nodes.empty(); }
    const Aabb& Bounds() const { return m_bounds; }
    const std::vector<BvhNode>& Nodes() const { return m_nodes; }
    const std::vector<Triangle>& LeafOrderTriangles() const { return m_tris; }     // para subir à GPU (mesma ordem das folhas)
    const std::vector<uint32_t>& LeafOrderIndices() const { return m_triIndex; }   // posição de folha -> índice original
    uint32_t MaxDepth() const { return m_depth; }
    float SahCost() const;                                                         // custo SAH normalizado (menor = melhor)
    uint64_t MemoryBytes() const;

private:
    std::vector<BvhNode> m_nodes;
    std::vector<Triangle> m_tris;       // na ordem das folhas
    std::vector<uint32_t> m_triIndex;
    Aabb m_bounds;
    uint32_t m_depth = 0;
};

// Referência O(n) para testes.
bool IntersectBruteForce(const std::vector<Triangle>& tris, const Ray& r, Hit& hit);
bool IntersectTriangle(const Triangle& t, const Ray& r, float tMax, float& tHit, float& u, float& v);

} // namespace LightPrism::Rt
