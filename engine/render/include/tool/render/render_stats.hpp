#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace tool::render {

// Passes do render, na ordem em que acontecem no quadro. Count = quantidade de passes.
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

// Janelas que renderizam: Scene (editor) e Game.
enum class RenderViewId : std::uint8_t { Scene, Game, Count };

// Quantidade de passes, para dimensionar arrays.
inline constexpr std::size_t kRenderPassCount = static_cast<std::size_t>(RenderPassId::Count);
// Quantidade de views, para dimensionar arrays.
inline constexpr std::size_t kRenderViewCount = static_cast<std::size_t>(RenderViewId::Count);

// Nome do passe para o painel de estatisticas.
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

// Nome da view para o painel de estatisticas.
inline const char* renderViewName(RenderViewId view) {
  switch (view) {
    case RenderViewId::Scene: return "Scene";
    case RenderViewId::Game: return "Game";
    case RenderViewId::Count: break;
  }
  return "Unknown";
}

// Contadores de um passe: draws, instancias, triangulos e tempo de CPU em ms.
struct PassStats {
  std::uint64_t drawCalls = 0;
  std::uint64_t instances = 0;
  std::uint64_t triangles = 0;
  double cpuMs = 0.0;

  // Conta um draw. Triangulos = indices / 3 por instancia.
  void recordDraw(std::uint32_t indexCount, std::uint32_t instanceCount) {
    ++drawCalls;
    instances += instanceCount;
    triangles += static_cast<std::uint64_t>(indexCount / 3u) * instanceCount;
  }

  // Soma os contadores de outro passe.
  void add(const PassStats& other) {
    drawCalls += other.drawCalls;
    instances += other.instances;
    triangles += other.triangles;
    cpuMs += other.cpuMs;
  }
};

// Mede o tempo de CPU de um passe (RAII). Para ao sair do escopo ou em stop().
class PassTimer {
 public:
  // Comeca a medir agora. O tempo e somado em stats.cpuMs ao parar.
  explicit PassTimer(PassStats& stats) : stats_{stats}, start_{Clock::now()} {}
  // Copia proibida: cada medicao e unica.
  PassTimer(const PassTimer&) = delete;
  // Atribuicao proibida (mesmo motivo da copia).
  PassTimer& operator=(const PassTimer&) = delete;
  // Para a medicao, se ela ainda estiver rodando.
  ~PassTimer() { stop(); }
  // Soma o tempo decorrido em stats.cpuMs. Chamadas seguintes nao fazem nada.
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

// Contadores do cache de sombras pontuais: reuso, renderizacoes e motivos de refazer uma face.
struct ShadowCacheStats {
  std::uint64_t pointFacesReused = 0;
  std::uint64_t pointFacesRendered = 0;
  std::uint64_t pointFacesFirstUse = 0;
  std::uint64_t pointFacesTargetReset = 0;
  std::uint64_t pointFacesLightChanged = 0;
  std::uint64_t pointFacesCastersChanged = 0;
  std::uint64_t pointFaceRenderFailures = 0;

  // Soma os contadores de outro cache de sombra.
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

// Estatisticas de uma view: contadores do backend e de cada passe.
struct RenderViewStats {
  // false = a view nao renderizou neste quadro; os totais a ignoram.
  bool valid = false;
  std::uint64_t backendDrawCalls = 0;
  std::uint64_t backendTriangles = 0;
  std::uint64_t backendPipelineSwitches = 0;
  std::uint64_t backendBufferUploads = 0;
  std::uint64_t backendSubmitCount = 0;
  std::uint64_t backendFlushCount = 0;
  std::array<PassStats, kRenderPassCount> passes{};
  ShadowCacheStats shadowCache{};

  // Soma de todos os passes da view.
  PassStats total() const {
    PassStats result;
    for (const PassStats& pass : passes) result.add(pass);
    return result;
  }

  // Soma os contadores do backend (draws, submits, flushes).
  void addBackend(const RenderViewStats& other) {
    backendDrawCalls += other.backendDrawCalls;
    backendTriangles += other.backendTriangles;
    backendPipelineSwitches += other.backendPipelineSwitches;
    backendBufferUploads += other.backendBufferUploads;
    backendSubmitCount += other.backendSubmitCount;
    backendFlushCount += other.backendFlushCount;
  }
};

// Estatisticas de um quadro inteiro (todas as views). beginFrame() avanca o id e zera as views.
struct FrameRenderStats {
  std::uint64_t frameId = 0;
  std::array<RenderViewStats, kRenderViewCount> views{};

  // Avanca o id do quadro e zera as views.
  void beginFrame() {
    ++frameId;
    views = {};
  }

  // Total de um passe, somando as views validas.
  PassStats total(RenderPassId pass) const {
    PassStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.passes[static_cast<std::size_t>(pass)]);
    }
    return result;
  }

  // Total geral de todos os passes das views validas.
  PassStats total() const {
    PassStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.total());
    }
    return result;
  }

  // Soma dos contadores do backend das views validas.
  RenderViewStats backendTotal() const {
    RenderViewStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.addBackend(view);
    }
    return result;
  }

  // Soma do cache de sombras das views validas.
  ShadowCacheStats shadowCacheTotal() const {
    ShadowCacheStats result;
    for (const RenderViewStats& view : views) {
      if (view.valid) result.add(view.shadowCache);
    }
    return result;
  }
};

} // namespace tool::render
