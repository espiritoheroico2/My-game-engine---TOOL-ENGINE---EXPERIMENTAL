// SSAO: dois kernels. Espelha include/LightPrism/Post/Ssao.hpp (a versão C++ é a referência testada).
//   SsaoMain : AO bruto   (Depth + Normal -> RawAO)       dispatch ceil(w/8) x ceil(h/8)
//   SsaoBlur : bilateral 4x4 (RawAO + Depth -> AO)         mesma dispatch
// Depth: depth buffer reverso-Z da câmera (convertido para distância linear de view aqui).
// Normal: normais em VIEW space (xyz em [-1,1], unitárias).
#define LP_MAX_KERNEL 32

cbuffer SsaoCB : register(b4) {        // 576 bytes (SsaoGPU)
    float4 Kernel[LP_MAX_KERNEL];
    float  Proj00, Proj11, Proj22, Proj23;
    float2 InvSize;
    uint2  Size;
    float  Radius, Bias, Power, BlurDepthRel;
    uint   KernelSize;
    float  BiasPerMeter, FadeStart, FadeEnd;
};

Texture2D<float>        DepthTex  : register(t5);
Texture2D<float4>       NormalTex : register(t6);
Texture2D<float>        RawAoTex  : register(t7);
RWTexture2D<float>      OutAo     : register(u0);

static const float PI = 3.14159265359;

float LinearDepth(uint2 p) {
    float z = DepthTex.Load(int3(p, 0));
    if (z <= 0.0) return 0.0;                      // reverso-Z: 0 = infinito/fundo
    return Proj23 / (z + Proj22);
}

float3 ViewPos(uint2 p, float d) {
    float2 ndc = (float2(p) + 0.5) * InvSize * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc.x * d / Proj00, ndc.y * d / Proj11, -d);
}

float Rotation(uint2 p) {
    static const uint pattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return float(pattern[(p.y & 3) * 4 + (p.x & 3)]) * (2.0 * PI / 16.0);
}

float SmoothStep01(float t) { t = saturate(t); return t * t * (3.0 - 2.0 * t); }

[numthreads(8, 8, 1)]
void SsaoMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Size.x || id.y >= Size.y) return;
    float d = LinearDepth(id.xy);
    if (d <= 0.0) { OutAo[id.xy] = 1.0; return; }

    float3 p = ViewPos(id.xy, d);
    float3 N = normalize(NormalTex.Load(int3(id.xy, 0)).xyz);
    float3 up = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t0 = normalize(cross(up, N));
    float3 b0 = cross(N, t0);
    float rot = Rotation(id.xy);
    float cr = cos(rot), sr = sin(rot);
    float3 T = t0 * cr + b0 * sr;
    float3 B = b0 * cr - t0 * sr;

    float biasEff = Bias + BiasPerMeter * d;
    float occ = 0.0;
    for (uint i = 0; i < KernelSize; ++i) {
        float3 k = Kernel[i].xyz;
        float3 sp = p + (T * k.x + B * k.y + N * k.z) * Radius;
        if (sp.z >= -1e-4) continue;
        float2 uv = float2(sp.x * Proj00 / -sp.z * 0.5 + 0.5, 0.5 - sp.y * Proj11 / -sp.z * 0.5);
        if (any(uv < 0.0) || any(uv >= 1.0)) continue;
        float sd = LinearDepth(uint2(uv * float2(Size)));
        if (sd <= 0.0) continue;
        float sceneZ = -sd;
        if (sceneZ >= sp.z + biasEff)
            occ += SmoothStep01(Radius / max(abs(p.z - sceneZ), 1e-4));
    }
    float ao = 1.0 - occ / float(KernelSize);
    float shaped = Power == 1.0 ? ao : pow(ao, Power);
    float fade = SmoothStep01((d - FadeStart) / max(FadeEnd - FadeStart, 1e-3));
    OutAo[id.xy] = lerp(shaped, 1.0, fade);
}

[numthreads(8, 8, 1)]
void SsaoBlur(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Size.x || id.y >= Size.y) return;
    float d = LinearDepth(id.xy);
    if (d <= 0.0) { OutAo[id.xy] = 1.0; return; }
    float sum = 0.0, wsum = 0.0;
    for (int dy = -2; dy <= 1; ++dy)
        for (int dx = -2; dx <= 1; ++dx) {
            int2 q = int2(id.xy) + int2(dx, dy);
            if (any(q < 0) || any(q >= int2(Size))) continue;
            float dj = LinearDepth(uint2(q));
            if (dj <= 0.0 || abs(dj - d) > BlurDepthRel * d) continue;
            sum += RawAoTex.Load(int3(q, 0));
            wsum += 1.0;
        }
    OutAo[id.xy] = wsum > 0.0 ? sum / wsum : RawAoTex.Load(int3(id.xy, 0));
}
