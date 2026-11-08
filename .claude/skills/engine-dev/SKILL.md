---
name: engine-dev
description: Plan, implement, debug and test this C++20 low-poly 3D engine, renderer, procedural world generator, Lua integration and editor. Use for engine architecture or code changes in this repository.
---

# Engine development workflow

Use this skill for any non-trivial change to the engine or editor.

## 1. Establish repository state

- Inspect `git status --short --branch` and do not overwrite unrelated user changes.
- Read `CLAUDE.md`, `AGENTS.md`, and the relevant sections of `docs/ENGINE_PLAN.md`, `docs/TECH_STACK.md`, `docs/PROJECT_STRUCTURE.md` and `docs/TESTING_AND_CI.md`.
- Inspect the existing CMake targets, public headers, call sites and tests before proposing new abstractions.
- Identify the current milestone. Do not pull future roadmap work into the current task.

## 2. Make a small technical plan

Before cross-module or architectural work, list:
1. Expected behavior and acceptance criteria.
2. Files/modules likely to change.
3. Public interfaces or serialized formats affected.
4. Tests and build targets that will prove the change.
5. Risks, compatibility concerns and unresolved decisions.

If implementation reveals that the plan is wrong, stop and explain the new evidence before widening scope.

## 3. Implement by domain

- Core/platform: keep SDL3 and OS details out of engine-domain APIs.
- Renderer: keep GL objects and enums inside the OpenGL backend; renderer-facing systems use project-owned handles and interfaces.
- World generation: separate parameter generation, height evaluation, mesh construction and rendering. Keep pure generation code independent of OpenGL and SDL.
- Scene/ECS: use stable serialized IDs; ECS runtime handles are not persistent IDs.
- Lua: expose narrow, checked APIs; define object lifetime and error propagation; scripts must not own native renderer resources.
- Editor: editor commands modify document/scene state through engine-facing APIs, not by directly mutating renderer internals.

Do not build abstractions “for later” without a current use case.

## 4. Test as you go

- Add unit tests for pure algorithms and edge cases.
- Add integration tests where subsystem boundaries are involved.
- Keep unit tests headless. GPU tests belong to the optional rendering test tier.
- For procedural generation, cover same-seed determinism, different-seed variation, parameter bounds, finite values, valid mesh indices and degenerate geometry.
- For serialization, cover round-trip, missing/unknown fields and malformed input.
- For Lua bindings, cover valid calls, invalid arguments, runtime errors and destroyed entity handles.

## 5. Build and validate

Use the actual scripts/presets present in the repository. Expected workflow once scaffolded:

```powershell
# Install the locked dependency graph for the selected profile.
conan install . -pr:h=profiles/windows-msvc -pr:b=default --build=missing

# Configure/build with the generated Conan toolchain/presets.
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

If those profile or preset names do not exist yet, inspect the actual files and use the real names; do not fabricate success. Prefer `scripts/windows/build.bat debug` and `scripts/windows/test.bat debug` once those wrappers have been implemented.

## 6. Finish with an evidence-based report

Report:
- What changed and why.
- Public API/data format changes.
- Tests/build commands actually run and their results.
- What was not tested (especially GPU-specific behavior).
- Remaining risks or a clearly scoped next task.

Do not claim full support for a platform/backend that was only stubbed or compiled conditionally.
