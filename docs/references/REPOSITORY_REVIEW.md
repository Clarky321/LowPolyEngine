# Review of reference repositories

**Checked:** 8 November 2026. This is a high-level review of public repository pages, README files and selected documentation/shaders. It is not a full source-code audit or local build verification.

## 1. Raven — GUI library

Repository: https://github.com/oscar-soirey/Raven

What looks useful:
- A C GUI library for editor/tool applications, with language bindings including C++ and Lua-related ecosystems.
- A repository layout separating public headers, core, platform, render, docs, examples, tests and tools.
- Designer/theme tools and viewport integration options. The README describes draw callbacks and native child-window handles, enabling an application to host its own OpenGL/Direct3D rendering surface.
- CMake build options for examples, tests and tools.

Risks:
- The README explicitly states that the project is in active development and its public API may change.
- Platform testing/support details need a closer look for the target host OS and desktop environment.
- This review did not clone/build/run Raven, so we cannot yet say that it integrates correctly with this engine's SDL3/OpenGL loop.

Conclusion: run a small integration prototype, but use Dear ImGui for the initial editor baseline. Revisit after viewport embedding, input focus, resize and platform tests pass.

## 2. LowPolyTerrain — procedural terrain reference

Repository: https://github.com/TheThinMatrix/LowPolyTerrain

Public description: demonstration of several techniques for generating and rendering low-poly terrain. It is a small Java/OpenGL tutorial-style repository with very little commit history compared with a production engine.

Useful learning targets:
- terrain sampling and height generation;
- triangulation choices and vertex/normal handling;
- low-poly visual treatment;
- procedural geometry pipeline.

Conclusion: study and reimplement relevant algorithms in C++/GLSL. Do not depend on the Java project as an engine module. Verify exact source-level license obligations before copying any code.

## 3. LowPolyWater — water shader reference

Repository: https://github.com/TheThinMatrix/LowPolyWater

A small Java/LWJGL demonstration. Its water fragment shader is a useful reference for sampling reflection/refraction/depth textures, estimating depth, mixing water colors, applying Fresnel-style blending and softening the shore boundary.

Conclusion: use it to understand the effect, then write a shader that fits the project's own renderer and color direction. Do not carry forward old LWJGL/Java dependencies.

## 4. VoxelCore — engine architecture and world generation

Repository: https://github.com/MihailRis/voxelcore

Useful ideas from the public README and documentation:
- distinct project and content-pack concepts;
- Lua scripting separated from engine internals;
- world generator configuration and script entry points;
- explicit world seed exposed to generator scripts;
- modular documentation around entities, data and world creation.

What not to copy uncritically:
- VoxelCore is a voxel engine. Chunk/voxel storage, meshing and world data layout are not automatically appropriate for a triangle-mesh low-poly island engine.
- The project’s chosen dependencies and APIs are its own design tradeoffs, not a mandatory stack for this engine.

Conclusion: borrow the separation of generator settings, seed, content data and scripts; retain a triangle mesh/height-field pipeline for the first low-poly world generator.

## 5. License and implementation caution

Before copying any source or assets, inspect the current license file, verify the license applies to the exact files used, preserve required notices and document modifications. Prefer algorithmic learning and clean reimplementation over moving chunks of tutorial code into the engine.
