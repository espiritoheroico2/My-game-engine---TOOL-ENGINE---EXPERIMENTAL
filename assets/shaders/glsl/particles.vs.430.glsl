#version 430
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

struct VSIn
{
    vec3 corner;
    uint iid;
};

struct VSOut
{
    vec4 clip;
    vec2 uv;
    vec4 col;
    float depth;
};

struct QuadInstance
{
    vec3 center;
    float size;
    float rot;
    uint color;
    uint material;
    float temp;
};

layout(binding = 0, std430) readonly buffer Quads
{
    QuadInstance _data[];
} Quads_1;

layout(binding = 6, std140) uniform PartBuffer
{
    uvec4 flags;
    vec4 soft;
} _31;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 camPos;
    float pad0;
} _78;

layout(location = 0) in vec3 i_corner;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseInstance gl_BaseInstanceARB
#else
uniform int SPIRV_Cross_BaseInstance;
#endif
layout(location = 0) out vec2 _entryPointOutput_uv;
layout(location = 1) out vec4 _entryPointOutput_col;
layout(location = 2) out float _entryPointOutput_depth;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

VSOut _VSMain(VSIn i)
{
    uint _40 = _31.flags.x + i.iid;
    QuadInstance q;
    q.center = Quads_1._data[_40].center;
    q.size = Quads_1._data[_40].size;
    q.rot = Quads_1._data[_40].rot;
    q.color = Quads_1._data[_40].color;
    q.material = Quads_1._data[_40].material;
    q.temp = Quads_1._data[_40].temp;
    vec3 R = vec3(1.0, 0.0, 0.0);
    vec3 U = vec3(0.0, 1.0, 0.0);
    if (_31.flags.y == 0u)
    {
        R = _78.view[0].xyz;
        U = _78.view[1].xyz;
    }
    float cs = cos(q.rot);
    float sn = sin(q.rot);
    vec2 c = i.corner.xy;
    float lx = (c.x * cs) - (c.y * sn);
    float ly = (c.x * sn) + (c.y * cs);
    vec3 wp = q.center + (((R * lx) + (U * ly)) * (q.size * 0.5));
    VSOut o;
    o.clip = vec4(wp, 1.0) * spvWorkaroundRowMajor(_78.viewProj);
    o.uv = c;
    o.col = vec4(float(q.color & 255u), float((q.color >> uint(8)) & 255u), float((q.color >> uint(16)) & 255u), float((q.color >> uint(24)) & 255u)) / vec4(255.0);
    o.depth = -(vec4(q.center, 1.0) * spvWorkaroundRowMajor(_78.view)).z;
    return o;
}

void main_inner()
{
    VSIn i;
    i.corner = i_corner;
    i.iid = uint((gl_InstanceID + SPIRV_Cross_BaseInstance));
    VSIn param = i;
    VSOut flattenTemp = _VSMain(param);
    gl_Position = flattenTemp.clip;
    _entryPointOutput_uv = flattenTemp.uv;
    _entryPointOutput_col = flattenTemp.col;
    _entryPointOutput_depth = flattenTemp.depth;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
