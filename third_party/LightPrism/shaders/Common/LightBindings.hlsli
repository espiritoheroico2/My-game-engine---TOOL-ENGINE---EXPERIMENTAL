// Bindings das luzes (set do passe de iluminação). Registradores fixos; ajuste aqui se conflitar.
#ifndef LP_LIGHT_BINDINGS_HLSLI
#define LP_LIGHT_BINDINGS_HLSLI

#include "LightTypes.hlsli"

StructuredBuffer<PointLight> PointLightsBuffer : register(t1);
StructuredBuffer<SpotLight>  SpotLightsBuffer  : register(t2);

cbuffer SunLightCB    : register(b1) { DirectionalLight SunLight; };
cbuffer GlobalLightCB : register(b0) { GlobalLighting   Globals;  };

#endif
