#pragma once
#include <cmath>
// API pública da Light Prism. Independente de engine (sem ECS) e de API gráfica (via Rhi.hpp).
// Fluxo por frame: Add/Update/Remove luzes -> BuildFrame(params) -> UploadToGPU(device).
#include <cstdint>
#include <memory>

#include "LightPrism/LightTypes.hpp"
#include "LightPrism/Quality.hpp"
#include "LightPrism/Rhi.hpp"

namespace LightPrism {

struct LightingConfig {
    uint32_t maxPointLights = 4096;
    uint32_t maxSpotLights = 1024;
    QualitySettings quality = MakeQualitySettings(QualityTier::Medium);
    // Raio automático: encolhe o raio de cada luz até onde a contribuição sem janela (I/d²) cai abaixo deste limiar
    // (em unidades de radiância já exposta). 0 = desligado. Menos clusters tocados => culling e shading mais baratos.
    float autoRadiusCutoff = 0.0f;
};

// Raio onde I/d² == cutoff, limitado ao raio pedido. `lum` = maior componente de cor*intensidade.
inline float AutoLightRadius(float requestedRadius, float lum, float cutoff) {
    if (cutoff <= 0.0f || lum <= 0.0f) return requestedRadius;
    const float r = std::sqrt(lum / cutoff);
    return r < requestedRadius ? r : requestedRadius;
}

struct FrameParams {
    Vec3 cameraPosition{};
    float preExposure = 1.0f;
    uint32_t layerMask = 0xFFFFFFFFu; // só entram luzes com (layerMask & light.layerMask) != 0
};

// Dados prontos para subir à GPU. Ponteiros válidos até o próximo BuildFrame().
struct FrameLightView {
    const PointLightGPU* points = nullptr;
    uint32_t pointCount = 0;
    const SpotLightGPU* spots = nullptr;
    uint32_t spotCount = 0;
    DirectionalLightGPU sun{};
    GlobalLightingGPU globals{};
    uint32_t droppedLights = 0; // luzes descartadas por orçamento neste frame
};

class LightingSystem {
public:
    explicit LightingSystem(const LightingConfig& config = {});
    ~LightingSystem();
    LightingSystem(LightingSystem&&) noexcept;
    LightingSystem& operator=(LightingSystem&&) noexcept;
    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;

    [[nodiscard]] LightHandle AddPointLight(const PointLightDesc& desc);
    [[nodiscard]] LightHandle AddSpotLight(const SpotLightDesc& desc);
    [[nodiscard]] bool UpdatePointLight(LightHandle h, const PointLightDesc& desc);
    [[nodiscard]] bool UpdateSpotLight(LightHandle h, const SpotLightDesc& desc);
    [[nodiscard]] bool RemoveLight(LightHandle h);

    void SetDirectionalLight(const DirectionalLightDesc& desc);
    void ClearDirectionalLight();

    void SetQuality(const QualitySettings& q);
    [[nodiscard]] const LightingConfig& Config() const;

    // Seleciona/converte luzes do frame (aplica orçamento e prioridade). Sem alocação em regime.
    [[nodiscard]] const FrameLightView& BuildFrame(const FrameParams& params);

    // Cria/redimensiona buffers conforme necessário e envia os dados. Retorna false em falha.
    [[nodiscard]] bool UploadToGPU(IGraphicsDevice& device);

    IGpuBuffer* PointLightBuffer() const;
    IGpuBuffer* SpotLightBuffer() const;
    IGpuBuffer* SunBuffer() const;
    IGpuBuffer* GlobalsBuffer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LightPrism
