// Consulta da grade de probes de GI (espelha ProbeGrid::Irradiance em include/LightPrism/Rt/ProbeGrid.hpp).
// NÃO COMPILADO — validar na engine. O passe de ATUALIZAÇÃO (raios por probe com Bvh.hlsli + mistura com histerese) segue
// ProbeGrid::UpdateOne e ainda não tem versão HLSL.
//
// Dados por probe (StructuredBuffer<ProbeData>; a engine reempacota os 27+32 floats do C++ em float4; FP16 reduz a metade):
//   irr[9]  : SH9 de E/pi já convoluído com o cosseno (rgb por coeficiente)
//   m1[16], m2[16] : momentos da distância de acerto, mapa octaédrico 4x4
#ifndef LP_PROBE_GRID_HLSLI
#define LP_PROBE_GRID_HLSLI

struct ProbeData { float4 irr[9]; float m1[16]; float m2[16]; };   // irr.w livre
cbuffer ProbeGridCB : register(b8)
{
    float3 probeOrigin; float probeSpacing;
    uint3 probeDim; uint probePad;
};
StructuredBuffer<ProbeData> g_Probes : register(t8);

float3 ProbeEvalSh(ProbeData p, float3 n)
{
    // mesma base do Ibl.hlsli (ordem Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22)
    float b[9];
    b[0] = 0.282095f; b[1] = 0.488603f * n.y; b[2] = 0.488603f * n.z; b[3] = 0.488603f * n.x;
    b[4] = 1.092548f * n.x * n.y; b[5] = 1.092548f * n.y * n.z; b[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    b[7] = 1.092548f * n.x * n.z; b[8] = 0.546274f * (n.x * n.x - n.y * n.y);
    float3 e = 0;
    [unroll] for (int k = 0; k < 9; ++k) e += p.irr[k].rgb * b[k];
    return max(e, 0.0f);
}

float2 ProbeOctaEncode(float3 d)
{
    float l1 = abs(d.x) + abs(d.y) + abs(d.z);
    float2 uv = float2(d.x, d.z) / l1;
    if (d.y < 0.0f) uv = (1.0f - abs(uv.yx)) * float2(uv.x >= 0 ? 1.0f : -1.0f, uv.y >= 0 ? 1.0f : -1.0f);
    return uv;
}

float3 ProbeIrradiance(float3 pos, float3 n)
{
    float3 q = (pos - probeOrigin) / probeSpacing;
    int3 c0 = clamp(int3(floor(q)), 0, int3(probeDim) - 2);
    float3 f = saturate(q - float3(c0));
    float3 sum = 0; float wsum = 0;
    [unroll] for (int c = 0; c < 8; ++c)
    {
        int3 o = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        int3 ci = c0 + o;
        uint idx = ci.x + probeDim.x * (ci.y + probeDim.y * ci.z);
        ProbeData p = g_Probes[idx];
        float3 pp = probeOrigin + float3(ci) * probeSpacing;
        float tri = (o.x ? f.x : 1 - f.x) * (o.y ? f.y : 1 - f.y) * (o.z ? f.z : 1 - f.z);
        float3 toProbe = pp - pos; float dist = length(toProbe);
        float3 dir = dist > 1e-5f ? toProbe / dist : n;
        float bf = (dot(dir, n) + 1.0f) * 0.5f;
        float w = tri * max(0.2f, bf * bf);
        float2 uv = ProbeOctaEncode(-toProbe / max(dist, 1e-5f)) * 0.5f + 0.5f;
        int cell = min(3, int(uv.x * 4.0f)) + 4 * min(3, int(uv.y * 4.0f));
        float var = max(p.m2[cell] - p.m1[cell] * p.m1[cell], 1e-3f);
        float m1 = p.m1[cell];
        float vis = 1.0f;
        if (dist > m1) { float dd = dist - m1; vis = var / (var + dd * dd); vis = vis * vis * vis; }
        w *= max(vis, 1e-4f);
        w = max(w, tri > 0 ? 1e-7f : 0.0f);
        sum += ProbeEvalSh(p, n) * w; wsum += w;
    }
    return wsum > 1e-9f ? sum / wsum : 0;
}
#endif
