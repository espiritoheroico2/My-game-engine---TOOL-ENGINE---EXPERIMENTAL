#include "LightPrism/Rt/Transfer.hpp"

#include <cmath>

namespace LightPrism::Rt {

std::vector<Vec3> FibonacciSphere(uint32_t n) {
    std::vector<Vec3> d(n ? n : 1);
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    for (uint32_t i = 0; i < d.size(); ++i) {
        const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(d.size());
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y)), phi = golden * float(i);
        d[i] = {r * std::cos(phi), y, r * std::sin(phi)};
    }
    return d;
}

void TransferCache::Init(std::vector<TransferSample> samples, const TransferConfig& cfg) {
    m_cfg = cfg;
    m_samples = std::move(samples);
    m_t.assign(m_samples.size() * 9, 0.0f);
    m_dirty.assign(m_samples.size(), 1);
    m_dirtyCount = uint32_t(m_samples.size());
    m_cursor = 0;
    m_raysCast = 0;
    m_dirs = FibonacciSphere(cfg.rays);
    m_basis.resize(m_dirs.size() * 9);
    for (size_t k = 0; k < m_dirs.size(); ++k) Ibl::ShBasis(m_dirs[k], &m_basis[k * 9]);
}

void TransferCache::BakeOne(const Bvh& bvh, uint32_t i) {
    const TransferSample& s = m_samples[i];
    Ray r;
    r.origin = s.position + s.normal * m_cfg.bias;
    r.tMax = m_cfg.maxDistance;
    double acc[9] = {};
    for (size_t k = 0; k < m_dirs.size(); ++k) {
        const float cosT = Dot(s.normal, m_dirs[k]);
        if (cosT <= 0.0f) continue;                  // abaixo do horizonte: cos+ = 0, não gasta raio
        r.dir = m_dirs[k];
        ++m_raysCast;
        if (!bvh.Empty() && bvh.Occluded(r)) continue;
        for (int b = 0; b < 9; ++b) acc[b] += double(cosT) * double(m_basis[k * 9 + b]);
    }
    const double w = 4.0 * double(kPi) / double(m_dirs.size());  // Monte Carlo uniforme na esfera
    for (int b = 0; b < 9; ++b) m_t[size_t(i) * 9 + b] = float(acc[b] * w);
}

uint32_t TransferCache::Bake(const Bvh& bvh, uint32_t budget) {
    if (m_dirtyCount == 0) return 0;
    const uint32_t n = Count();
    uint32_t done = 0;
    for (uint32_t step = 0; step < n && m_dirtyCount > 0; ++step) {
        const uint32_t i = (m_cursor + step) % n;
        if (!m_dirty[i]) continue;
        BakeOne(bvh, i);
        m_dirty[i] = 0; --m_dirtyCount; ++done;
        if (budget && done >= budget) { m_cursor = (i + 1) % n; break; }
    }
    return done;
}

uint32_t TransferCache::InvalidateRegion(const Aabb& changed) {
    const Aabb reach = changed.Expanded(m_cfg.maxDistance + m_cfg.bias + 1e-3f);
    uint32_t marked = 0;
    for (uint32_t i = 0; i < Count(); ++i) {
        const Vec3& p = m_samples[i].position;
        if (p.x < reach.lo.x || p.x > reach.hi.x || p.y < reach.lo.y || p.y > reach.hi.y || p.z < reach.lo.z || p.z > reach.hi.z) continue;
        if (!m_dirty[i]) { m_dirty[i] = 1; ++m_dirtyCount; ++marked; }
    }
    return marked;
}

void TransferCache::InvalidateAll() {
    std::fill(m_dirty.begin(), m_dirty.end(), uint8_t(1));
    m_dirtyCount = Count();
}

Vec3 TransferCache::Irradiance(uint32_t i, const Ibl::Sh9& L) const {
    const float* t = Transfer(i);
    Vec3 e{};
    for (int k = 0; k < 9; ++k) e = e + L.c[k] * t[k];
    return e * (1.0f / kPi);
}

TransferStats TransferCache::Stats() const {
    TransferStats s;
    s.samples = Count(); s.dirty = m_dirtyCount; s.raysCast = m_raysCast;
    s.bytes = uint64_t(m_t.size()) * 4 + m_dirty.size() + uint64_t(m_samples.size()) * sizeof(TransferSample);
    return s;
}

} // namespace LightPrism::Rt
