// Cascaded Shadow Maps do sol. Espelha include/LightPrism/Shadow/Csm.hpp (a versão C++ é a referência testada).
// Diferença deliberada: aqui o PCF usa SampleCmp (bilinear por hardware); a referência CPU compara por texel.
//
// Host: Texture2DArray<float> de profundidade (D32/D16), uma fatia por cascata, renderizada com
// cascades[i].viewProj (profundidade convencional: perto = 0, longe = 1). O sampler de comparação deve ser
// LESS_EQUAL (lit se receptor <= armazenado), filtro de comparação linear, endereço clamp.
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
Texture2DArray<float>  CsmShadowMaps : register(t10);
SamplerComparisonState CsmSampler    : register(s1);

float CsmSampleCascade(uint k, float3 worldPos, float3 N) {
    CsmCascade c = Csm.cascades[k];
    float3 p = worldPos + N * (Csm.normalBias * c.texelWorldSize);
    float3 clip = mul(c.viewProj, float4(p, 1.0f)).xyz;
    if (any(abs(clip.xy) > 1.0f) || clip.z > 1.0f) return 1.0f;
    float depth = clip.z - Csm.depthBias / c.depthRange;
    float2 uv = float2(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f);
    float texel = 1.0f / Csm.shadowMapSize;

    float lit = 0.0f;
    if (Csm.pcfTaps >= 9u) {
        [unroll] for (int j = -1; j <= 1; ++j)
            [unroll] for (int i = -1; i <= 1; ++i)
                lit += CsmShadowMaps.SampleCmpLevelZero(CsmSampler, float3(uv + float2(i, j) * texel, k), depth);
        return lit / 9.0f;
    }
    if (Csm.pcfTaps >= 4u) {
        [unroll] for (int j = 0; j < 2; ++j)
            [unroll] for (int i = 0; i < 2; ++i)
                lit += CsmShadowMaps.SampleCmpLevelZero(CsmSampler, float3(uv + (float2(i, j) - 0.5f) * texel, k), depth);
        return lit * 0.25f;
    }
    return CsmShadowMaps.SampleCmpLevelZero(CsmSampler, float3(uv, k), depth);
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
