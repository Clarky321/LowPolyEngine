#version 450 core

// Interface checked by the renderer at startup (engine/src/render/opengl/GlRenderer.cpp):
// uniform uBaseColor, the material's base color (ADR 0010). Output color =
// vertex color x base color; textures and lighting come in later M2 steps.
layout(location = 0) in vec4 vColor;

uniform vec4 uBaseColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor * uBaseColor;
}
