// Sombras suaves por SDF (espelha include/LightPrism/Sdf/Sdf.hpp). NÃO COMPILADO — validar na engine.
// Volume int8 SNORM em Texture3D<float> (R8_SNORM): valor * maxDist = distância em metros. Amostragem trilinear em hardware.
#ifndef LP_SDF_SHADOW_HLSLI
#define LP_SDF_SHADOW_HLSLI

cbuffer SdfData : register(b6)
{
    float3 sdfOrigin; float sdfVoxel;      // canto mínimo do volume e tamanho do voxel
    float3 sdfInvDim; float sdfMaxDist;    // 1 / (dimensões em voxels), distância máxima codificada
};
Texture3D<float> g_Sdf : register(t6);
SamplerState g_SdfSampler : register(s6);   // linear, clamp

float SdfDistance(float3 p)
{
    float3 q = (p - sdfOrigin) / sdfVoxel;
    float3 uvw = (q + 0.5f) * sdfInvDim;
    bool outside = any(uvw < 0.0f) || any(uvw > 1.0f);
    return outside ? sdfMaxDist : g_Sdf.SampleLevel(g_SdfSampler, uvw, 0) * sdfMaxDist;
}

float SdfDiscVisible(float s)
{
    s = clamp(s, -1.0f, 1.0f);
    return 0.5f + (s * sqrt(1.0f - s * s) + asin(s)) / 3.14159265359f;
}

float SdfSoftShadow(float3 origin, float3 dir, float tMin, float tMax, float coneHalfAngle, uint maxSteps)
{
    float tanA = tan(max(coneHalfAngle, 1e-4f));
    float sMin = 1.0f, t = tMin;
    [loop] for (uint i = 0; i < maxSteps && t < tMax; ++i)
    {
        float h = SdfDistance(origin + dir * t);
        sMin = min(sMin, h / (t * tanA));
        if (sMin <= -1.0f) return 0.0f;
        t += clamp(abs(h), 0.02f, 4.0f);
    }
    return SdfDiscVisible(sMin);
}
#endif
