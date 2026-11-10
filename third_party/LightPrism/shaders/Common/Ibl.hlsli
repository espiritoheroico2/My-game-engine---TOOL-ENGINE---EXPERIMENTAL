// IBL: difuso por SH9 + especular split-sum. Espelha include/LightPrism/Ibl/Ibl.hpp (a versão C++ é a referência testada).
//
// Host: TextureCube com mips (mip i = ambiente filtrado com rugosidade perceptual i / specularMaxMip), Texture2D RG16F
// com a LUT da BRDF (x = NoV, y = rugosidade; gerada por BuildBrdfLut) e o cbuffer IBLData preenchido por MakeIblGPU.
// Convenção do cubemap: D3D (+X -X +Y -Y +Z -Z), igual a CubeFaceUVToDir.
#ifndef LP_IBL_HLSLI
#define LP_IBL_HLSLI

struct IblData {                       // 160 bytes (IblGPU)
    float4 sh[9];                      // rgb já multiplicado pelas constantes da base e pelo cosseno
    float  intensity;
    float  specularMaxMip;
    uint   flags;
    float  _pad;
};

cbuffer IblCB : register(b3) { IblData Ibl; };
TextureCube<float3> IblPrefiltered : register(t3);
Texture2D<float2>   IblBrdfLut     : register(t4);
SamplerState        IblSampler     : register(s2);   // linear, clamp

// E(n)/pi: o chamador multiplica por albedo (e por kD).
float3 IblEvaluateIrradiance(float3 n) {
    float3 r = Ibl.sh[0].rgb
             + Ibl.sh[1].rgb * n.y + Ibl.sh[2].rgb * n.z + Ibl.sh[3].rgb * n.x
             + Ibl.sh[4].rgb * (n.x * n.y) + Ibl.sh[5].rgb * (n.y * n.z)
             + Ibl.sh[6].rgb * (3.0 * n.z * n.z - 1.0)
             + Ibl.sh[7].rgb * (n.x * n.z) + Ibl.sh[8].rgb * (n.x * n.x - n.y * n.y);
    return max(r, 0.0);
}

float IblDominantDirFactor(float roughness) {
    float a = roughness * roughness;
    float s = saturate(1.0 - a);
    return s * (sqrt(s) + a);
}

float IblSpecularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// N, V normalizados (V aponta para a câmera). ao = 1 sem oclusão. Retorna difuso + especular indiretos.
float3 IblEvaluate(float3 N, float3 V, float3 albedo, float metallic, float roughness, float ao) {
    float rawNoV = dot(N, V);
    float NoV = max(rawNoV, 1e-4);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float2 ab = IblBrdfLut.SampleLevel(IblSampler, float2(NoV, roughness), 0);
    float3 Fss = F0 * ab.x + ab.y;
    float3 kD = (1.0 - min(Fss, 1.0)) * (1.0 - metallic);

    float3 diffuse = kD * albedo * IblEvaluateIrradiance(N) * (Ibl.intensity * ao);

    float3 R = N * (2.0 * rawNoV) - V;
    R = normalize(lerp(N, R, IblDominantDirFactor(roughness)));
    float3 prefiltered = IblPrefiltered.SampleLevel(IblSampler, R, saturate(roughness) * Ibl.specularMaxMip);
    float3 specular = prefiltered * Fss * (Ibl.intensity * IblSpecularOcclusion(NoV, ao, roughness));
    return diffuse + specular;
}

#endif
