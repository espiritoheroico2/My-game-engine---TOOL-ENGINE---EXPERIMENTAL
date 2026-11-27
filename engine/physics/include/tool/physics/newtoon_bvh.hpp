// Newtoon Physics - BVH (arvore de volumes envolventes) para broad-phase e raycast. Atualizada por refit linear e
// reconstruida periodicamente para recuperar a qualidade espacial,
// vetor contiguo de nos, sem ponteiros. A mesma arvore pode servir ao render (culling de luzes) - ver docs/planos/10_FISICA.md.
#pragma once
#include "newtoon_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NEWTOON_BVH_SSE2 1
#include <immintrin.h>
#endif

namespace Newtoon {

// No da BVH: caixa envolvente, filhos (ou a entidade, se for folha). Folha quando left < 0.
struct BVHNode { AABB bounds; int left = -1, right = -1; Entity entity = kNoEntity; bool IsLeaf() const { return left < 0; } };

// AABB utilizavel na arvore: todos os componentes finitos (sem NaN/Inf) e min <= max em cada eixo.
// Uma caixa com NaN propagaria NaN para os volumes pais e corromperia a arvore inteira, por isso e descartada.
inline bool IsUsableBox(const AABB& b) {
  const float comps[6] = {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
  for (float f : comps) {
    if (!std::isfinite(f)) return false;
  }
  return b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z;
}

// True se as duas caixas se tocam (inclusive na borda).
inline bool Overlaps(const AABB& a, const AABB& b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}
// Raio x AABB (lajes). Retorna a distancia de entrada (>= 0) ou -1.
inline float RayAABB(const Vec3& o, const Vec3& invD, const AABB& b, float maxT) {
  float t0 = 0.f, t1 = maxT;
  for (int i = 0; i < 3; ++i) {
    float a = (At(b.min, i) - At(o, i)) * At(invD, i), c = (At(b.max, i) - At(o, i)) * At(invD, i);
    if (a > c) std::swap(a, c);
    t0 = std::max(t0, a); t1 = std::min(t1, c); if (t0 > t1) return -1.f;
  }
  return t0;
}

// Arvore de volumes envolventes para consultas de broad-phase e raycast. Reconstruida a cada passo com Build().
class BVH {
 public:
  // Entradas da pilha de percurso. Invariante GARANTIDA por Build(), em runtime (nao por assert):
  // a arvore e dividida pela mediana, entao a profundidade e ceil(log2 n) e a pilha usa no maximo
  // profundidade + 1 entradas. Build() recusa qualquer entrada que quebraria esse limite.
  static constexpr int kStack = 128;
  // 2n - 1 nos precisam caber em int (indices de no sao int). 2^30 entidades ja geram mais de 2 bilhoes de nos.
  static constexpr size_t kMaxEntities = size_t{1} << 30;
  std::vector<BVHNode> nodes;

  // `boxes[i]` = AABB da entidade i; `valid[i]` = 0 para ignorar. Reaproveita a memoria entre chamadas.
  // Depois de Build(), Refit() pode atualizar apenas os bounds sem reordenar a topologia.
  // Contrato (F-05):
  //   - valid.size() != boxes.size() lanca std::invalid_argument (antes lia valid[i] fora do limite);
  //   - mais de kMaxEntities entidades validas lanca std::length_error;
  //   - AABB com NaN/Inf ou min > max NAO entra na arvore: e descartada e contada em RejectedCount().
  //     Nao lanca, para que um corpo com transformacao invalida nao derrube a simulacao inteira.
  void Build(const std::vector<AABB>& boxes, const std::vector<char>& valid) {
    if (valid.size() != boxes.size()) throw std::invalid_argument("BVH::Build: valid e boxes devem ter o mesmo tamanho");
    nodes.clear(); ids_.clear(); rejected_ = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (!valid[i]) continue;
      if (!IsUsableBox(boxes[i])) { ++rejected_; continue; }
      ids_.push_back(static_cast<Entity>(i));
    }
    if (ids_.empty()) return;
    if (ids_.size() > kMaxEntities) { ids_.clear(); throw std::length_error("BVH::Build: entidades demais para indices int"); }
    // Verificacao da pilha ANTES de montar a arvore: profundidade = ceil(log2 n) e pilha <= profundidade + 1.
    size_t depth = 0;
    while ((size_t{1} << depth) < ids_.size()) ++depth;
    if (depth + 1 > static_cast<size_t>(kStack)) { ids_.clear(); throw std::length_error("BVH::Build: profundidade excede a pilha de percurso"); }
    nodes.reserve(ids_.size() * 2);
    BuildRange(boxes, 0, ids_.size());
  }
  // Atualiza os bounds da arvore mantendo a topologia criada por Build(). Retorna false se a topologia
  // deixou de corresponder as entradas (por exemplo, quantidade ou validade mudou).
  bool Refit(const std::vector<AABB>& boxes, const std::vector<char>& valid) {
    if (valid.size() != boxes.size() || nodes.empty()) return false;
    rejected_ = 0;
    for (size_t i = nodes.size(); i-- > 0;) {
      BVHNode& node = nodes[i];
      if (node.IsLeaf()) {
        if (node.entity == kNoEntity || node.entity >= boxes.size() || !valid[node.entity] || !IsUsableBox(boxes[node.entity])) {
          ++rejected_;
          return false;
        }
        node.bounds = boxes[node.entity];
      } else {
        if (node.left < 0 || node.right < 0 || static_cast<size_t>(node.left) >= nodes.size() || static_cast<size_t>(node.right) >= nodes.size()) return false;
        node.bounds.min = Min(nodes[static_cast<size_t>(node.left)].bounds.min, nodes[static_cast<size_t>(node.right)].bounds.min);
        node.bounds.max = Max(nodes[static_cast<size_t>(node.left)].bounds.max, nodes[static_cast<size_t>(node.right)].bounds.max);
      }
    }
    return true;
  }
  // True se a BVH nao tem nos (nenhuma caixa valida na ultima Build).
  bool Empty() const { return nodes.empty(); }
  // Quantas caixas foram descartadas na ultima Build() por NaN/Inf ou min > max.
  size_t RejectedCount() const { return rejected_; }

  // Chama f(entity) para cada entidade cuja caixa toca a caixa q.
  template <class F> // f(entity) para cada folha cuja caixa toca `q`
  void Query(const AABB& q, F&& f) const {
    if (nodes.empty()) return;
    // Sem checagem no caminho quente: o limite da pilha ja foi verificado em Build() (ver kStack).
    int stack[kStack]; int sp = 0; stack[sp++] = 0;
    while (sp) {
      const BVHNode& n = nodes[static_cast<size_t>(stack[--sp])];
      if (!Overlaps(n.bounds, q)) continue;
      if (n.IsLeaf()) f(n.entity); else { stack[sp++] = n.left; stack[sp++] = n.right; }
    }
  }
  // Percorre folhas que o raio pode tocar (ordem arbitraria); `visit(entity, maxT&)` pode reduzir maxT ao achar um acerto.
  template <class F>
  void RayQuery(const Vec3& o, const Vec3& d, float& maxT, F&& visit) const {
    if (nodes.empty()) return;
    const Vec3 inv{std::fabs(d.x) > 1e-12f ? 1.f / d.x : 1e12f, std::fabs(d.y) > 1e-12f ? 1.f / d.y : 1e12f, std::fabs(d.z) > 1e-12f ? 1.f / d.z : 1e12f};
    int stack[kStack]; int sp = 0; stack[sp++] = 0; // limite garantido por Build(), ver kStack
    while (sp) {
      const BVHNode& n = nodes[static_cast<size_t>(stack[--sp])];
      if (RayAABB(o, inv, n.bounds, maxT) < 0.f) continue;
      if (n.IsLeaf()) visit(n.entity, maxT); else { stack[sp++] = n.left; stack[sp++] = n.right; }
    }
  }

 private:
  // Constroi recursivamente a subarvore com as entidades ids_[b..e) e devolve o indice do no criado.
  int BuildRange(const std::vector<AABB>& boxes, size_t b, size_t e) {
    const int idx = static_cast<int>(nodes.size()); nodes.emplace_back();
    AABB bb = boxes[ids_[b]];
    for (size_t i = b + 1; i < e; ++i) { bb.min = Min(bb.min, boxes[ids_[i]].min); bb.max = Max(bb.max, boxes[ids_[i]].max); }
    nodes[static_cast<size_t>(idx)].bounds = bb;
    if (e - b == 1) { nodes[static_cast<size_t>(idx)].entity = ids_[b]; return idx; }
    const Vec3 ext = bb.max - bb.min; const int axis = ext.x > ext.y && ext.x > ext.z ? 0 : (ext.y > ext.z ? 1 : 2);
    const size_t mid = (b + e) / 2;
    std::nth_element(ids_.begin() + static_cast<long>(b), ids_.begin() + static_cast<long>(mid), ids_.begin() + static_cast<long>(e), [&](Entity x, Entity y) {
      return At(boxes[x].min, axis) + At(boxes[x].max, axis) < At(boxes[y].min, axis) + At(boxes[y].max, axis);
    });
    const int l = BuildRange(boxes, b, mid), r = BuildRange(boxes, mid, e);
    nodes[static_cast<size_t>(idx)].left = l; nodes[static_cast<size_t>(idx)].right = r;
    return idx;
  }
  std::vector<Entity> ids_;
  size_t rejected_ = 0;
};

// ------------------------------------------------------------------ Morton Codes + BVH4 (4-way SIMD BVH)
// Expande 10 bits para 30 bits intercalados (Z-order curve) para construir LBVH com alta localidade de cache.
inline uint32_t ExpandBits10(uint32_t v) noexcept {
  v = (v * 0x00010001u) & 0xFF0000FFu;
  v = (v * 0x00000101u) & 0x0F00F00Fu;
  v = (v * 0x00000011u) & 0xC30C30C3u;
  v = (v * 0x00000005u) & 0x49249249u;
  return v;
}

inline uint32_t MortonCode3D(const Vec3& p, const Vec3& worldMin, const Vec3& invExtent) noexcept {
  const float nx = std::clamp((p.x - worldMin.x) * invExtent.x, 0.f, 1.f);
  const float ny = std::clamp((p.y - worldMin.y) * invExtent.y, 0.f, 1.f);
  const float nz = std::clamp((p.z - worldMin.z) * invExtent.z, 0.f, 1.f);
  const uint32_t xx = ExpandBits10(static_cast<uint32_t>(std::min(nx * 1023.f, 1023.f)));
  const uint32_t yy = ExpandBits10(static_cast<uint32_t>(std::min(ny * 1023.f, 1023.f)));
  const uint32_t zz = ExpandBits10(static_cast<uint32_t>(std::min(nz * 1023.f, 1023.f)));
  return (xx << 2) | (yy << 1) | zz;
}

// No SoA de 4 filhos para BVH4: testa 4 AABBs simultaneamente via SIMD (inspirado na BroadPhaseQuadTree do Jolt).
struct alignas(16) BVH4Node {
  static constexpr int32_t kEmpty = 0x7FFFFFFF;
  float minX[4]{}, minY[4]{}, minZ[4]{};
  float maxX[4]{}, maxY[4]{}, maxZ[4]{};
  int32_t child[4]{kEmpty, kEmpty, kEmpty, kEmpty}; // >= 0: no interno; < 0: folha (~entity); kEmpty: vazio
};

class BVH4 {
 public:
  static constexpr int kStack = 128;
  static constexpr size_t kMaxEntities = size_t{1} << 30;
  std::vector<BVH4Node> nodes;

