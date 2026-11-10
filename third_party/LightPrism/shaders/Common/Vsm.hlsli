// Variance Shadow Maps (espelha include/LightPrism/Shadow/Vsm.hpp). NÃO COMPILADO — validar na engine.
// Textura de momentos RG16F (R = E[d], G = E[d²]); gere com VsmMomentsFromDepth e filtre com blur separável.
#ifndef LP_VSM_HLSLI
#define LP_VSM_HLSLI

float2 VsmMomentsFromDepth(float d)
{
    float dx = ddx(d), dy = ddy(d);                 // no passe de sombra (pixel shader) ou do depth->momentos com derivadas por vizinhos
    return float2(d, d * d + 0.25f * (dx * dx + dy * dy));
}

float VsmChebyshev(float2 moments, float t, float minVariance, float bleedReduction)
{
    if (t <= moments.x) return 1.0f;
    float var = max(moments.y - moments.x * moments.x, minVariance);
    float d = t - moments.x;
    float p = var / (var + d * d);
    return saturate((p - bleedReduction) / (1.0f - bleedReduction));
}

// `vsm` com sampler linear (o filtro bilinear dos momentos é o que dá a sombra suave).
float VsmVisibility(Texture2DArray<float2> vsm, SamplerState linearClamp, float3 uvCascade, float receiverDepth,
                    float minVariance, float bleedReduction)
{
    return VsmChebyshev(vsm.SampleLevel(linearClamp, uvCascade, 0), receiverDepth, minVariance, bleedReduction);
}
#endif
