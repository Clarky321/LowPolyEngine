#version 450 core

// Interface checked by the renderer at startup (engine/src/render/opengl/GlRenderer.cpp):
// vertex inputs aPosition at location 0 and aColor at location 1 (GlMesh.hpp),
// uniform uModelViewProjection.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uModelViewProjection;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
