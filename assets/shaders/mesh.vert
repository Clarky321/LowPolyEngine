#version 450 core

// Interface checked by the renderer at startup (engine/src/render/opengl/GlRenderer.cpp):
// vertex inputs aPosition at location 0 and aColor at location 1 (GlMesh.hpp),
// uniform uModelViewProjection. The optional inputs aNormal (location 2) and
// aUv (location 3) may be unused, which the check allows: aNormal is declared
// for that contract but not used yet (lighting comes later in M2). Meshes
// without normals or UVs read (0, 0, 0) and (0, 0).
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec2 aUv;

uniform mat4 uModelViewProjection;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

void main()
{
    vColor = aColor;
    vUv = aUv;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
