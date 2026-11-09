# Engine development plan

**Status:** proposal / initial architecture baseline  
**Research snapshot:** 8 November 2026  
**Working description:** low-poly 3D engine with a native editor, C++20 runtime and Lua scripting.

## 1. Product vision

Create a focused engine for stylized low-poly 3D games. The engine should make it easy to build colorful environments with readable silhouettes, flat-shaded geometry, vertex-color palettes and selective textures. Visual references include Journey, ABZÛ, Stray, Ori and the Blind Forest, Stardew Valley and The Witness. These references guide mood, color, composition and clarity; they are not feature requirements to clone.

The first deliverable is not a game. It is an **island-generation sandbox** running inside an engine application:

- Starts as a native desktop application.
- Generates several distinct islands from a reproducible seed.
- Uses low-poly terrain geometry, vertex colors and a small number of materials/textures.
- Draws water and simple low-poly cloud formations.
- Has a first-person free-fly camera (WASD + mouse, vertical movement, speed control).
- Allows changing generation parameters and regenerating the scene.
- Can save/load the seed and generation settings.
- Has automated tests for deterministic generation and core data structures.

The next deliverable adds scene editing and authoring. Character/NPC/gameplay content comes later.

## 2. Non-goals for the first vertical slice

Do not build a general-purpose DCC tool, a complete Unity replacement, a full character animation suite, a multiplayer framework or three graphics backends at once. Do not add full physics, audio middleware, networking, quest/inventory systems or a game-specific hero before the core editor/world workflow is stable.

## 3. Architectural principles

1. **C++ owns mechanisms; Lua owns behavior.** Rendering, platform access, resource lifetime, ECS storage, physics and expensive geometry algorithms live in C++. Lua defines gameplay behavior, item/NPC logic, events, quests and optional generator hooks.
2. **Editor/runtime share engine data models.** The editor should author the same scene, entity, component, asset, material and prefab formats consumed by the runtime.
3. **Data is not code.** Materials, biome palettes, scene layout and prefab composition are serialized as versioned data. Lua is used for behavior and deliberate extension points.
4. **Deterministic procedural generation.** A seed plus a versioned parameter set must reproduce the same logical result. The generator must not depend on frame timing, wall-clock time or iteration order of unordered containers.
5. **Renderer backend isolation.** Scene, material and world-generation modules do not include OpenGL/Vulkan/D3D headers.
6. **Build and tests are part of each feature.** A feature is not finished because it compiles on one developer machine.
7. **Prefer a working vertical slice over premature framework design.** Add generality after a second concrete use case appears.

## 4. Proposed runtime architecture

```text
Application
 ├─ Platform (SDL3: window, input, controller, events)
 ├─ Core (lifetime, clock, logging, settings, events)
 ├─ Renderer API / RHI
 │   └─ OpenGL 4.5 backend (first implementation)
 ├─ Scene (world, entity, components, transform, prefab, serialization)
 ├─ Assets (registry, handles, import, cache, virtual paths)
 ├─ WorldGen (noise, island profiles, terrain mesh, biome palette, clouds)
 ├─ Scripting (Lua VM, engine bindings, script lifecycle)
 └─ Optional future systems (physics, audio, animation)

Editor application
 ├─ Editor document/project model
 ├─ Viewport and scene navigation
 ├─ Outliner / hierarchy
 ├─ Inspector / component properties
 ├─ Content browser
 ├─ Console / diagnostics
 ├─ World-generation panel
 └─ Commands, undo/redo, selection and gizmos
```

Keep engine and editor as separate CMake targets and applications. The engine must be usable by a future standalone game executable without linking editor-only code.

## 5. Rendering plan

### Initial OpenGL renderer

- Request an OpenGL 4.5 Core context via SDL3.
- Load GL functions using a generated GLAD 2 loader or an equivalently maintained loader.
- Create renderer-owned abstractions for buffers, vertex/index arrays, textures, shaders, samplers, materials and render targets.
- Implement a small forward renderer: camera, depth testing, back-face culling, configurable winding, directional light, vertex colors and basic textured materials.
- Use flat shading by supplying face normals (duplicated vertices where needed) or another explicit flat-normal strategy. Do not infer flat shading from polygon triangulation accidentally.
- Start with one render pass. Add a render graph only when real pass dependencies make it worthwhile.

### Future backends

