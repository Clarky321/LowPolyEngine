# Project-wide engineering instructions

## 1. Project intent

This repository is a custom low-poly 3D engine and editor. It is not a game project and is not intended to become a general-purpose Unity/Unreal competitor. The first vertical slice is a native desktop application that renders deterministic procedural islands, water and low-poly clouds, and provides a first-person free-fly camera. Editing, content authoring and gameplay systems are staged after the rendering/world-generation foundation works.

## 2. Languages and boundaries

- Engine/runtime/editor core: C++20. Do not silently raise the standard to C++23 or require compiler extensions.
- Gameplay and content behavior: Lua. Keep rendering, GPU resource ownership, geometry hot paths, core ECS storage, platform integration and physics internals in C++.
- Configuration and authored content: versioned data files and serialized scenes/prefabs. Lua scripts may extend behavior and generator hooks; they are not the sole storage format for all project content.
- Python is for build orchestration, asset validation/conversion helpers, code generation and developer tooling. It is not a runtime requirement for packaged games.
- Windows command wrappers live in `scripts/windows/` and call the same CMake/Conan commands used by CI.

## 3. Architecture rules

- Keep platform, rendering API, world/scene, scripting and editor concerns separated.
- OpenGL/Vulkan/Direct3D-specific types must not leak into scene, world-generation, scripting or editor domain APIs. Put API-specific code behind the renderer/RHI boundary.
- The first renderer is OpenGL 4.5 Core. Do not add Vulkan or D3D12 as stubs just to claim support. Add a backend only with a compiling implementation and tests/smoke checks.
- SDL3 owns window creation, input/events and controller support. The initial OpenGL context is created through SDL3. Do not combine SDL_GPU with the OpenGL renderer.
- Prefer Conan 2 for third-party packages. Do not add a dependency as a git submodule if it is already reproducibly available through Conan. Pin versions and commit the lockfile when the package graph is stable.
- `glm`, `spdlog`, `EnTT`, Lua, Catch2 and Dear ImGui are approved candidates from the initial plan; they are not automatically approved for every subsystem. Keep dependency count low.
- Do not introduce a generic framework, plugin system, job system, render graph, asset database or custom memory allocator before a concrete feature requires it.
- Use stable project-owned IDs/handles for persisted assets and entities. Do not serialize raw pointers, GPU object IDs or EnTT entity handles as durable identity.
- Generation must be deterministic: the same versioned generator settings and seed must produce the same logical terrain result. Use independent derived random streams for island, vegetation and cloud generation so adding one feature does not reshuffle all other randomness.

## 4. C++ quality

- Prefer RAII, explicit ownership, value types and narrow interfaces.
- Avoid raw owning pointers, global mutable state and hidden singleton dependencies.
- Make thread ownership explicit. GPU/context operations stay on the render/context thread unless a tested design says otherwise.
- Use `std::unique_ptr`/`std::shared_ptr` only where ownership semantics justify them; do not use shared ownership by default.
- Public headers must be self-contained and include what they use. Avoid unnecessary includes and implementation details in public headers.
- Use `const` correctness, `enum class`, `std::span`, `std::optional` and standard library facilities where they improve clarity.
- No exceptions across C ABI or Lua C callbacks. Convert errors to a project error/result type at subsystem boundaries.
- Do not suppress compiler warnings to make a build pass. Fix the issue or explain a narrowly scoped suppression.
- Keep naming consistent: types `PascalCase`, functions/variables `camelCase`, constants project convention, namespaces lowercase.

## 5. Lua boundary and safety

- Expose an intentionally small engine API, not raw C++ internals.
- Never let Lua own GPU resources or directly call OpenGL/Vulkan/Direct3D.
- Validate script inputs and handle runtime errors without crashing the host process.
- Plan for reloadable scripts, but do not hot-reload while objects are in an unsafe partially-updated state.
- Treat project scripts and imported assets as untrusted input when they can come from outside the project. Do not expose unrestricted filesystem, process execution or native module loading by default.
- Keep C++ object lifetime rules explicit for values exposed to Lua. Do not leave dangling userdata references after scene/entity destruction.

## 6. Rendering and low-poly style

- The art direction is low-poly, color-led, stylized 3D. Flat shading and vertex colors are first-class; textures are supported but not required for every surface.
- The renderer must support both vertex colors and textured materials. Do not encode all visual information into a single global palette or hard-code biome colors in shaders.
- Begin with a small forward renderer, directional light and simple material model. Add shadows, post-processing, reflections and advanced lighting incrementally.
- Prefer data-driven material and biome palettes. Keep shader code versioned and validate shader compilation in tests/smoke runs.

