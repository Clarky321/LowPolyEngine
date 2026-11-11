#version 450 core

// Compiles and links, but uses aNormal at location 4 instead of 2, where GlMesh
// puts the normals (shader interface test): the renderer must name the input
// and both locations instead of feeding it the wrong data.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 4) in vec3 aNormal;

uniform mat4 uModelViewProjection;
uniform mat3 uNormalMatrix;

layout(location = 0) out vec4 vColor;
layout(location = 2) out vec3 vNormal;

void main()
{
    vColor = vec4(aColor.rgb * abs(aNormal), aColor.a);
    vNormal = uNormalMatrix * aNormal;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
