#pragma once
// Tipos de luz: descrições CPU (amigáveis) e structs GPU (layout rígido, espelhado em
// shaders/Common/LightTypes.hlsli). Qualquer mudança aqui exige mudar o .hlsli.
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "LightPrism/Math.hpp"

namespace LightPrism {

enum class LightType : uint8_t { Point, Spot, Directional };

enum LightFlags : uint32_t {
    LightFlag_None        = 0,
    LightFlag_CastShadows = 1u << 0,
    LightFlag_Volumetric  = 1u << 1,
    LightFlag_StaticOnly  = 1u << 2, // candidata a bake/cache estático
};

// Handle com geração: invalida referências antigas após Remove().
struct LightHandle {
    uint32_t index = 0;
    uint32_t generation = 0; // 0 = inválido
    LightType type = LightType::Point;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
};

// ---------------------------------------------------------------------------
// Descrições CPU (o que o jogo/ECS preenche)
// ---------------------------------------------------------------------------
struct PointLightDesc {
    Vec3 position{};
    float radius = 10.0f; // metros
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    uint32_t flags = LightFlag_None;
    uint32_t layerMask = 0xFFFFFFFFu;
};

struct SpotLightDesc {
    Vec3 position{};
    float radius = 10.0f;
    Vec3 direction{0.0f, -1.0f, 0.0f};
    float innerAngleRad = 0.35f;
    float outerAngleRad = 0.60f;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    uint32_t flags = LightFlag_None;
    uint32_t layerMask = 0xFFFFFFFFu;
};

struct DirectionalLightDesc {
    Vec3 direction{0.3f, -1.0f, 0.2f}; // direção de viagem dos raios
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    uint32_t flags = LightFlag_CastShadows;
};

// ---------------------------------------------------------------------------
// Structs GPU (StructuredBuffer / cbuffer). Múltiplos de 16 bytes.
// ---------------------------------------------------------------------------
struct alignas(16) PointLightGPU {
    Vec3 position;  float radius;
    Vec3 color;     float intensity;
    uint32_t flags; uint32_t layerMask; uint32_t shadowIndex;
    float cullRadius;  // raio só do culling em clusters (<= radius). 0 = usa radius. A janela de atenuação continua usando radius.
};

struct alignas(16) SpotLightGPU {
    Vec3 position;  float radius;
    Vec3 color;     float intensity;
    Vec3 direction; float spotScale;   // direction normalizada (viagem do raio)
    float spotOffset; uint32_t flags; uint32_t layerMask; uint32_t shadowIndex;
};

struct alignas(16) DirectionalLightGPU {
    Vec3 direction; float intensity;
    Vec3 color;     uint32_t flags;
};

struct alignas(16) GlobalLightingGPU {
    uint32_t numPointLights; uint32_t numSpotLights; uint32_t hasSun; uint32_t qualityFlags;
    Vec3 cameraPosition;     float preExposure;
};

#define LP_ASSERT_GPU_TYPE(T, SIZE)                                                     \
    static_assert(sizeof(T) == (SIZE), #T " size mismatch com HLSL");                   \
    static_assert(alignof(T) == 16, #T " deve ter alinhamento 16");                     \
    static_assert(std::is_standard_layout<T>::value, #T " deve ser standard-layout");   \
    static_assert(std::is_trivially_copyable<T>::value, #T " deve ser trivialmente copiável")

LP_ASSERT_GPU_TYPE(PointLightGPU, 48);
LP_ASSERT_GPU_TYPE(SpotLightGPU, 64);
LP_ASSERT_GPU_TYPE(DirectionalLightGPU, 32);
LP_ASSERT_GPU_TYPE(GlobalLightingGPU, 32);

static_assert(offsetof(PointLightGPU, color) == 16 && offsetof(PointLightGPU, flags) == 32, "PointLightGPU offsets");
static_assert(offsetof(SpotLightGPU, direction) == 32 && offsetof(SpotLightGPU, spotOffset) == 48, "SpotLightGPU offsets");
static_assert(offsetof(DirectionalLightGPU, color) == 16, "DirectionalLightGPU offsets");
static_assert(offsetof(GlobalLightingGPU, cameraPosition) == 16, "GlobalLightingGPU offsets");

} // namespace LightPrism
