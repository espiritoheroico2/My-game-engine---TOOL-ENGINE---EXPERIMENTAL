// Cascaded Shadow Maps do sol - VERSAO DA ENGINE (adaptada do Shadow.hlsli da Light Prism).
// Mesma matematica da referencia CPU (include/LightPrism/Shadow/Csm.hpp): profundidade convencional (perto = 0, longe = 1),
// PCF por texel (1, 4 ou 9 taps) com `depth <= armazenado`.
// DIFERENCA DELIBERADA: o RHI da engine nao tem Texture2DArray nem SamplerComparisonState. Cada cascata e uma Texture2D
// propria (CsmMap0..3, ligadas por nome) lida com Load. O resultado e o mesmo do original, sem filtragem bilinear por hardware.
#ifndef LP_SHADOW_HLSLI
#define LP_SHADOW_HLSLI

#define LP_MAX_CSM_CASCADES 4

struct CsmCascade {                    // 80 bytes (CsmCascadeGPU)
    row_major float4x4 viewProj;
    float splitNear, splitFar, texelWorldSize, depthRange;
};

struct CsmData {                       // 352 bytes (CsmGPU)
    CsmCascade cascades[LP_MAX_CSM_CASCADES];
    uint  cascadeCount;                // 0 = sombras desligadas
    float shadowMapSize;
    float depthBias;                   // metros
    float normalBias;                  // texels
    float blendFraction;
    uint  pcfTaps;                     // 1, 4 ou 9
    float2 _pad;
};

cbuffer CsmCB : register(b2) { CsmData Csm; };
Texture2D<float> CsmMap0 : register(t3);
Texture2D<float> CsmMap1 : register(t4);
Texture2D<float> CsmMap2 : register(t5);
Texture2D<float> CsmMap3 : register(t6);

// Le um texel do mapa da cascata k (coordenadas ja limitadas a [0, size-1]).
float CsmTexel(uint k, int2 p) {
    if (k == 0u) return CsmMap0.Load(int3(p, 0));
    if (k == 1u) return CsmMap1.Load(int3(p, 0));
    if (k == 2u) return CsmMap2.Load(int3(p, 0));
    return CsmMap3.Load(int3(p, 0));
}

float CsmSampleCascade(uint k, float3 worldPos, float3 N) {
    CsmCascade c = Csm.cascades[k];
    float3 p = worldPos + N * (Csm.normalBias * c.texelWorldSize);
    float3 clip = mul(c.viewProj, float4(p, 1.0f)).xyz;
    if (any(abs(clip.xy) > 1.0f) || clip.z > 1.0f) return 1.0f;
    float depth = clip.z - Csm.depthBias / c.depthRange;
    float size = Csm.shadowMapSize;
    float2 t = float2(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f) * size;
    int maxIdx = int(size) - 1;

    float lit = 0.0f;
    float n = 1.0f;
    if (Csm.pcfTaps >= 9u) {
        n = 9.0f;
        [unroll] for (int j = -1; j <= 1; ++j)
            [unroll] for (int i = -1; i <= 1; ++i) {
                int2 q = clamp(int2(floor(t + float2(i, j))), int2(0, 0), int2(maxIdx, maxIdx));
                lit += depth <= CsmTexel(k, q) ? 1.0f : 0.0f;
            }
    } else if (Csm.pcfTaps >= 4u) {
        n = 4.0f;
        [unroll] for (int j = 0; j < 2; ++j)
            [unroll] for (int i = 0; i < 2; ++i) {
                int2 q = clamp(int2(floor(t + (float2(i, j) - 0.5f))), int2(0, 0), int2(maxIdx, maxIdx));
                lit += depth <= CsmTexel(k, q) ? 1.0f : 0.0f;
            }
    } else {
        int2 q = clamp(int2(floor(t)), int2(0, 0), int2(maxIdx, maxIdx));
        lit = depth <= CsmTexel(k, q) ? 1.0f : 0.0f;
    }
    return lit / n;
}

// viewDepth = -viewPos.z. Fora do alcance das sombras (ou desligadas): 1.
float CsmVisibility(float3 worldPos, float3 N, float viewDepth) {
    uint k = 0;
    while (k < Csm.cascadeCount && viewDepth > Csm.cascades[k].splitFar) ++k;
    if (k >= Csm.cascadeCount) return 1.0f;

    float vis = CsmSampleCascade(k, worldPos, N);
    CsmCascade c = Csm.cascades[k];
    float blendWidth = Csm.blendFraction * (c.splitFar - c.splitNear);
    if (k + 1u < Csm.cascadeCount && blendWidth > 0.0f && viewDepth > c.splitFar - blendWidth) {
        float t = saturate((viewDepth - (c.splitFar - blendWidth)) / blendWidth);
        vis = lerp(vis, CsmSampleCascade(k + 1u, worldPos, N), t);
    }
    return vis;
}

#endif
