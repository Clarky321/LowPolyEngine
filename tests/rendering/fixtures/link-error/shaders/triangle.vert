#version 450 core

// Attribute locations must match kPositionLocation / kColorLocation in
// engine/src/render/opengl/GlRenderer.cpp.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = vec4(aPosition, 1.0);
}