  void Build(const std::vector<AABB>& boxes, const std::vector<char>& valid) {
    if (valid.size() != boxes.size()) throw std::invalid_argument("BVH4::Build: valid e boxes devem ter o mesmo tamanho");
    nodes.clear();
    mortonItems_.clear();
    rejected_ = 0;

    Vec3 wMin{1e30f, 1e30f, 1e30f}, wMax{-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (!valid[i]) continue;
      if (!IsUsableBox(boxes[i])) { ++rejected_; continue; }
      wMin = Min(wMin, boxes[i].min);
      wMax = Max(wMax, boxes[i].max);
      mortonItems_.push_back({0, static_cast<Entity>(i)});
    }
    if (mortonItems_.empty()) return;
    if (mortonItems_.size() > kMaxEntities) { mortonItems_.clear(); throw std::length_error("BVH4::Build: entidades demais"); }

    const Vec3 ext = wMax - wMin;
    const Vec3 invExt{ext.x > 1e-6f ? 1.f / ext.x : 0.f, ext.y > 1e-6f ? 1.f / ext.y : 0.f, ext.z > 1e-6f ? 1.f / ext.z : 0.f};
    for (auto& item : mortonItems_) {
      const AABB& b = boxes[item.entity];
      const Vec3 c = (b.min + b.max) * 0.5f;
      item.key = (static_cast<uint64_t>(MortonCode3D(c, wMin, invExt)) << 32) | item.entity;
    }
    std::sort(mortonItems_.begin(), mortonItems_.end(), [](const MortonItem& a, const MortonItem& b) { return a.key < b.key; });

    nodes.reserve(mortonItems_.size() / 2 + 4);
    BuildRange(boxes, 0, mortonItems_.size());
  }

