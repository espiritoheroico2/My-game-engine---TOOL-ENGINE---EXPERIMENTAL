#version 430

layout(location = 0) in float i_depth;
layout(location = 0) out float _entryPointOutput;

void main()
{
    _entryPointOutput = i_depth;
}

