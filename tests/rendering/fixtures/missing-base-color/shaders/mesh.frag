#version 450 core

// Compiles and links, but does not provide the uBaseColor uniform the renderer
// needs for materials (shader interface test): the vertex color is written as is.
layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
