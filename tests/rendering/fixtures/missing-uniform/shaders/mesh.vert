#version 450 core

// Compiles and links, but does not provide the uModelViewProjection uniform the
// renderer needs (shader interface test): positions are passed through as-is.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = vec4(aPosition, 1.0);
}
