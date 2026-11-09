#version 430

layout(binding = 4, std140) uniform BloomCB
{
    vec2 SrcSize;
    vec2 HaloSize;
    vec2 Dir;
    float Threshold;
    float Strength;
} _52;

uniform sampler2D Src;
uniform sampler2D Halo;

layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    ivec2 _125 = ivec2(gl_FragCoord.xy);
    _entryPointOutput = vec4(texelFetch(Src, ivec3(_125, 0).xy, 0).xyz + (texelFetch(Halo, ivec3(min(ivec2(vec2(_125) * (_52.HaloSize / _52.SrcSize)), (ivec2(_52.HaloSize) - ivec2(1))), 0).xy, 0).xyz * _52.Strength), 1.0);
}

