#version 450 core

// Deliberate syntax error (missing semicolon) for the shader compile-error test.
layout(location = 0) in vec3 aPosition;

void main()
{
    gl_Position = vec4(aPosition, 1.0)
}