Define the minimum RHI surface needed by the actual first renderer. Do not define a large speculative API. Vulkan is the next candidate. D3D12 comes after the project has a clear Windows-specific need or a strong portability requirement. If multi-backend support is a launch requirement, prototype BGFX before writing the renderer: it advertises OpenGL 4.3+, Vulkan, Direct3D 11/12 and Metal. This changes the project from “implement graphics APIs” to “build an engine above a backend library.”

## 6. Procedural island generation

### Generator inputs

- Master seed and generator version.
- Island count and placement bounds.
- Per-island derived seed.
- Shape profile: radius, ellipticity, orientation, coastline roughness.
- Elevation profile: central mass, ridges/peaks, plateau/terrace options.
- Noise parameters: scale, octave count, persistence, lacunarity and domain-warp strength.
- Biome palette and elevation/slope thresholds.
- Mesh resolution or target triangle count.
- Water level and cloud parameters.

### Determinism

PRNG, seed derivation, distributions and floating-point rules are fixed in [ADR 0003](adr/0003-deterministic-generation.md): SplitMix64 for seed mixing, PCG32 for random sequences, project-owned distributions, no `std::hash`/standard distributions. WorldGen is a pure CPU module that outputs mesh/world data; it does not depend on the renderer.

### Baseline algorithm (implement in stages)

1. Derive an independent deterministic seed for each island using a stable hash of `(masterSeed, islandIndex, generatorVersion)`.
2. Generate island centers and overall shapes with a bounded placement algorithm. Keep island spacing and world bounds explicit.
3. Build an island mask using normalized elliptical distance and smooth radial falloff. Add low-frequency shape noise/domain warping to avoid perfectly circular islands.
4. Evaluate elevation from a combination of island mask, fractal Brownian motion (fBm), ridge noise and optional terrace quantization. Clamp/validate final height values.
5. First mesh implementation: a jittered grid with alternating triangle diagonals. It is easy to debug and deterministic. Consider Poisson-disc samples + Delaunay triangulation later if the grid looks too regular.
6. Assign vertex colors from elevation, slope, biome and a deterministic palette. Keep palette data outside shader source.
7. Generate a coastline/shore band and place a water surface at configured sea level.
8. Generate clouds as deterministic clusters of low-poly meshes/instances; drift them using a separate wind/time system. Do not start with expensive volumetric cloud rendering.
9. Store generation parameters and seed in project/scene data. Optionally cache generated mesh data only after profiling shows generation time requires it.

### Water

Begin with a water plane/grid, subtle vertex displacement and a simple stylized shader. Later add depth-based shoreline fade, Fresnel blending, reflection/refraction and underwater color absorption. ThinMatrix's water shader is useful as an algorithm reference because it demonstrates depth sampling, Fresnel-style blending and soft shore edges; do not transplant its old Java/LWJGL pipeline.

### Variation and determinism tests

- Same seed + same version + same settings => same vertex/index output or same stable mesh hash.
- Different seeds => meaningful differences, not merely a color swap.
- No NaN/Inf positions or normals; all indices are in range.
- All generated heights stay within configured bounds.
- Island placement remains inside world bounds and respects minimum spacing within tolerance.
- Zero/negative/out-of-range settings are rejected or clamped by a documented policy.

## 7. Scene, entities and authoring model

Use an ECS or ECS-like component model, with EnTT as the initial candidate. Start with a small component set:

- `TransformComponent`
- `StaticMeshComponent`
- `CameraComponent`
- `LightComponent`
- `MaterialComponent` or material reference on the mesh renderer
- `ScriptComponent`
- `NameComponent` / editor metadata

Use stable UUID-like IDs for saved entities and assets. Runtime EnTT handles are transient. A scene document should contain entities, components, references and version metadata. Prefabs are reusable entity/component compositions with overrides. Decide JSON vs another format by testing human readability, schema migration and dependency availability; do not persist implementation-specific object layout.

The editor should use commands for mutations (create, delete, rename, transform, add/remove component, property change) so undo/redo can be introduced without rewriting all editing code. Selection and viewport picking should identify the stable scene entity/document object, not a raw pointer.

## 8. Lua integration

Use PUC Lua 5.5.x as the first embedded scripting runtime. LuaBridge3 is a candidate wrapper; also evaluate direct Lua C API bindings. Pin one choice after a proof-of-concept and CI build.

C++ exposes a narrow facade such as:

- Read/write supported entity components through checked handles.
- Subscribe to engine events.
- Register/update gameplay systems at documented lifecycle points.
- Read input actions through a gameplay-facing abstraction.
- Spawn/despawn entities through scene commands or a controlled runtime API.
- Access assets by stable resource handles.

