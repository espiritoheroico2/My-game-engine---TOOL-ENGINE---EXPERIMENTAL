#version 430
// Escrito a mao: equivale a shadow.hlsl::PSMain (grava o z da luz, 0..1, no alvo R32F).

layout(location = 0) in float vDepth;
layout(location = 0) out vec4 outDepth;

void main()
{
    outDepth = vec4(vDepth, 0.0, 0.0, 0.0);
}
