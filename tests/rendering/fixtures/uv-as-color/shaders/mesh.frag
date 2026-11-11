#version 450 core

layout(location = 0) in vec4 vColor;

// Required by the renderer's interface check (material base color and base
// color texture). Untextured materials bind a white texture, so sampling any
// coordinate leaves the color unchanged.
uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor * uBaseColor * texture(uBaseColorTexture, vec2(0.0));
}
