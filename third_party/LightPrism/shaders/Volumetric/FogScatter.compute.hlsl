// Neblina volumétrica: espalhamento + integração ao longo da coluna (espelha include/LightPrism/Volumetric/Fog.hpp).
// NÃO COMPILADO — validar na engine. Uma thread por coluna (x,y) do volume de froxels; escreve (L.rgb, T) acumulados até a borda FAR
// de cada slice em um RWTexture3D<float4> (RGBA16F). O pixel lê com SampleLevel trilinear na profundidade do pixel:
//     cor = cor * T + L.
// Visibilidade do sol: Common/Shadow.hlsli (CsmVisibility). Troque por VSM/SDF/RT conforme o preset.
#include "../Common/Shadow.hlsli"

cbuffer FogData : register(b7)
{
    row_major float4x4 fogInvView;    // view -> mundo
    float3 fogSunToLight; float fogSigmaT;
    float3 fogSunRadiance; float fogHeightFalloff;
    float3 fogScatterAlbedo; float fogAnisotropy;
    float3 fogAmbient; float fogNear;
    float fogMaxDistance; float fogProj00; float fogProj11; uint fogGridZ;
    uint fogGridX; uint fogGridY; uint2 fogPad;
};
RWTexture3D<float4> g_FogOut : register(u7);

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (12.5663706144f * pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

float FogSliceEdge(float k) { return fogNear * pow(fogMaxDistance / fogNear, k / float(fogGridZ)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= fogGridX || id.y >= fogGridY) return;
    float ndcX = (id.x + 0.5f) / fogGridX * 2.0f - 1.0f;
    float ndcY = 1.0f - (id.y + 0.5f) / fogGridY * 2.0f;
    float3 dv = float3(ndcX / fogProj00, ndcY / fogProj11, -1.0f);
    float rayLen = length(dv);
    float3 worldDir = normalize(mul(float4(dv, 0.0f), fogInvView).xyz);
    float3 L = 0; float T = 1.0f;
    for (uint z = 0; z < fogGridZ; ++z)
    {
        float d0 = FogSliceEdge(z), d1 = FogSliceEdge(z + 1), dm = sqrt(d0 * d1);
        float3 p = mul(float4(dv * dm, 1.0f), fogInvView).xyz;
        float sigmaT = fogSigmaT * (fogHeightFalloff > 0.0f ? exp(-fogHeightFalloff * max(p.y, 0.0f)) : 1.0f);
        float dt = (d1 - d0) * rayLen;
        float vis = CsmVisibility(p, float3(0, 1, 0), dm);
        float3 radiance = fogAmbient + fogSunRadiance * (HenyeyGreenstein(dot(fogSunToLight, worldDir), fogAnisotropy) * vis);
        float3 S = fogScatterAlbedo * sigmaT * radiance;
        float e = exp(-sigmaT * dt);
        if (sigmaT > 1e-9f) L += S * (T * (1.0f - e) / sigmaT);
        T *= e;
        g_FogOut[uint3(id.xy, z)] = float4(L, T);
    }
}
