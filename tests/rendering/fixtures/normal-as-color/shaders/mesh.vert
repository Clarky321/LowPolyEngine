#version 450 core

// Test-only (tests/rendering/renderer_gpu_test.cpp): shows the vertex normal as
// color, normal * 0.5 + 0.5, so frame readback proves normals reach the vertex
// shader. Using aColor's alpha keeps aColor active, as the renderer's
// interface check requires.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;

uniform mat4 uModelViewProjection;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = vec4(aNormal * 0.5 + 0.5, aColor.a);
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