  bool Refit(const std::vector<AABB>& boxes, const std::vector<char>& valid) {
    if (valid.size() != boxes.size() || nodes.empty()) return false;
    rejected_ = 0;
    nodeBounds_.resize(nodes.size());
    for (size_t i = nodes.size(); i-- > 0;) {
      BVH4Node& n = nodes[i];
      AABB total{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
      bool hasAny = false;
      for (int k = 0; k < 4; ++k) {
        const int32_t ch = n.child[k];
        if (ch == BVH4Node::kEmpty) continue;
        AABB cb;
        if (ch < 0) {
          const Entity e = static_cast<Entity>(~ch);
          if (e >= boxes.size() || !valid[e] || !IsUsableBox(boxes[e])) { ++rejected_; return false; }
          cb = boxes[e];
        } else {
          if (static_cast<size_t>(ch) >= nodes.size()) return false;
          cb = nodeBounds_[static_cast<size_t>(ch)];
        }
        n.minX[k] = cb.min.x; n.minY[k] = cb.min.y; n.minZ[k] = cb.min.z;
        n.maxX[k] = cb.max.x; n.maxY[k] = cb.max.y; n.maxZ[k] = cb.max.z;
        total.min = Min(total.min, cb.min);
        total.max = Max(total.max, cb.max);
        hasAny = true;
      }
      if (!hasAny) return false;
      nodeBounds_[i] = total;
    }
    return true;
  }

  bool Empty() const { return nodes.empty(); }
  size_t RejectedCount() const { return rejected_; }

  template <class F>
  void Query(const AABB& q, F&& f) const {
    if (nodes.empty()) return;
    int32_t stack[kStack];
    int sp = 0;
    stack[sp++] = 0;
#ifdef NEWTOON_BVH_SSE2
    const __m128 qMinX = _mm_set1_ps(q.min.x), qMinY = _mm_set1_ps(q.min.y), qMinZ = _mm_set1_ps(q.min.z);
    const __m128 qMaxX = _mm_set1_ps(q.max.x), qMaxY = _mm_set1_ps(q.max.y), qMaxZ = _mm_set1_ps(q.max.z);
#endif
    while (sp > 0) {
      const BVH4Node& n = nodes[static_cast<size_t>(stack[--sp])];
#ifdef NEWTOON_BVH_SSE2
      const __m128 ox = _mm_and_ps(_mm_cmple_ps(_mm_loadu_ps(n.minX), qMaxX), _mm_cmpge_ps(_mm_loadu_ps(n.maxX), qMinX));
      const __m128 oy = _mm_and_ps(_mm_cmple_ps(_mm_loadu_ps(n.minY), qMaxY), _mm_cmpge_ps(_mm_loadu_ps(n.maxY), qMinY));
      const __m128 oz = _mm_and_ps(_mm_cmple_ps(_mm_loadu_ps(n.minZ), qMaxZ), _mm_cmpge_ps(_mm_loadu_ps(n.maxZ), qMinZ));
      int mask = _mm_movemask_ps(_mm_and_ps(_mm_and_ps(ox, oy), oz));
      while (mask) {
        const int k = CountTrailingZeros4(mask);
        mask &= (mask - 1);
        const int32_t ch = n.child[k];
        if (ch == BVH4Node::kEmpty) continue;
        if (ch < 0) f(static_cast<Entity>(~ch));
        else if (sp < kStack) stack[sp++] = ch;
      }
#else
      for (int k = 0; k < 4; ++k) {
        const int32_t ch = n.child[k];
        if (ch == BVH4Node::kEmpty) continue;
        if (n.minX[k] <= q.max.x && n.maxX[k] >= q.min.x &&
            n.minY[k] <= q.max.y && n.maxY[k] >= q.min.y &&
            n.minZ[k] <= q.max.z && n.maxZ[k] >= q.min.z) {
          if (ch < 0) f(static_cast<Entity>(~ch));
          else if (sp < kStack) stack[sp++] = ch;
        }
      }
#endif
    }
  }

  template <class F>
  void RayQuery(const Vec3& o, const Vec3& d, float& maxT, F&& visit) const {
    if (nodes.empty()) return;
    const Vec3 inv{std::fabs(d.x) > 1e-12f ? 1.f / d.x : 1e12f,
                   std::fabs(d.y) > 1e-12f ? 1.f / d.y : 1e12f,
                   std::fabs(d.z) > 1e-12f ? 1.f / d.z : 1e12f};
    int32_t stack[kStack];
    int sp = 0;
    stack[sp++] = 0;
#ifdef NEWTOON_BVH_SSE2
    const __m128 ox = _mm_set1_ps(o.x), oy = _mm_set1_ps(o.y), oz = _mm_set1_ps(o.z);
    const __m128 ix = _mm_set1_ps(inv.x), iy = _mm_set1_ps(inv.y), iz = _mm_set1_ps(inv.z);
    const __m128 zero = _mm_setzero_ps();
#endif
    while (sp > 0) {
      const BVH4Node& n = nodes[static_cast<size_t>(stack[--sp])];
#ifdef NEWTOON_BVH_SSE2
      const __m128 tMaxV = _mm_set1_ps(maxT);
      const __m128 x1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.minX), ox), ix);
      const __m128 x2 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.maxX), ox), ix);
      const __m128 y1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.minY), oy), iy);
      const __m128 y2 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.maxY), oy), iy);
      const __m128 z1 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.minZ), oz), iz);
      const __m128 z2 = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(n.maxZ), oz), iz);
      const __m128 t0 = _mm_max_ps(_mm_max_ps(zero, _mm_min_ps(x1, x2)), _mm_max_ps(_mm_min_ps(y1, y2), _mm_min_ps(z1, z2)));
      const __m128 t1 = _mm_min_ps(_mm_min_ps(tMaxV, _mm_max_ps(x1, x2)), _mm_min_ps(_mm_max_ps(y1, y2), _mm_max_ps(z1, z2)));
      int mask = _mm_movemask_ps(_mm_cmple_ps(t0, t1));
      while (mask) {
        const int k = CountTrailingZeros4(mask);
        mask &= (mask - 1);
        const int32_t ch = n.child[k];
        if (ch == BVH4Node::kEmpty) continue;
        if (ch < 0) visit(static_cast<Entity>(~ch), maxT);
        else if (sp < kStack) stack[sp++] = ch;
      }
