// Fase 1 (sem clusters): itera todas as luzes. Útil como referência e para cenas minúsculas.
#ifndef LP_FORWARD_LIGHTING_HLSLI
#define LP_FORWARD_LIGHTING_HLSLI

#include "PBR.hlsli"
#include "LightBindings.hlsli"

float3 EvaluateAllLights(SurfaceData s, float3 worldPos) {
    float3 total = 0.0f;
    if (Globals.hasSun != 0u) total += EvaluateDirectionalLight(SunLight, s);
    for (uint i = 0; i < Globals.numPointLights; ++i) total += EvaluatePointLight(PointLightsBuffer[i], s, worldPos);
    for (uint j = 0; j < Globals.numSpotLights; ++j)  total += EvaluateSpotLight(SpotLightsBuffer[j], s, worldPos);
    return total;
}

#endif
