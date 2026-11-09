#version 430

layout(location = 0) in vec3 i_nView;
layout(location = 1) in float i_depthView;
layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    _entryPointOutput = vec4(normalize(i_nView), i_depthView);
}

