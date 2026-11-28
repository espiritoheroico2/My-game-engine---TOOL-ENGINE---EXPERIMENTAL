#version 430

struct VSOut
{
    vec4 clip;
};

layout(binding = 0, std140) uniform SkyCB
{
    vec4 Fwd;
    vec4 Right;
    vec4 Up;
    vec4 Solid;
    vec4 Size;
    uvec4 HasA;
    uvec4 HasB;
} _71;

uniform sampler2D Face0;
uniform sampler2D Face1;
uniform sampler2D Face2;
uniform sampler2D Face3;
uniform sampler2D Face4;
uniform sampler2D Face5;

layout(location = 0) out vec4 _entryPointOutput;

float SrgbToLinear1(float c)
{
    return (c <= 0.040449999272823333740234375) ? (c / 12.9200000762939453125) : pow((c + 0.054999999701976776123046875) / 1.05499994754791259765625, 2.400000095367431640625);
}

vec3 SrgbToLinear(vec3 c)
{
    float param = c.x;
    float param_1 = c.y;
    float param_2 = c.z;
    return vec3(SrgbToLinear1(param), SrgbToLinear1(param_1), SrgbToLinear1(param_2));
}

vec4 _PSMain(VSOut i)
{
    float ndcX = ((2.0 * i.clip.x) / _71.Size.x) - 1.0;
    float ndcY = 1.0 - ((2.0 * i.clip.y) / _71.Size.y);
    vec3 d = (_71.Fwd.xyz + (_71.Right.xyz * ((ndcX * _71.Fwd.w) * _71.Right.w))) + (_71.Up.xyz * (ndcY * _71.Fwd.w));
    d = normalize(d);
    float ax = abs(d.x);
    float ay = abs(d.y);
    float az = abs(d.z);
    int face;
    float s;
    float t;
    if ((ax >= ay) && (ax >= az))
    {
        if (d.x >= 0.0)
        {
            face = 0;
            s = (-d.z) / ax;
            t = (-d.y) / ax;
        }
        else
        {
            face = 1;
            s = d.z / ax;
            t = (-d.y) / ax;
        }
    }
    else
    {
        if (ay >= az)
        {
            if (d.y >= 0.0)
            {
                face = 2;
                s = d.x / ay;
                t = d.z / ay;
            }
            else
            {
                face = 3;
                s = d.x / ay;
                t = (-d.z) / ay;
            }
        }
        else
        {
            if (d.z >= 0.0)
            {
                face = 4;
                s = d.x / az;
                t = (-d.y) / az;
            }
            else
            {
                face = 5;
                s = (-d.x) / az;
                t = (-d.y) / az;
            }
        }
    }
    float u = (s + 1.0) * 0.5;
    float v = 1.0 - ((t + 1.0) * 0.5);
    uint has = (face < 4) ? _71.HasA[face] : _71.HasB[face - 4];
    vec3 c = _71.Solid.xyz;
    if (has != 0u)
    {
        if (face == 0)
        {
            uvec2 sizeQueryTemp = uvec2(textureSize(Face0, 0));
            uint w_ = sizeQueryTemp.x;
            uint h_ = sizeQueryTemp.y;
            int x_ = clamp(int(fract(u) * float(w_)), 0, int(w_) - 1);
            int y_ = clamp(int(fract(v) * float(h_)), 0, int(h_) - 1);
            c = texelFetch(Face0, ivec3(x_, y_, 0).xy, ivec3(x_, y_, 0).z).xyz;
        }
        else
        {
            if (face == 1)
            {
                uvec2 sizeQueryTemp_1 = uvec2(textureSize(Face1, 0));
                uint w_1 = sizeQueryTemp_1.x;
                uint h_1 = sizeQueryTemp_1.y;
                int x_1 = clamp(int(fract(u) * float(w_1)), 0, int(w_1) - 1);
                int y_1 = clamp(int(fract(v) * float(h_1)), 0, int(h_1) - 1);
                c = texelFetch(Face1, ivec3(x_1, y_1, 0).xy, ivec3(x_1, y_1, 0).z).xyz;
            }
            else
            {
                if (face == 2)
                {
                    uvec2 sizeQueryTemp_2 = uvec2(textureSize(Face2, 0));
                    uint w_2 = sizeQueryTemp_2.x;
                    uint h_2 = sizeQueryTemp_2.y;
                    int x_2 = clamp(int(fract(u) * float(w_2)), 0, int(w_2) - 1);
                    int y_2 = clamp(int(fract(v) * float(h_2)), 0, int(h_2) - 1);
                    c = texelFetch(Face2, ivec3(x_2, y_2, 0).xy, ivec3(x_2, y_2, 0).z).xyz;
                }
                else
                {
                    if (face == 3)
                    {
                        uvec2 sizeQueryTemp_3 = uvec2(textureSize(Face3, 0));
                        uint w_3 = sizeQueryTemp_3.x;
                        uint h_3 = sizeQueryTemp_3.y;
                        int x_3 = clamp(int(fract(u) * float(w_3)), 0, int(w_3) - 1);
                        int y_3 = clamp(int(fract(v) * float(h_3)), 0, int(h_3) - 1);
                        c = texelFetch(Face3, ivec3(x_3, y_3, 0).xy, ivec3(x_3, y_3, 0).z).xyz;
                    }
                    else
                    {
                        if (face == 4)
                        {
                            uvec2 sizeQueryTemp_4 = uvec2(textureSize(Face4, 0));
                            uint w_4 = sizeQueryTemp_4.x;
                            uint h_4 = sizeQueryTemp_4.y;
                            int x_4 = clamp(int(fract(u) * float(w_4)), 0, int(w_4) - 1);
                            int y_4 = clamp(int(fract(v) * float(h_4)), 0, int(h_4) - 1);
                            c = texelFetch(Face4, ivec3(x_4, y_4, 0).xy, ivec3(x_4, y_4, 0).z).xyz;
                        }
                        else
                        {
                            uvec2 sizeQueryTemp_5 = uvec2(textureSize(Face5, 0));
                            uint w_5 = sizeQueryTemp_5.x;
                            uint h_5 = sizeQueryTemp_5.y;
                            int x_5 = clamp(int(fract(u) * float(w_5)), 0, int(w_5) - 1);
                            int y_5 = clamp(int(fract(v) * float(h_5)), 0, int(h_5) - 1);
                            c = texelFetch(Face5, ivec3(x_5, y_5, 0).xy, ivec3(x_5, y_5, 0).z).xyz;
                        }
                    }
                }
            }
        }
    }
    vec3 param = c;
    return vec4(SrgbToLinear(param), 1.0);
}

void main()
{
    VSOut i;
    i.clip = gl_FragCoord;
    VSOut param = i;
    _entryPointOutput = _PSMain(param);
}

