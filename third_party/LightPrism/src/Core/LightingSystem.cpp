#include "LightPrism/LightingSystem.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "SlotMap.hpp"

#include "LightPrism/Util.hpp"

namespace LightPrism {

namespace {

template <class T>
struct Ranked { float priority; T gpu; };

constexpr float kMinSpotRange = 1e-4f;

PointLightGPU ToGpu(const PointLightDesc& d, float cutoff) {
    PointLightGPU g{};
    g.position = d.position;
    g.radius = std::max(d.radius, 1e-3f);
    g.cullRadius = cutoff > 0.0f ? std::max(AutoLightRadius(g.radius, MaxComponent(d.color) * d.intensity, cutoff), 1e-3f) : 0.0f;
    g.color = d.color;
    g.intensity = d.intensity;
    g.flags = d.flags;
    g.layerMask = d.layerMask;
    g.shadowIndex = 0xFFFFFFFFu; // sem sombra atribuída (Fase 3)
    return g;
}

SpotLightGPU ToGpu(const SpotLightDesc& d) {
    SpotLightGPU g{};
    g.position = d.position;
    g.radius = std::max(d.radius, 1e-3f);
    g.color = d.color;
    g.intensity = d.intensity;
    g.direction = Normalize(d.direction);
    const float outer = std::clamp(d.outerAngleRad, 1e-3f, kPi * 0.5f - 1e-3f);
    const float inner = std::clamp(d.innerAngleRad, 0.0f, outer);
    const float cosOuter = std::cos(outer);
    const float cosInner = std::cos(inner);
    g.spotScale = 1.0f / std::max(cosInner - cosOuter, kMinSpotRange);
    g.spotOffset = -cosOuter * g.spotScale;
    g.flags = d.flags;
    g.layerMask = d.layerMask;
    g.shadowIndex = 0xFFFFFFFFu;
    return g;
}

float Priority(const Vec3& pos, float radius, const Vec3& color, float intensity, const Vec3& cam) {
    const Vec3 d = pos - cam;
    const float edge = std::max(Length(d) - radius, 0.0f);
    return intensity * MaxComponent(color) / (edge * edge + 1.0f);
}

template <class T>
void SelectTop(std::vector<Ranked<T>>& ranked, size_t budget, std::vector<T>& out, uint32_t& dropped) {
    if (ranked.size() > budget) {
        std::nth_element(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(budget), ranked.end(),
                         [](const Ranked<T>& a, const Ranked<T>& b) { return a.priority > b.priority; });
        dropped += static_cast<uint32_t>(ranked.size() - budget);
        ranked.resize(budget);
    }
    // Ordem decrescente de prioridade: o culling em clusters corta pelo fim da lista, então
    // quando um cluster estoura o teto ficam as luzes mais importantes.
    std::sort(ranked.begin(), ranked.end(),
              [](const Ranked<T>& a, const Ranked<T>& b) { return a.priority > b.priority; });
    out.clear();
    out.reserve(ranked.size());
    for (const Ranked<T>& r : ranked) out.push_back(r.gpu);
}

template <class T>
bool EnsureBuffer(IGraphicsDevice& dev, std::unique_ptr<IGpuBuffer>& buf, uint64_t& capacity,
                  uint64_t count, BufferKind kind, const char* name) {
    if (buf && capacity >= count) return true;
    const uint64_t newCap = (kind == BufferKind::Uniform) ? 1 : NextPow2(count);
    BufferDesc bd;
    bd.sizeBytes = newCap * sizeof(T);
    bd.strideBytes = sizeof(T);
    bd.kind = kind;
    bd.debugName = name;
    auto nb = dev.CreateBuffer(bd);
    if (!nb) return false;
    buf = std::move(nb);
    capacity = newCap;
    return true;
}

} // namespace

struct LightingSystem::Impl {
    LightingConfig cfg;
    SlotMap<PointLightDesc> points;
    SlotMap<SpotLightDesc> spots;
    DirectionalLightDesc sun{};
    bool hasSun = false;

    // Scratch reaproveitado entre frames (sem alocação em regime).
    std::vector<Ranked<PointLightGPU>> rankedPoints;
    std::vector<Ranked<SpotLightGPU>> rankedSpots;
    std::vector<PointLightGPU> framePoints;
    std::vector<SpotLightGPU> frameSpots;
    FrameLightView view{};

