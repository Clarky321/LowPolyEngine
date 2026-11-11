---
name: engine-dev
description: Plan, implement, debug and test this C++20 low-poly 3D engine, renderer, procedural world generator, Lua integration and editor. Use for engine architecture or code changes in this repository.
---

# Engine development workflow

Use this skill for any non-trivial change to the engine or editor.

## 1. Establish repository state

Follow `docs/DEVELOPMENT_WORKFLOW.md`. Before editing anything:

- The working directory is the canonical checkout (the `LowPolyEngine/` folder with `.git/`), not a folder under `.claude/worktrees/`. Do not create Git worktrees.
- `git branch --show-current` names the milestone branch (`milestone/mN-<name>`), not `main`.
- `git status --short --branch` is clean, and after `git fetch` the branch is level with `origin`. Do not overwrite unrelated user changes.
- `git log --oneline` ends with the previous sub-milestone (or the milestone plan).
- If any of these fails, stop and report instead of working around it.

Then:

- Read `CLAUDE.md`, `AGENTS.md`, and the relevant sections of `docs/ENGINE_PLAN.md`, `docs/TECH_STACK.md`, `docs/PROJECT_STRUCTURE.md` and `docs/TESTING_AND_CI.md`.
- Find the sub-milestone to implement in `docs/milestones/mN-<name>.md`: the one the user named, or, after the user answered "yes" to the previous report's question, the next one in the plan's status table. Its plan section and the recorded decisions are the task; do not ask the user for a separate prompt. **This task implements exactly that one sub-milestone:** not the next one, not parts of it, not code that only a later step needs. If the request names several sub-milestones, or this one cannot be done without another, stop and ask for the plan to be changed first.
- Inspect the existing CMake targets, public headers, call sites and tests before proposing new abstractions.

## 2. Make a small technical plan

Before writing code, state:
1. Scope and out-of-scope items, taken from the milestone plan.
2. Acceptance criteria.
3. Files/modules likely to change.
4. Public interfaces or serialized formats affected (and whether an ADR is needed).
5. Tests and build targets that will prove the change.
6. Visual check: which screenshot or on-screen check proves it, or "no visible change".
7. Risks, compatibility concerns and unresolved decisions.

If implementation reveals that the plan is wrong, stop and explain the new evidence before widening scope.

When the plan stands, set the sub-milestone's status to `in progress` in the milestone plan.

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

Run the checks that `docs/TESTING_AND_CI.md` §8 requires for the kind of change. On Windows, use the wrappers in `scripts/windows/` (they run the same commands as CI):

```powershell
$env:LPE_WARNINGS_AS_ERRORS = "ON"     # read by the presets when build.bat configures
scripts\windows\build.bat debug        # conan install + cmake --preset + cmake --build
scripts\windows\test.bat debug         # headless tests (unit + integration)
scripts\windows\test.bat debug gpu     # GPU tier, when §8 requires it
scripts\windows\format-check.bat
git diff --check
```

When diagnosing a failure, show the underlying commands (`README.md`, "Build and test manually"). Do not fabricate success: a check that did not run is reported as not run.

For a visible change, capture a screenshot with `island-preview --screenshot` and record it as described in `docs/DEVELOPMENT_WORKFLOW.md` §6.

## 6. Commit, push, report, stop

- Make one Conventional Commit for the change, `type(scope): summary (MN.x)`, with the status in the milestone plan still `in progress` (`docs/DEVELOPMENT_WORKFLOW.md` §4–§5).
- Push to the milestone branch and wait for CI on that commit. Fix failures with a new commit; never force-push or amend a pushed commit.
- Only after CI is green, commit the status change alone, `in progress` → `done`, as `docs(milestones): mark MN.x done (MN.x)`. Push it and wait for its CI to be green too.
- Report with the template in `docs/DEVELOPMENT_WORKFLOW.md` §7: what changed and why, public API/data format changes, the exact commands and results, what was not tested (especially GPU-specific behavior), limitations, and the next sub-milestone.
- End the report with one question: "Continue with MN.y — <title>?" (the next sub-milestone in the plan). Then stop.
- On "yes", start exactly that sub-milestone from §1 of this skill. On "no", or any other answer, wait for the next instruction. Each "yes" covers one sub-milestone; never continue past it without a new "yes".
- A decision the plan leaves open is made and recorded as described in `docs/DEVELOPMENT_WORKFLOW.md` §4 ("Decisions the plan leaves open"); stop and ask only if it would change the plan's scope, add a dependency or contradict an accepted ADR.

Do not claim full support for a platform/backend that was only stubbed or compiled conditionally.
