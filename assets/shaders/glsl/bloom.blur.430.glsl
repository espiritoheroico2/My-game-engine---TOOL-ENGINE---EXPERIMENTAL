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

const float _138[5] = float[](0.227026998996734619140625, 0.19459499418735504150390625, 0.121622003614902496337890625, 0.054053999483585357666015625, 0.01621600054204463958740234375);

layout(binding = 4, std140) uniform BloomCB
{
    vec2 SrcSize;
    vec2 HaloSize;
    vec2 Dir;
    float Threshold;
    float Strength;
} _28;

uniform sampler2D Halo;

layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    ivec2 _180 = ivec2(gl_FragCoord.xy);
    ivec2 _185 = ivec2(_28.HaloSize) - ivec2(1);
    ivec2 _188 = ivec2(_28.Dir);
    vec3 _285;
    _285 = texelFetch(Halo, ivec3(_180, 0).xy, 0).xyz * 0.227026998996734619140625;
    SPIRV_CROSS_UNROLL
    for (int _284 = 1; _284 < 5; )
    {
        _285 += ((texelFetch(Halo, ivec3(clamp(_180 + (_188 * ivec2(_284)), ivec2(0), _185), 0).xy, 0).xyz + texelFetch(Halo, ivec3(clamp(_180 - (_188 * ivec2(_284)), ivec2(0), _185), 0).xy, 0).xyz) * _138[_284]);
        _284++;
        continue;
    }
    _entryPointOutput = vec4(_285, 1.0);
}

