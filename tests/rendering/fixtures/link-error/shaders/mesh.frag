#version 450 core

// Compiles on its own, but reads an input the vertex shader never writes,
// which fails at link time (shader link-error test).
in vec3 vNeverWritten;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(vNeverWritten, 1.0);
}
