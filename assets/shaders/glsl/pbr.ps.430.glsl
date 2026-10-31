#version 430

struct GpuLight
{
    vec3 position;
    float range;
    vec3 color;
    float intensity;
    vec3 direction;
    float spotCos;
    uint type;
    uint shadowIndex;
    float spotInnerCos;
    uint pad;
};

struct VSOut
{
    vec4 clip;
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
};

layout(binding = 1, std140) uniform LightBuffer
{
    GpuLight lights[64];
    vec4 ambient;
    uint lightCount;
    layout(row_major) mat4 lightViewProj;
    vec4 shadowParams;
    vec4 shadowInfo;
} _361;

layout(binding = 0) uniform sampler2D ShadowMap;

layout(binding = 0, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    vec3 cameraPos;
    float _pad0;
} _458;

layout(binding = 2, std140) uniform ObjectBuffer
{
    layout(row_major) mat4 world;
    vec4 baseColor;
    float metallic;
    float roughness;
    float receiveShadow;
    float _pad1;
} _461;

layout(location = 0) in vec3 i_worldPos;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
layout(location = 0) out vec4 _entryPointOutput;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

// Espelha ShadowFactor() de lighting.hlsli (mesma matematica; ver comentarios la).
float ShadowFactor(vec3 P, vec3 N, float ndl)
{
    if (_361.shadowParams.z <= 0.0)
    {
        return 1.0;
    }
    float res = _361.shadowInfo.x;
    vec4 c = vec4(P + (N * _361.shadowParams.w), 1.0) * spvWorkaroundRowMajor(_361.lightViewProj);
    vec2 uv = vec2((c.x * 0.5) + 0.5, 1.0 - ((c.y * 0.5) + 0.5));
    if ((((c.z > 1.0) || (c.z < 0.0)) || ((uv.x < 0.0) || (uv.y < 0.0))) || ((uv.x >= 1.0) || (uv.y >= 1.0)))
    {
        return 1.0;
    }
    float bias = _361.shadowParams.x * (1.0 + (2.0 * (1.0 - ndl)));
    ivec2 ip = ivec2(uv * res);
    int k = int(_361.shadowInfo.y);
    int last = int(res) - 1;
    float lit = 0.0;
    float tot = 0.0;
    for (int dy = -k; dy <= k; dy++)
    {
        for (int dx = -k; dx <= k; dx++)
        {
            ivec2 sp = clamp(ip + ivec2(dx, dy), ivec2(0), ivec2(last));
            lit += ((texelFetch(ShadowMap, sp, 0).x >= (c.z - bias)) ? 1.0 : 0.0);
            tot += 1.0;
        }
    }
    return lit / tot;
}

float Attenuation(float dist, float range)
{
    if (dist >= range)
    {
        return 0.0;
    }
    float d2 = max(dist * dist, 9.9999997473787516355514526367188e-05);
    float w = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return (w * w) / d2;
}

float SpotFactor(vec3 lightDir, vec3 fromLight, float innerCos, float outerCos)
{
    float c = dot(normalize(lightDir), normalize(fromLight));
    return clamp((c - outerCos) / max(innerCos - outerCos, 9.9999997473787516355514526367188e-05), 0.0, 1.0);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + ((vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0));
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = ((NdotH * NdotH) * (a2 - 1.0)) + 1.0;
    return a2 / ((3.1415927410125732421875 * d) * d);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / ((NdotV * (1.0 - k)) + k);
    float gl = NdotL / ((NdotL * (1.0 - k)) + k);
    return gv * gl;
}

