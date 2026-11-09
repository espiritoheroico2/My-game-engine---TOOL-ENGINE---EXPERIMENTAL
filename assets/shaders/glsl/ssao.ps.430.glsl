#version 430

const uint _124[16] = uint[](0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);

layout(binding = 4, std140) uniform SsaoCB
{
    vec4 Kernel[32];
    float Proj00;
    float Proj11;
    float Proj22;
    float Proj23;
    vec2 InvSize;
    uvec2 Size;
    float Radius;
    float Bias;
    float Power;
    float BlurDepthRel;
    uint KernelSize;
    float BiasPerMeter;
    float FadeStart;
    float FadeEnd;
} _71;

uniform sampler2D GBuf;

layout(location = 0) out float _entryPointOutput;

void main()
{
    float _807;
    do
    {
        ivec2 _478 = ivec2(gl_FragCoord.xy);
        vec4 _691 = texelFetch(GBuf, ivec3(_478, 0).xy, 0);
        float _692 = _691.w;
        if (_692 <= 0.0)
        {
            _807 = 1.0;
            break;
        }
        vec2 _705 = (((vec2(_478) + vec2(0.5)) * _71.InvSize) * 2.0) - vec2(1.0);
        float _725 = -_692;
        vec3 _726 = vec3((_705.x * _692) / _71.Proj00, ((-_705.y) * _692) / _71.Proj11, _725);
        vec3 _501 = normalize(texelFetch(GBuf, ivec3(_478, 0).xy, 0).xyz);
        bvec3 _506 = bvec3(abs(_501.y) < 0.9900000095367431640625);
        vec3 _511 = normalize(cross(vec3(_506.x ? vec3(0.0, 1.0, 0.0).x : vec3(1.0, 0.0, 0.0).x, _506.y ? vec3(0.0, 1.0, 0.0).y : vec3(1.0, 0.0, 0.0).y, _506.z ? vec3(0.0, 1.0, 0.0).z : vec3(1.0, 0.0, 0.0).z), _501));
        vec3 _514 = cross(_501, _511);
        float _741 = float(_124[((_478.y & 3) * 4) + (_478.x & 3)]) * 0.3926990926265716552734375;
        float _518 = cos(_741);
        float _520 = sin(_741);
        vec3 _527 = (_511 * _518) + (_514 * _520);
        vec3 _534 = (_514 * _518) - (_511 * _520);
        float _541 = _71.Bias + (_71.BiasPerMeter * _692);
        float _804;
        _804 = 0.0;
        float _808;
        for (uint _803 = 0u; _803 < _71.KernelSize; _804 = _808, _803++)
        {
            vec3 _571 = _726 + ((((_527 * _71.Kernel[_803].x) + (_534 * _71.Kernel[_803].y)) + (_501 * _71.Kernel[_803].z)) * _71.Radius);
            if (_571.z >= (-9.9999997473787516355514526367188e-05))
            {
                _808 = _804;
                continue;
            }
            vec2 _599 = vec2((((_571.x * _71.Proj00) / (-_571.z)) * 0.5) + 0.5, 0.5 - (((_571.y * _71.Proj11) / (-_571.z)) * 0.5));
            if (any(lessThan(_599, vec2(0.0))) || any(greaterThanEqual(_599, vec2(1.0))))
            {
                _808 = _804;
                continue;
            }
            vec4 _755 = texelFetch(GBuf, ivec3(ivec2(_599 * vec2(_71.Size)), 0).xy, 0);
            float _756 = _755.w;
            if (_756 <= 0.0)
            {
                _808 = _804;
                continue;
            }
            float _621 = -_756;
            float _809;
            if (_621 >= (_571.z + _541))
            {
                float _760 = clamp(_71.Radius / max(abs(_725 - _621), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                _809 = _804 + ((_760 * _760) * (3.0 - (2.0 * _760)));
            }
            else
            {
                _809 = _804;
            }
            _808 = _809;
        }
        float _651 = 1.0 - (_804 / float(_71.KernelSize));
        float _771 = clamp((_692 - _71.FadeStart) / max(_71.FadeEnd - _71.FadeStart, 0.001000000047497451305389404296875), 0.0, 1.0);
        _807 = mix((_71.Power == 1.0) ? _651 : pow(_651, _71.Power), 1.0, (_771 * _771) * (3.0 - (2.0 * _771)));
        break;
    } while(false);
    _entryPointOutput = _807;
}

