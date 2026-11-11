#version 450 core

// Compiles and links, but does not provide the uBaseColorTexture sampler the
// renderer needs for textured materials (shader interface test): the texture
// is left out.
layout(location = 0) in vec4 vColor;

uniform vec4 uBaseColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor * uBaseColor;
}
