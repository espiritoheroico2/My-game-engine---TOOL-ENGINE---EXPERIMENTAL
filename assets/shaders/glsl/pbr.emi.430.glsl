#version 430

layout(location = 8) flat in vec3 i_emissive;
layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    _entryPointOutput = vec4(i_emissive, 1.0);
}

