#include "LightPrism/Rt/ProbeGrid.hpp"

#include <cmath>

#include "LightPrism/Rt/Transfer.hpp"   // FibonacciSphere

namespace LightPrism::Rt {

namespace {
uint32_t Hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
float H01(uint32_t x) { return float(Hash(x) >> 8) / float(1u << 24); }

// Mapa octaédrico 4x4: direção <-> célula.
int OctaCell(const Vec3& d) {
    const float l1 = std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z);
    float u = d.x / l1, v = d.z / l1;
    if (d.y < 0.0f) { const float uu = (1.0f - std::fabs(v)) * (u >= 0 ? 1.0f : -1.0f), vv = (1.0f - std::fabs(u)) * (v >= 0 ? 1.0f : -1.0f); u = uu; v = vv; }
    const int cx = std::min(3, std::max(0, int((u * 0.5f + 0.5f) * 4.0f))), cy = std::min(3, std::max(0, int((v * 0.5f + 0.5f) * 4.0f)));
    return cx + 4 * cy;
}
Vec3 OctaDir(int cell) {
    float u = (float(cell % 4) + 0.5f) / 4.0f * 2.0f - 1.0f, v = (float(cell / 4) + 0.5f) / 4.0f * 2.0f - 1.0f;
    Vec3 d{u, 1.0f - std::fabs(u) - std::fabs(v), v};
    if (d.y < 0.0f) { const float uu = (1.0f - std::fabs(v)) * (u >= 0 ? 1.0f : -1.0f), vv = (1.0f - std::fabs(u)) * (v >= 0 ? 1.0f : -1.0f); d.x = uu; d.z = vv; }
    return Normalize(d);
}

Vec3 Rotate(const Vec3& v, const Vec3& axis, float c, float s) {   // Rodrigues
    return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1.0f - c));
}
}

void ProbeGrid::Init(const ProbeGridDesc& d) {
    d_ = d;
    probes_.assign(Count(), Probe{});
    valid_.assign(Count(), 0);
    dirs_ = FibonacciSphere(d.raysPerProbe);
    cursor_ = 0; frame_ = 0; stats_ = {};
    const float vd = 3.0f * d.spacing; for (Probe& p : probes_) for (int o = 0; o < kVisTexels; ++o) { p.m1[o] = vd; p.m2[o] = vd * vd; }
}

void ProbeGrid::UpdateOne(const Bvh& bvh, const ProbeEnvironment& env, uint32_t i) {
    const Vec3 pos = Position(i);
    // rotação aleatória por (probe, quadro): descorrelaciona o padrão para a histerese convergir
    const uint32_t seed = i * 9781u + frame_ * 6271u;
    const float z = 2.0f * H01(seed) - 1.0f, phi = 2.0f * kPi * H01(seed + 1), ang = 2.0f * kPi * H01(seed + 2);
    const float rxy = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const Vec3 axis{rxy * std::cos(phi), rxy * std::sin(phi), z};
    const float ca = std::cos(ang), sa = std::sin(ang);

    Ibl::Sh9 acc{};
    constexpr int T = kVisTexels;
    float sum1[T] = {}, sum2[T] = {}, wsum[T] = {};
    Vec3 texDir[T]; for (int t = 0; t < T; ++t) texDir[t] = OctaDir(t);
    const float visDist = 3.0f * d_.spacing;   // distâncias além disso não importam para a visibilidade de uma célula
    const float w = 4.0f * kPi / float(dirs_.size());
    for (const Vec3& d0 : dirs_) {
        const Vec3 dir = Rotate(d0, axis, ca, sa);
        Ray r; r.origin = pos; r.dir = dir; r.tMin = 1e-3f; r.tMax = d_.maxDistance;
        ++stats_.raysCast;
        Hit h; Vec3 L; float dist;
        if (bvh.Intersect(r, h)) {
            const Triangle& t = (*env.triangles)[h.tri];
            Vec3 N = Normalize(Cross(t.v1 - t.v0, t.v2 - t.v0));
            if (Dot(N, dir) > 0.0f) N = -N;
            const Vec3 hp = pos + dir * h.t;
            const Vec3 alb = env.albedo(h.tri);
            Vec3 E{};
            const float ndl = Dot(N, env.sunToLight);
            if (ndl > 0.0f) {
                Ray s; s.origin = hp + N * d_.normalBias; s.dir = env.sunToLight; s.tMax = d_.maxDistance;
                if (!bvh.Occluded(s)) E = env.sunIrradiance * (ndl / kPi);
            }
            if (env.multiBounce) E = E + Irradiance(hp + N * d_.normalBias, N, true);
            L = alb * E; dist = h.t;
        } else {
            L = env.sky(dir); dist = d_.maxDistance;
        }
        float y[9]; Ibl::ShBasis(dir, y);
        for (int k = 0; k < 9; ++k) acc.c[k] = acc.c[k] + L * (y[k] * w);
        const float dc = dist < visDist ? dist : visDist;
        for (int t = 0; t < T; ++t) {
            const float c = Dot(dir, texDir[t]); if (c <= 0.0f) continue;
            const float c2 = c * c, c4 = c2 * c2, c8 = c4 * c4, c16 = c8 * c8, wt = c16 * c16;   // cos^32
            wsum[t] += wt; sum1[t] += wt * dc; sum2[t] += wt * dc * dc;
        }
    }
    const Ibl::Sh9 conv = Ibl::ConvolveCosine(acc);
    Probe& p = probes_[i];
    const float hy = valid_[i] ? d_.hysteresis : 0.0f;
    for (int k = 0; k < 9; ++k) p.irr.c[k] = conv.c[k] * (1.0f - hy) + p.irr.c[k] * hy;
    for (int t = 0; t < T; ++t) if (wsum[t] > 1e-4f) {
        p.m1[t] = (sum1[t] / wsum[t]) * (1.0f - hy) + p.m1[t] * hy;
        p.m2[t] = (sum2[t] / wsum[t]) * (1.0f - hy) + p.m2[t] * hy;
    }
    valid_[i] = 1;
}

