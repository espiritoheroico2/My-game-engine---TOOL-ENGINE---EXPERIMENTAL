// Newtoon Physics - BVH (arvore de volumes envolventes) para broad-phase e raycast. Reconstruida a cada passo (O(n log n)),
// vetor contiguo de nos, sem ponteiros. A mesma arvore pode servir ao render (culling de luzes) - ver docs/PHYSICS.md.
#pragma once
#include "newtoon_types.hpp"

#include <algorithm>
#include <vector>

namespace Newtoon {

struct BVHNode { AABB bounds; int left = -1, right = -1; Entity entity = kNoEntity; bool IsLeaf() const { return left < 0; } };

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

class BVH {
 public:
  std::vector<BVHNode> nodes;

  // `boxes[i]` = AABB da entidade i; `valid[i]` = 0 para ignorar. Reaproveita a memoria entre chamadas.
  void Build(const std::vector<AABB>& boxes, const std::vector<char>& valid) {
    nodes.clear(); ids_.clear();
    for (size_t i = 0; i < boxes.size(); ++i) if (valid[i]) ids_.push_back(static_cast<Entity>(i));
    if (ids_.empty()) return;
    nodes.reserve(ids_.size() * 2);
    BuildRange(boxes, 0, ids_.size());
  }
  bool Empty() const { return nodes.empty(); }

  template <class F> // f(entity) para cada folha cuja caixa toca `q`
  void Query(const AABB& q, F&& f) const {
    if (nodes.empty()) return;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp) {
      const BVHNode& n = nodes[static_cast<size_t>(stack[--sp])];
      if (!Overlaps(n.bounds, q)) continue;
      if (n.IsLeaf()) f(n.entity); else if (sp < 62) { stack[sp++] = n.left; stack[sp++] = n.right; }
    }
  }
  // Percorre folhas que o raio pode tocar (ordem arbitraria); `visit(entity, maxT&)` pode reduzir maxT ao achar um acerto.
  template <class F>
  void RayQuery(const Vec3& o, const Vec3& d, float& maxT, F&& visit) const {
    if (nodes.empty()) return;
    const Vec3 inv{std::fabs(d.x) > 1e-12f ? 1.f / d.x : 1e12f, std::fabs(d.y) > 1e-12f ? 1.f / d.y : 1e12f, std::fabs(d.z) > 1e-12f ? 1.f / d.z : 1e12f};
    int stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp) {
      const BVHNode& n = nodes[static_cast<size_t>(stack[--sp])];
      if (RayAABB(o, inv, n.bounds, maxT) < 0.f) continue;
      if (n.IsLeaf()) visit(n.entity, maxT); else if (sp < 62) { stack[sp++] = n.left; stack[sp++] = n.right; }
    }
  }

 private:
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
};

} // namespace Newtoon