## 7. Build and dependencies

- CMake is the source of truth for targets, options and tests. Conan 2 installs dependencies and generates the CMake toolchain/dependency metadata.
- Do not make CMake execute network package installation implicitly. The documented workflow runs `conan install` first, then CMake configure/build.
- Keep the project buildable from a clean checkout with the documented commands.
- Do not commit build outputs, generated IDE files, local Conan caches, credentials, personal paths or machine-specific settings.
- Any new third-party dependency must include: purpose, version, license, platform support and why the standard library/current dependencies are insufficient.

## 8. Testing requirements

- Add or update tests for every new deterministic algorithm, serializer, parser, state transition or bug fix.
- Unit tests must not require a GPU or visible window.
- Renderer/GPU tests are a separate tier. Do not make ordinary CI depend on a developer desktop or an interactive window server.
- At minimum, test procedural generation for repeatability, valid indices, finite vertex data, valid bounds and parameter edge cases.
- Run formatting/static checks and relevant tests before claiming a task is complete.
- Never claim a test/build passed unless you actually ran it; include the exact command and outcome in the report.

## 9. Required implementation workflow

1. Confirm you are in the canonical checkout on the milestone branch with a clean `git status` (§12), then inspect the target module, related interfaces, tests and current docs before editing.
2. Restate the requested scope in one or two sentences and identify the smallest useful change.
3. For cross-module or architectural work, present a short plan before coding. Preserve existing conventions unless a reasoned ADR changes them.
4. Implement a vertical slice, not speculative scaffolding for future milestones.
5. Add/adjust tests and documentation in the same change.
6. Configure/build/test the affected targets. On Windows, prefer the documented `.bat` wrapper; also show the underlying commands when diagnosing failures.
7. Review the diff for accidental scope expansion, API leakage, missing error handling and generated files.
8. Report changed files, behavior, commands executed, test results, and known limitations.

## 10. Documentation and decisions

- Architectural decisions that affect dependencies, public APIs, serialization, renderer backends or module boundaries belong in `docs/adr/` as short ADRs.
- Update `docs/ENGINE_PLAN.md` only when the roadmap or acceptance criteria materially change.
- Keep documentation examples aligned with the actual repository. Mark proposals as proposals until implemented.
- Do not invent benchmark results, platform support, completed features or compatibility claims.

## 11. First milestone guardrail

Until the initial island-preview milestone is accepted, do not implement character controllers, NPC AI, quests, inventory, networking, multiplayer, full physics, animation editor, terrain sculpting, material graph, Vulkan or Direct3D backends. Keep the code ready for extension without implementing these systems prematurely.

## 12. Milestones, branches and task size

The full rules are in `docs/DEVELOPMENT_WORKFLOW.md`. In short:

- Work in the canonical checkout (the `LowPolyEngine/` folder with `.git/`) using ordinary branches. Do not create Git worktrees (`git worktree add`, `.claude/worktrees/`).
- `main` is the stable integration branch. Each milestone has one branch `milestone/mN-<name>`, created from `main` and merged back only through a pull request after the user accepts the milestone.
- Each milestone is split into numbered sub-milestones (`MN.1`, `MN.2`, …) in `docs/milestones/mN-<name>.md`.
- **One task implements exactly one sub-milestone.** Do not implement two together, start the next one, or add code that only a later step needs, however small. If two steps must land together, the plan is changed first in a separate docs commit approved by the user; until then they stay separate tasks.
- A sub-milestone ends with the checks in `docs/TESTING_AND_CI.md` §8, one Conventional Commit `type(scope): summary (MN.x)` for the change (plan status `in progress`), a push to the milestone branch and green CI; then a separate commit `docs(milestones): mark MN.x done (MN.x)`, also pushed with green CI, and the report from `docs/DEVELOPMENT_WORKFLOW.md` §7.
- The report ends by asking whether to continue with the next sub-milestone of the plan. Then stop. On "yes", start exactly that one, using the plan and the recorded decisions as the task; do not ask the user for a separate prompt. On "no", stay stopped until the next instruction. Never start the next sub-milestone without that "yes", and never put two sub-milestones in one commit.
- Do not rewrite published history or discard work: no force-push, no amending or rebasing pushed commits, no `git reset --hard`, `git clean -fd`/`-fdx` or forced worktree removal.
