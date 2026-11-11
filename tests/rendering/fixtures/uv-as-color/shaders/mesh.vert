#version 450 core

// Test-only (tests/rendering/renderer_gpu_test.cpp): shows the vertex UV as
// color (red = U, green = V), so frame readback proves UVs reach the vertex
// shader. Using aColor's alpha keeps aColor active, as the renderer's
// interface check requires; vNormal feeds the required lighting uniforms.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec2 aUv;

uniform mat4 uModelViewProjection;
uniform mat3 uNormalMatrix;

layout(location = 0) out vec4 vColor;
layout(location = 2) out vec3 vNormal;

void main()
{
    vColor = vec4(aUv, 0.0, aColor.a);
    vNormal = uNormalMatrix * aNormal;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
