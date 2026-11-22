#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace tool::render {

enum class RenderPassId : std::uint8_t {
  CsmShadow,
  PointShadow,
  Prepass,
  Ssao,
  SsaoBlur,
  Opaque,
  Emission,
  Transparent,
  BloomExtract,
  BloomBlurH,
  BloomBlurV,
  BloomComposite,
  Count
};

enum class RenderViewId : std::uint8_t { Scene, Game, Count };

inline constexpr std::size_t kRenderPassCount = static_cast<std::size_t>(RenderPassId::Count);
inline constexpr std::size_t kRenderViewCount = static_cast<std::size_t>(RenderViewId::Count);

inline const char* renderPassName(RenderPassId pass) {
  switch (pass) {
    case RenderPassId::CsmShadow: return "CSM shadow";
    case RenderPassId::PointShadow: return "Point/Spot shadow";
    case RenderPassId::Prepass: return "Depth/normal prepass";
    case RenderPassId::Ssao: return "SSAO";
    case RenderPassId::SsaoBlur: return "SSAO blur";
    case RenderPassId::Opaque: return "Opaque";
    case RenderPassId::Emission: return "Emission";
    case RenderPassId::Transparent: return "Transparent";
    case RenderPassId::BloomExtract: return "Bloom extract";
    case RenderPassId::BloomBlurH: return "Bloom blur H";
    case RenderPassId::BloomBlurV: return "Bloom blur V";
    case RenderPassId::BloomComposite: return "Bloom composite";
    case RenderPassId::Count: break;
  }
  return "Unknown";
}

inline const char* renderViewName(RenderViewId view) {
  switch (view) {
    case RenderViewId::Scene: return "Scene";
    case RenderViewId::Game: return "Game";
    case RenderViewId::Count: break;
  }
  return "Unknown";
}

struct PassStats {
  std::uint64_t drawCalls = 0;
  std::uint64_t instances = 0;
  std::uint64_t triangles = 0;
  double cpuMs = 0.0;

  void recordDraw(std::uint32_t indexCount, std::uint32_t instanceCount) {
    ++drawCalls;
    instances += instanceCount;
    triangles += static_cast<std::uint64_t>(indexCount / 3u) * instanceCount;
  }

  void add(const PassStats& other) {
    drawCalls += other.drawCalls;
    instances += other.instances;
    triangles += other.triangles;
    cpuMs += other.cpuMs;
  }
};

class PassTimer {
 public:
  explicit PassTimer(PassStats& stats) : stats_{stats}, start_{Clock::now()} {}
  PassTimer(const PassTimer&) = delete;
  PassTimer& operator=(const PassTimer&) = delete;
  ~PassTimer() { stop(); }
  void stop() {
    if (stopped_) return;
    stats_.cpuMs += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    stopped_ = true;
  }

 private:
  using Clock = std::chrono::steady_clock;
  PassStats& stats_;
  Clock::time_point start_;
  bool stopped_ = false;
};

struct ShadowCacheStats {
  std::uint64_t pointFacesReused = 0;
  std::uint64_t pointFacesRendered = 0;
  std::uint64_t pointFacesFirstUse = 0;
  std::uint64_t pointFacesTargetReset = 0;
  std::uint64_t pointFacesLightChanged = 0;
  std::uint64_t pointFacesCastersChanged = 0;
  std::uint64_t pointFaceRenderFailures = 0;

  void add(const ShadowCacheStats& other) {
    pointFacesReused += other.pointFacesReused;
    pointFacesRendered += other.pointFacesRendered;
    pointFacesFirstUse += other.pointFacesFirstUse;
    pointFacesTargetReset += other.pointFacesTargetReset;
    pointFacesLightChanged += other.pointFacesLightChanged;
    pointFacesCastersChanged += other.pointFacesCastersChanged;
    pointFaceRenderFailures += other.pointFaceRenderFailures;
  }
};

struct RenderViewStats {
  bool valid = false;
  std::uint64_t backendDrawCalls = 0;
  std::uint64_t backendTriangles = 0;
  std::uint64_t backendPipelineSwitches = 0;
  std::uint64_t backendBufferUploads = 0;
  std::array<PassStats, kRenderPassCount> passes{};
  ShadowCacheStats shadowCache{};

  PassStats total() const {
    PassStats result;
    for (const PassStats& pass : passes) result.add(pass);
    return result;
  }

  void addBackend(const RenderViewStats& other) {
    backendDrawCalls += other.backendDrawCalls;
    backendTriangles += other.backendTriangles;
    backendPipelineSwitches += other.backendPipelineSwitches;
    backendBufferUploads += other.backendBufferUploads;
  }
};

struct FrameRenderStats {
  std::uint64_t frameId = 0;
  std::array<RenderViewStats, kRenderViewCount> views{};

  void beginFrame() {
    ++frameId;
    views = {};
  }

  PassStats total(RenderPassId pass) const {
    PassStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.passes[static_cast<std::size_t>(pass)]);
    }
    return result;
  }

  PassStats total() const {
    PassStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.total());
    }
    return result;
  }

  RenderViewStats backendTotal() const {
    RenderViewStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.addBackend(view);
    }
    return result;
  }

  ShadowCacheStats shadowCacheTotal() const {
    ShadowCacheStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.shadowCache);
    }
    return result;
  }
};

} // namespace tool::render