vec3 EvaluateLight(GpuLight L, vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, float receive)
{
    float att = 1.0;
    vec3 toLight;
    if (L.type == 0u)
    {
        toLight = -normalize(L.direction);
    }
    else
    {
        vec3 d = L.position - P;
        float dist = length(d);
        toLight = d / vec3(max(dist, 9.9999997473787516355514526367188e-05));
        float param = dist;
        float param_1 = L.range;
        att = Attenuation(param, param_1);
        if (L.type == 2u)
        {
            vec3 param_2 = L.direction;
            vec3 param_3 = -toLight;
            float param_4 = L.spotInnerCos;
            float param_5 = L.spotCos;
            att *= SpotFactor(param_2, param_3, param_4, param_5);
        }
    }
    float NdotL = clamp(dot(N, toLight), 0.0, 1.0);
    if ((NdotL <= 0.0) || (att <= 0.0))
    {
        return vec3(0.0);
    }
    vec3 H = normalize(V + toLight);
    float NdotV = max(dot(N, V), 9.9999997473787516355514526367188e-05);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float VdotH = clamp(dot(V, H), 0.0, 1.0);
    vec3 F0 = mix(vec3(0.039999999105930328369140625), albedo, vec3(metallic));
    float param_6 = VdotH;
    vec3 param_7 = F0;
    vec3 F = FresnelSchlick(param_6, param_7);
    float param_8 = NdotH;
    float param_9 = roughness;
    float D = DistributionGGX(param_8, param_9);
    float param_10 = NdotV;
    float param_11 = NdotL;
    float param_12 = roughness;
    float G = GeometrySmith(param_10, param_11, param_12);
    vec3 spec = (F * (D * G)) / vec3(max((4.0 * NdotV) * NdotL, 9.9999997473787516355514526367188e-05));
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    float vis = 1.0;
    if ((L.shadowIndex == 0u) && (receive > 0.5))
    {
        vis = 1.0 - (_361.shadowParams.z * (1.0 - ShadowFactor(P, N, NdotL)));
    }
    return (((((((kd * albedo) / vec3(3.1415927410125732421875)) + spec) * L.color) * L.intensity) * att) * NdotL) * vis;
}

vec3 ShadeAll(vec3 P, vec3 N, vec3 camPos, vec3 albedo, float metallic, float roughness, float receive)
{
    vec3 V = normalize(camPos - P);
    vec3 c = _361.ambient.xyz * albedo;
    GpuLight param;
    for (uint i = 0u; i < _361.lightCount; i++)
    {
        param.position = _361.lights[i].position;
        param.range = _361.lights[i].range;
        param.color = _361.lights[i].color;
        param.intensity = _361.lights[i].intensity;
        param.direction = _361.lights[i].direction;
        param.spotCos = _361.lights[i].spotCos;
        param.type = _361.lights[i].type;
        param.shadowIndex = _361.lights[i].shadowIndex;
        param.spotInnerCos = _361.lights[i].spotInnerCos;
        param.pad = _361.lights[i].pad;
        vec3 param_1 = P;
        vec3 param_2 = N;
        vec3 param_3 = V;
        vec3 param_4 = albedo;
        float param_5 = metallic;
        float param_6 = roughness;
        c += EvaluateLight(param, param_1, param_2, param_3, param_4, param_5, param_6, receive);
    }
    return c;
}

vec3 ToneMapACES(vec3 x)
{
    return clamp((x * ((x * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((x * ((x * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0));
}

vec4 _PSMain(VSOut i)
{
    vec3 param = i.worldPos;
    vec3 param_1 = normalize(i.normal);
    vec3 param_2 = _458.cameraPos;
    vec3 param_3 = _461.baseColor.xyz;
    float param_4 = _461.metallic;
    float param_5 = max(_461.roughness, 0.039999999105930328369140625);
    vec3 color = ShadeAll(param, param_1, param_2, param_3, param_4, param_5, _461.receiveShadow);
    vec3 param_6 = color;
    return vec4(ToneMapACES(param_6), _461.baseColor.w);
}

void main()
{
    VSOut i;
    i.clip = gl_FragCoord;
    i.worldPos = i_worldPos;
    i.normal = i_normal;
    i.uv = i_uv;
    VSOut param = i;
    // OpenGL: o alvo e RGBA8 (UNORM, ver GpuRenderer::colorFmt_), entao a conversao linear->sRGB que Vulkan/D3D fazem
    // no hardware e feita aqui. (Alvo sRGB + gamma no shader dava gamma duplo quando o driver liga FRAMEBUFFER_SRGB.)
    vec4 lit = _PSMain(param);
    _entryPointOutput = vec4(pow(lit.rgb, vec3(1.0 / 2.2)), lit.a);
}

