#version 450 core

// Valid on its own; mesh.frag next to it makes linking fail.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uModelViewProjection;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