Lua owns gameplay-level state machines, NPC/trigger behavior, quests, item definitions, skills, and project-specific rules. The scripting host owns VM lifetime and error reporting. Define reload semantics and entity-lifetime behavior before implementing hot reload. Scripts should not receive unrestricted OS/process access by default.

## 9. Editor plan

Start with Dear ImGui, using its SDL3 platform integration and OpenGL renderer backend. The initial editor is an engine tool, not a full content creation suite.

First panels:

1. Viewport: rendered scene, fly camera, select entity.
2. Hierarchy: list/create/delete/rename scene entities.
3. Inspector: view/edit Transform and basic components.
4. WorldGen: seed, island count, noise, height, biome palette, regenerate.
5. Console: logs, script errors and diagnostics.
6. Project/assets: browse assets and open/save project.

Then add transform gizmos, undo/redo, asset import, prefab editor, brush-based terrain edits and richer materials. Validate Raven in a standalone prototype; only consider replacing ImGui after a short decision record demonstrates a real advantage and the viewport integration works on target platforms.

The engine editor should author characters as entities assembled from components and imported assets. Modeling, UV work, rigging and detailed mesh creation should initially happen in external tools such as Blender; the engine imports those assets, sets up materials/colliders/animation references and attaches Lua behavior. Do not attempt to build Blender inside this engine.

## 10. Milestones and acceptance criteria

### M0 — Architecture and build proof

- Create repository, CMake project, Conan 2 recipe and lockfile strategy.
- Build a small native SDL3 app on Windows.
- Create OpenGL 4.5 context and render a triangle.
- Integrate logging and Catch2/CTest.
- GitHub Actions builds and runs headless unit tests.
- ADRs record SDL3 vs GLFW, renderer strategy, GUI and dependency management.

**Exit:** clean checkout builds with documented commands; CI is green; no hidden IDE-only steps.

### M1 — Engine loop and camera

- Application lifecycle, clock, input state, window resize handling and structured logging.
- First-person free-fly camera and basic debug input.
- Renderer draws a mesh with depth test and vertex colors.

**Exit:** user can fly around a simple static test scene; camera/input math tests pass.

### M2 — Low-poly renderer and materials

- Mesh/material abstractions, vertex/index buffers, shader loading, vertex color and texture sampling.
- Directional light and clear visual style.
- Basic model/texture import proof.

**Exit:** draw multiple flat-shaded meshes and at least one textured material; shader errors are surfaced clearly.

### M3 — Procedural island sandbox

- Deterministic seeded islands with distinct geometry and palettes.
- Water surface and simple cloud clusters.
- WorldGen UI controls and repeatable regeneration. Dear ImGui enters here as a development/debug UI in the sandbox; the full editor is M5.
- Save/load seed and generator parameters as versioned JSON ([ADR 0004](adr/0004-serialization-format.md)).

**Exit:** several visibly different islands are generated; same seed repeats; generation tests pass; free-fly camera works.

### M4 — Scene document and data model

- Entity/component model, stable IDs, scene serialization and prefab concept.
- Asset handles/registry and basic import paths.
- Lua VM and first script component/event binding proof.

**Exit:** save/load a scene without losing entity identity or component values; a Lua script can alter a permitted property and errors are contained.

### M5 — Minimal editor

- Viewport, hierarchy, inspector, project save/load, world generator panel and console.
- Selection/picking and transform editing.
- Undo/redo command layer.

**Exit:** create scene entities, edit transforms/components, regenerate world, save, close and reopen.

### M6 — Content workflow

- Asset browser, material authoring, prefab authoring and import pipeline (glTF 2.0 candidate).
- Character composition from imported model + components + Lua behavior.
- Basic animation integration only when a real character asset is ready.

**Exit:** build a small scene from reusable assets and prefabs without editing C++.

### M7 — Production extensions

- Physics, audio, animation, profiler, packaging and performance work based on actual project needs.
- Evaluate Vulkan backend; then decide on D3D12 vs BGFX based on platform requirements.

**Exit:** separate acceptance criteria must be written for each added subsystem; do not treat this milestone as an automatic commitment to every feature.

## 11. Definition of done for any feature

- It has a documented purpose and acceptance criteria.
- Public interfaces and ownership are clear.
- Tests cover normal, boundary and failure cases appropriate to the feature.
- The affected build target compiles with warnings enabled.
- Relevant CTest tests pass.
- CI covers the relevant platform/compiler path or the missing coverage is explicitly documented.
- Logs/errors are actionable; failure does not silently corrupt scene or asset data.
- The documentation and examples match the implementation.
