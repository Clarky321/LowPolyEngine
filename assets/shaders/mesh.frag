#version 450 core

// Interface checked by the renderer at startup (engine/src/render/opengl/GlRenderer.cpp):
// uniforms uBaseColor and uBaseColorTexture (the material, ADR 0010) and
// uDirectionToLight, uLightColor and uAmbientColor (the light, ADR 0011).
// Untextured materials bind a 1x1 white texture; unlit draws set white
// ambient and no light color, so the light factor is 1 for them.
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vNormal;

uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;
// World space, unit length: from the surface towards the light.
uniform vec3 uDirectionToLight;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 color = texture(uBaseColorTexture, vUv) * vColor * uBaseColor;

    // Lambert diffuse from the interpolated normal, normalized here; it is the
    // same on a whole face of a flat-shaded mesh. A zero normal (a mesh
    // without normals, drawn unlit) gets no diffuse light instead of NaN.
    float lengthSquared = dot(vNormal, vNormal);
    float diffuse = lengthSquared > 0.0
                        ? max(dot(vNormal, uDirectionToLight), 0.0) * inversesqrt(lengthSquared)
                        : 0.0;
    vec3 light = uAmbientColor + uLightColor * diffuse;

    // Alpha is not lit.
    fragColor = vec4(color.rgb * light, color.a);
}
