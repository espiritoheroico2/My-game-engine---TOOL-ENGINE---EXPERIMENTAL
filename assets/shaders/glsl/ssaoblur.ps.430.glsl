#version 430

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
} _102;

uniform sampler2D GBuf;
uniform sampler2D RawAo;

layout(location = 0) out float _entryPointOutput;

void main()
{
    float _355;
    do
    {
        ivec2 _216 = ivec2(gl_FragCoord.xy);
        vec4 _325 = texelFetch(GBuf, ivec3(_216, 0).xy, 0);
        float _326 = _325.w;
        if (_326 <= 0.0)
        {
            _355 = 1.0;
            break;
        }
        float _348;
        float _349;
        _349 = 0.0;
        _348 = 0.0;
        float _352;
        float _353;
        for (int _347 = -2; _347 <= 1; _349 = _352, _348 = _353, _347++)
        {
            _353 = _348;
            _352 = _349;
            float _357;
            float _358;
            for (int _350 = -2; _350 <= 1; _353 = _358, _352 = _357, _350++)
            {
                ivec2 _237 = _216 + ivec2(_350, _347);
                if (any(lessThan(_237, ivec2(0))) || any(greaterThanEqual(_237, ivec2(_102.Size))))
                {
                    _358 = _353;
                    _357 = _352;
                    continue;
                }
                vec4 _340 = texelFetch(GBuf, ivec3(_237, 0).xy, 0);
                float _341 = _340.w;
                if ((_341 <= 0.0) || (abs(_341 - _326) > (_102.BlurDepthRel * _326)))
                {
                    _358 = _353;
                    _357 = _352;
                    continue;
                }
                _358 = _353 + 1.0;
                _357 = _352 + texelFetch(RawAo, ivec3(_237, 0).xy, 0).x;
            }
        }
        _355 = (_348 > 0.0) ? (_349 / _348) : texelFetch(RawAo, ivec3(_216, 0).xy, 0).x;
        break;
    } while(false);
    _entryPointOutput = _355;
}

