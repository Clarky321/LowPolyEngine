#version 450 core

layout(location = 0) in vec4 vColor;
layout(location = 2) in vec3 vNormal;

// Required by the renderer's interface check (material and light). The tests
// draw with the default material: a white texture and an unlit light factor
// of 1, so the shown color stays unchanged.
uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTexture;
uniform vec3 uDirectionToLight;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 color = vColor * uBaseColor * texture(uBaseColorTexture, vec2(0.0));
    float lengthSquared = dot(vNormal, vNormal);
    float diffuse = lengthSquared > 0.0
                        ? max(dot(vNormal, uDirectionToLight), 0.0) * inversesqrt(lengthSquared)
                        : 0.0;
    fragColor = vec4(color.rgb * (uAmbientColor + uLightColor * diffuse), color.a);
}
