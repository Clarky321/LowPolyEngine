# Low-poly engine — Claude Code starter docs

This bundle contains the initial architecture plan and instruction files for a C++20/Lua low-poly 3D engine.

## Included

- `CLAUDE.md` — Claude Code entry point. Imports `AGENTS.md` using `@AGENTS.md`.
- `AGENTS.md` — shared project engineering rules.
- `.claude/skills/engine-dev/SKILL.md` — the `/engine-dev` skill workflow.
- `docs/ENGINE_PLAN.md` — product scope, architecture, world generation plan and milestones.
- `docs/TECH_STACK.md` — technology comparison and recommended stack.
- `docs/PROJECT_STRUCTURE.md` — proposed repository tree and module boundaries.
- `docs/TESTING_AND_CI.md` — quality gates, test layers, Docker and GitHub Actions approach.
- `docs/references/REPOSITORY_REVIEW.md` — notes on Raven, ThinMatrix examples and VoxelCore.
- `docs/adr/0001-initial-technology-stack.md` — proposed first architecture decision record.

## How to use

1. Copy the contents of this directory into the root of a new Git repository.
2. Read `docs/ENGINE_PLAN.md` and confirm the scope/decisions before asking Claude Code to implement anything.
3. Start Claude Code from the repository root so it loads `CLAUDE.md`.
4. Ask Claude to review the plan and produce a small M0 implementation checklist; do not ask it to implement every milestone in one pass.
5. Use `/engine-dev` for implementation/debugging tasks once Claude Code recognizes the skill.

The proposed profiles, presets and scripts are future repository files. The skill explicitly tells Claude to inspect actual filenames rather than assume they exist.

## Important

This bundle contains plans and instruction files, not an implemented engine. Raven integration, the Conan dependency graph, CI workflow and all runtime systems still need to be built and verified in the target repository.
