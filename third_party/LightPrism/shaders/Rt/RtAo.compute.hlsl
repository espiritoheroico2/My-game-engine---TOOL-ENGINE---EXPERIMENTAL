// AO por ray tracing (limite de distância) em um G-buffer de posição/normal do mundo. NÃO COMPILADO.
#include "RtEffects.hlsli"

cbuffer RtAoData : register(b5)
{
    float rtRadius; uint rtRays; float rtBias; uint rtWidth;
    uint rtHeight; uint3 rtPad;
};

Texture2D<float4>   g_WorldPos    : register(t0);   // xyz = posição, w = 0 se fundo
Texture2D<float4>   g_WorldNormal : register(t1);
RWTexture2D<float>  g_AoOut       : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= rtWidth || id.y >= rtHeight) return;
    float4 p = g_WorldPos[id.xy];
    if (p.w == 0.0f) { g_AoOut[id.xy] = 1.0f; return; }
    g_AoOut[id.xy] = RtAmbientOcclusion(p.xyz, normalize(g_WorldNormal[id.xy].xyz), rtRadius, rtRays, rtBias, id.y * rtWidth + id.x);
}
