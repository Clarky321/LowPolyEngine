#version 450 core

layout(location = 0) in vec4 vColor;

// Required by the renderer's interface check (material base color).
uniform vec4 uBaseColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor * uBaseColor;
}
