#version 450 core

// Compiles and links, but has no lighting: neither uNormalMatrix nor the light
// uniforms (shader interface test). These are the shaders of M2.7, so the test
// also shows how a shader from before lighting is reported.
//
// Interface checked by the renderer at startup (engine/src/render/opengl/GlRenderer.cpp):
// uniforms uBaseColor and uBaseColorTexture, the material's base color and
// base color texture (ADR 0010). Untextured materials bind a 1x1 white
// texture, so every draw samples one texture. Output color = texture x vertex
// color x base color; lighting comes in a later M2 step.
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(uBaseColorTexture, vUv) * vColor * uBaseColor;
}