uint32_t ProbeGrid::Update(const Bvh& bvh, const ProbeEnvironment& env, uint32_t budget) {
    const uint32_t n = Count();
    const uint32_t todo = budget == 0 || budget > n ? n : budget;
    for (uint32_t k = 0; k < todo; ++k) UpdateOne(bvh, env, (cursor_ + k) % n);
    cursor_ = (cursor_ + todo) % n;
    if (cursor_ == 0 || todo == n) ++frame_; else if (todo < n && cursor_ < todo) ++frame_;   // novo ciclo => nova rotação
    stats_.updatedLast = todo;
    return todo;
}

uint32_t ProbeGrid::InvalidateRegion(const Aabb& changed) {
    const Aabb reach = changed.Expanded(d_.invalidateRadius > 0.0f ? d_.invalidateRadius : d_.maxDistance);
    uint32_t n = 0;
    for (uint32_t i = 0; i < Count(); ++i) {
        const Vec3 p = Position(i);
        if (p.x < reach.lo.x || p.x > reach.hi.x || p.y < reach.lo.y || p.y > reach.hi.y || p.z < reach.lo.z || p.z > reach.hi.z) continue;
        if (valid_[i]) { valid_[i] = 0; ++n; }
    }
    return n;
}

void ProbeGrid::InvalidateAll() { std::fill(valid_.begin(), valid_.end(), uint8_t(0)); }

Vec3 ProbeGrid::Irradiance(const Vec3& p, const Vec3& n, bool useVisibility) const {
    const Vec3 q = (p - d_.origin) * (1.0f / d_.spacing);
    const int x0 = std::max(0, std::min(int(std::floor(q.x)), int(d_.nx) - 2)), y0 = std::max(0, std::min(int(std::floor(q.y)), int(d_.ny) - 2)),
              z0 = std::max(0, std::min(int(std::floor(q.z)), int(d_.nz) - 2));
    const float fx = Saturate(q.x - float(x0)), fy = Saturate(q.y - float(y0)), fz = Saturate(q.z - float(z0));
    Vec3 sum{}; float wsum = 0.0f;
    Vec3 sumTri{}; float wTri = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        const uint32_t i = uint32_t(x0 + dx) + d_.nx * (uint32_t(y0 + dy) + d_.ny * uint32_t(z0 + dz));
        if (!valid_[i]) continue;
        const Probe& pr = probes_[i];
        const float tri = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
        const Vec3 ev = Ibl::EvalSh(pr.irr, n);
        sumTri = sumTri + ev * tri; wTri += tri;
        float w = tri;
        if (useVisibility) {
            const Vec3 toProbe = Position(i) - p;
            const float dist = Length(toProbe);
            const Vec3 dir = dist > 1e-5f ? toProbe * (1.0f / dist) : n;
            const float bf = (Dot(dir, n) + 1.0f) * 0.5f;
            w *= std::max(0.2f, bf * bf);
            // célula octaédrica do probe na direção de `p`
            const int o = OctaCell(toProbe * -1.0f);
            const float var = std::max(pr.m2[o] - pr.m1[o] * pr.m1[o], 1e-3f);
            const float m1 = pr.m1[o];
            float vis = 1.0f;
            if (dist > m1) { const float dd = dist - m1; vis = var / (var + dd * dd); vis = vis * vis * vis; }
            w *= std::max(vis, 1e-4f);
        }
        w = std::max(w, 1e-7f * (tri > 0 ? 1.0f : 0.0f));
        sum = sum + ev * w; wsum += w;
    }
    if (wsum <= 1e-9f) return wTri > 0 ? sumTri * (1.0f / wTri) : Vec3{};
    return sum * (1.0f / wsum);
}

} // namespace LightPrism::Rt
