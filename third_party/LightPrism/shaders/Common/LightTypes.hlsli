// Espelha include/LightPrism/LightTypes.hpp. Mudou lá? Mude aqui (e vice-versa).
#ifndef LP_LIGHT_TYPES_HLSLI
#define LP_LIGHT_TYPES_HLSLI

struct PointLight {          // 48 bytes
    float3 position;  float radius;
    float3 color;     float intensity;
    uint   flags;     uint layerMask; uint shadowIndex; float cullRadius;
};

struct SpotLight {           // 64 bytes
    float3 position;  float radius;
    float3 color;     float intensity;
    float3 direction; float spotScale;   // direção de viagem do raio, normalizada
    float  spotOffset; uint flags; uint layerMask; uint shadowIndex;
};

struct DirectionalLight {    // 32 bytes
    float3 direction; float intensity;   // direção de viagem do raio (L = -direction)
    float3 color;     uint flags;
};

struct GlobalLighting {      // 32 bytes
    uint   numPointLights; uint numSpotLights; uint hasSun; uint qualityFlags;
    float3 cameraPosition; float preExposure;
};

#endif
