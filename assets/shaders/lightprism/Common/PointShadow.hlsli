// Sombras de luzes PONTUAIS e SPOT feitas pela engine (a Light Prism so tem CSM do sol).
// Espelha o caminho da CPU (SoftRenderer::faceVisibility): mesma escolha de face, mesma projecao, mesmo PCF 3x3 e o
// mesmo viés (ao longo da normal e constante em metros). Este arquivo fica em lightprism/Common so por organizacao:
// NAO e um arquivo da biblioteca Light Prism.
//
// Layout: cada luz com sombra tem uma textura R x (6R) (R32F). As 6 faces ficam empilhadas em y (face f = linhas
// [f*R, f*R+R)); spot usa so a face 0. Profundidade = distancia ao longo do eixo da face.
//
// Defina LP_ENABLE_POINT_SHADOWS antes de incluir ClusterLighting.hlsli (como LP_ENABLE_CSM).
#ifndef LP_POINT_SHADOW_HLSLI
#define LP_POINT_SHADOW_HLSLI

#define LP_MAX_POINT_SHADOWS 4

struct PointShadowLight {
    float4 posType;     // xyz = posicao da luz (mesma float da PointLightGPU/SpotLightGPU), w = tipo: 0 ponto, 1 spot
    float4 params;      // x = near da face, y = numero de faces (6 ou 1), z = resolucao R, w = 0
    float4x4 vp[6];     // projecao * view de cada face
};
cbuffer PointShadowCB : register(b7) {
    // escalares (nao uint3): std140 do SPIR-V exige vetores alinhados a 16 bytes
    uint pointShadowCount; uint _pointShadowPad0, _pointShadowPad1, _pointShadowPad2;
    PointShadowLight PointShadows[LP_MAX_POINT_SHADOWS];
};
Texture2D<float> PointShadowMap0 : register(t10);
Texture2D<float> PointShadowMap1 : register(t11);
Texture2D<float> PointShadowMap2 : register(t12);
Texture2D<float> PointShadowMap3 : register(t13);

float PointShadowTexel(uint slot, int2 p) {
    if (slot == 0u) return PointShadowMap0.Load(int3(p, 0));
    if (slot == 1u) return PointShadowMap1.Load(int3(p, 0));
    if (slot == 2u) return PointShadowMap2.Load(int3(p, 0));
    return PointShadowMap3.Load(int3(p, 0));
}

// Visibilidade (0..1) de worldPos pela luz `slot`. Fora da face ou atras do near = iluminado (como a CPU).
float PointShadowVisibility(uint slot, float3 worldPos, float3 N, float receive) {
    if (receive < 0.5) return 1.0;
    PointShadowLight L = PointShadows[slot];
    float3 d = worldPos - L.posType.xyz;
    uint f = 0u;
    if (L.posType.w < 0.5) {
        float3 a = abs(d);
        if (a.x >= a.y && a.x >= a.z) f = d.x > 0.0 ? 0u : 1u;
        else if (a.y >= a.z)          f = d.y > 0.0 ? 2u : 3u;
        else                          f = d.z > 0.0 ? 4u : 5u;
    }
    if (f >= (uint)L.params.y) return 1.0;
    float dist = length(d);
    float3 pb = worldPos + N * (0.01 + 0.002 * dist);
    float4 c = mul(L.vp[f], float4(pb, 1.0));
    if (c.w <= L.params.x) return 1.0;
    float nx = c.x / c.w, ny = c.y / c.w;
    if (abs(nx) > 1.0 || abs(ny) > 1.0) return 1.0;
    int R = (int)L.params.z;
    int cx = clamp((int)round((nx * 0.5 + 0.5) * R - 0.5), 0, R - 1);
    int cy = clamp((int)round((0.5 - ny * 0.5) * R - 0.5), 0, R - 1);
    int y0 = (int)f * R, y1 = y0 + R - 1;  // faixa da face dentro da textura empilhada
    float depth = c.w, bias = 0.02 + 0.0015 * depth;
    float lit = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        int x = clamp(cx + dx, 0, R - 1);
        int y = clamp(cy + dy, 0, R - 1) + y0;
        y = clamp(y, y0, y1);
        lit += PointShadowTexel(slot, int2(x, y)) >= depth - bias ? 1.0 : 0.0;
    }
    return lit / 9.0;
}

// Visibilidade da luz que tem a posicao `lightPos` (a PointLightGPU/SpotLightGPU da cena). Sem sombra = 1.
float PointShadowForLight(float3 lightPos, uint type, float3 worldPos, float3 N, float receive) {
    [loop] for (uint k = 0u; k < pointShadowCount; ++k) {
        if ((uint)PointShadows[k].posType.w != type) continue;
        float3 e = PointShadows[k].posType.xyz - lightPos;
        if (dot(e, e) > 1e-8) continue;
        return PointShadowVisibility(k, worldPos, N, receive);
    }
    return 1.0;
}

#endif
