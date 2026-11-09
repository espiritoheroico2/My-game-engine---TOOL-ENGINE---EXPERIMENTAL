#version 430
#if defined(GL_EXT_control_flow_attributes)
#extension GL_EXT_control_flow_attributes : require
#define SPIRV_CROSS_FLATTEN [[flatten]]
#define SPIRV_CROSS_BRANCH [[dont_flatten]]
#define SPIRV_CROSS_UNROLL [[unroll]]
#define SPIRV_CROSS_LOOP [[dont_unroll]]
#else
#define SPIRV_CROSS_FLATTEN
#define SPIRV_CROSS_BRANCH
#define SPIRV_CROSS_UNROLL
#define SPIRV_CROSS_LOOP
#endif

layout(binding = 4, std140) uniform BloomCB
{
    vec2 SrcSize;
    vec2 HaloSize;
    vec2 Dir;
    float Threshold;
    float Strength;
} _45;

uniform sampler2D Src;

layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    ivec2 _174 = ivec2(gl_FragCoord.xy) * ivec2(2);
    ivec2 _179 = ivec2(_45.SrcSize) - ivec2(1);
    int _254;
    vec3 _255;
    _255 = vec3(0.0);
    _254 = 0;
    vec3 _258;
    SPIRV_CROSS_UNROLL
    for (; _254 < 2; _255 = _258, _254++)
    {
        _258 = _255;
        SPIRV_CROSS_UNROLL
        for (int _256 = 0; _256 < 2; )
        {
            vec3 _214 = texelFetch(Src, ivec3(min((_174 + ivec2(_256, _254)), _179), 0).xy, 0).xyz;
            float _246 = dot(_214, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
            _258 += (_214 * (max(_246 - _45.Threshold, 0.0) / max(_246, 9.9999997473787516355514526367188e-05)));
            _256++;
            continue;
        }
    }
    _entryPointOutput = vec4(_255 * 0.25, 1.0);
}