    const IGraphicsDevice* boundDevice = nullptr;
    std::unique_ptr<IGpuBuffer> pointBuf, spotBuf, sunBuf, globalsBuf;
    uint64_t pointCap = 0, spotCap = 0, sunCap = 0, globalsCap = 0;
};

LightingSystem::LightingSystem(const LightingConfig& config) : m_impl(std::make_unique<Impl>()) {
    m_impl->cfg = config;
}
LightingSystem::~LightingSystem() = default;
LightingSystem::LightingSystem(LightingSystem&&) noexcept = default;
LightingSystem& LightingSystem::operator=(LightingSystem&&) noexcept = default;

LightHandle LightingSystem::AddPointLight(const PointLightDesc& d) {
    const auto id = m_impl->points.Insert(d);
    return {id.index, id.generation, LightType::Point};
}
LightHandle LightingSystem::AddSpotLight(const SpotLightDesc& d) {
    const auto id = m_impl->spots.Insert(d);
    return {id.index, id.generation, LightType::Spot};
}
bool LightingSystem::UpdatePointLight(LightHandle h, const PointLightDesc& d) {
    if (h.type != LightType::Point) return false;
    PointLightDesc* p = m_impl->points.Get({h.index, h.generation});
    if (!p) return false;
    *p = d;
    return true;
}
bool LightingSystem::UpdateSpotLight(LightHandle h, const SpotLightDesc& d) {
    if (h.type != LightType::Spot) return false;
    SpotLightDesc* p = m_impl->spots.Get({h.index, h.generation});
    if (!p) return false;
    *p = d;
    return true;
}
bool LightingSystem::RemoveLight(LightHandle h) {
    switch (h.type) {
    case LightType::Point: return m_impl->points.Remove({h.index, h.generation});
    case LightType::Spot:  return m_impl->spots.Remove({h.index, h.generation});
    default: return false;
    }
}

void LightingSystem::SetDirectionalLight(const DirectionalLightDesc& d) { m_impl->sun = d; m_impl->hasSun = true; }
void LightingSystem::ClearDirectionalLight() { m_impl->hasSun = false; }
void LightingSystem::SetQuality(const QualitySettings& q) { m_impl->cfg.quality = q; }
const LightingConfig& LightingSystem::Config() const { return m_impl->cfg; }

const FrameLightView& LightingSystem::BuildFrame(const FrameParams& p) {
    Impl& s = *m_impl;
    s.rankedPoints.clear();
    s.rankedSpots.clear();
    s.rankedPoints.reserve(s.points.Count());
    s.rankedSpots.reserve(s.spots.Count());

    s.points.ForEach([&](const PointLightDesc& d) {
        if ((d.layerMask & p.layerMask) == 0) return;
        s.rankedPoints.push_back({Priority(d.position, d.radius, d.color, d.intensity, p.cameraPosition), ToGpu(d, s.cfg.autoRadiusCutoff)});
    });
    s.spots.ForEach([&](const SpotLightDesc& d) {
        if ((d.layerMask & p.layerMask) == 0) return;
        s.rankedSpots.push_back({Priority(d.position, d.radius, d.color, d.intensity, p.cameraPosition), ToGpu(d)});
    });

    uint32_t dropped = 0;
    SelectTop(s.rankedPoints, s.cfg.maxPointLights, s.framePoints, dropped);
    SelectTop(s.rankedSpots, s.cfg.maxSpotLights, s.frameSpots, dropped);

    // Pré-exposição aplicada na CPU: o shader recebe intensidades prontas.
    for (PointLightGPU& l : s.framePoints) l.intensity *= p.preExposure;
    for (SpotLightGPU& l : s.frameSpots) l.intensity *= p.preExposure;

    FrameLightView& v = s.view;
    v.points = s.framePoints.data();
    v.pointCount = static_cast<uint32_t>(s.framePoints.size());
    v.spots = s.frameSpots.data();
    v.spotCount = static_cast<uint32_t>(s.frameSpots.size());
    v.droppedLights = dropped;

    v.sun = {};
    if (s.hasSun) {
        v.sun.direction = Normalize(s.sun.direction);
        v.sun.color = s.sun.color;
        v.sun.intensity = s.sun.intensity * p.preExposure;
        v.sun.flags = s.sun.flags;
    }

    v.globals = {};
    v.globals.numPointLights = v.pointCount;
    v.globals.numSpotLights = v.spotCount;
    v.globals.hasSun = s.hasSun ? 1u : 0u;
    v.globals.qualityFlags = static_cast<uint32_t>(s.cfg.quality.tier);
    v.globals.cameraPosition = p.cameraPosition;
    v.globals.preExposure = p.preExposure;
    return v;
}

bool LightingSystem::UploadToGPU(IGraphicsDevice& dev) {
    Impl& s = *m_impl;
    if (s.boundDevice != &dev) { // dispositivo trocado: recria tudo
        s.pointBuf.reset(); s.spotBuf.reset(); s.sunBuf.reset(); s.globalsBuf.reset();
        s.pointCap = s.spotCap = s.sunCap = s.globalsCap = 0;
        s.boundDevice = &dev;
    }
    const FrameLightView& v = s.view;

    if (!EnsureBuffer<PointLightGPU>(dev, s.pointBuf, s.pointCap, std::max(v.pointCount, 1u), BufferKind::Structured, "LP_PointLights")) return false;
    if (!EnsureBuffer<SpotLightGPU>(dev, s.spotBuf, s.spotCap, std::max(v.spotCount, 1u), BufferKind::Structured, "LP_SpotLights")) return false;
    if (!EnsureBuffer<DirectionalLightGPU>(dev, s.sunBuf, s.sunCap, 1, BufferKind::Uniform, "LP_Sun")) return false;
    if (!EnsureBuffer<GlobalLightingGPU>(dev, s.globalsBuf, s.globalsCap, 1, BufferKind::Uniform, "LP_Globals")) return false;

    bool ok = true;
    if (v.pointCount) ok &= s.pointBuf->Update(v.points, uint64_t(v.pointCount) * sizeof(PointLightGPU));
    if (v.spotCount)  ok &= s.spotBuf->Update(v.spots, uint64_t(v.spotCount) * sizeof(SpotLightGPU));
    ok &= s.sunBuf->Update(&v.sun, sizeof(DirectionalLightGPU));
    ok &= s.globalsBuf->Update(&v.globals, sizeof(GlobalLightingGPU));
    return ok;
}

IGpuBuffer* LightingSystem::PointLightBuffer() const { return m_impl->pointBuf.get(); }
IGpuBuffer* LightingSystem::SpotLightBuffer() const { return m_impl->spotBuf.get(); }
IGpuBuffer* LightingSystem::SunBuffer() const { return m_impl->sunBuf.get(); }
IGpuBuffer* LightingSystem::GlobalsBuffer() const { return m_impl->globalsBuf.get(); }

} // namespace LightPrism
