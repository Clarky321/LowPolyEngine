#version 450 core

// Compiles and links, but does not provide the uBaseColor uniform the renderer
// needs for materials (shader interface test): the base color is left out.
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

uniform sampler2D uBaseColorTexture;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(uBaseColorTexture, vUv) * vColor;
}
