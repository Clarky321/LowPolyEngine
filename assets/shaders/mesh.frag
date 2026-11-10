#version 450 core

// Vertex color only; lighting and textures arrive with the M2 material model.
layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