#else
      for (int k = 0; k < 4; ++k) {
        const int32_t ch = n.child[k];
        if (ch == BVH4Node::kEmpty) continue;
        const AABB b{{n.minX[k], n.minY[k], n.minZ[k]}, {n.maxX[k], n.maxY[k], n.maxZ[k]}};
        if (RayAABB(o, inv, b, maxT) < 0.f) continue;
        if (ch < 0) visit(static_cast<Entity>(~ch), maxT);
        else if (sp < kStack) stack[sp++] = ch;
      }
#endif
    }
  }

 private:
  struct MortonItem { uint64_t key = 0; Entity entity = kNoEntity; };

  static int CountTrailingZeros4(int mask) noexcept {
    if (mask & 1) return 0;
    if (mask & 2) return 1;
    if (mask & 4) return 2;
    return 3;
  }

  static void SetChildBounds(BVH4Node& n, int slot, const AABB& b) noexcept {
    n.minX[slot] = b.min.x; n.minY[slot] = b.min.y; n.minZ[slot] = b.min.z;
    n.maxX[slot] = b.max.x; n.maxY[slot] = b.max.y; n.maxZ[slot] = b.max.z;
  }

  AABB BuildRange(const std::vector<AABB>& boxes, size_t b, size_t e) {
    const int32_t idx = static_cast<int32_t>(nodes.size());
    nodes.emplace_back();
    const size_t count = e - b;

    AABB total{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    if (count <= 4) {
      for (size_t k = 0; k < count; ++k) {
        const Entity ent = mortonItems_[b + k].entity;
        const AABB& cb = boxes[ent];
        SetChildBounds(nodes[static_cast<size_t>(idx)], static_cast<int>(k), cb);
        nodes[static_cast<size_t>(idx)].child[k] = ~static_cast<int32_t>(ent);
        total.min = Min(total.min, cb.min);
        total.max = Max(total.max, cb.max);
      }
      for (size_t k = count; k < 4; ++k) {
        nodes[static_cast<size_t>(idx)].minX[k] = 1e30f;
        nodes[static_cast<size_t>(idx)].maxX[k] = -1e30f;
      }
      return total;
    }

    // Divide o intervalo ordenado por Morton em 4 fatias contiguas
    const size_t cuts[5] = {
        b,
        b + count / 4,
        b + count / 2,
        b + (3 * count) / 4,
        e};
    for (int k = 0; k < 4; ++k) {
      const size_t cb = cuts[k], ce = cuts[k + 1];
      if (ce - cb == 1) {
        const Entity ent = mortonItems_[cb].entity;
        const AABB& box = boxes[ent];
        SetChildBounds(nodes[static_cast<size_t>(idx)], k, box);
        nodes[static_cast<size_t>(idx)].child[k] = ~static_cast<int32_t>(ent);
        total.min = Min(total.min, box.min);
        total.max = Max(total.max, box.max);
      } else {
        const int32_t childIdx = static_cast<int32_t>(nodes.size());
        const AABB box = BuildRange(boxes, cb, ce);
        SetChildBounds(nodes[static_cast<size_t>(idx)], k, box);
        nodes[static_cast<size_t>(idx)].child[k] = childIdx;
        total.min = Min(total.min, box.min);
        total.max = Max(total.max, box.max);
      }
    }
    return total;
  }

  std::vector<MortonItem> mortonItems_;
  std::vector<AABB> nodeBounds_;
  size_t rejected_ = 0;
};

} // namespace Newtoon
