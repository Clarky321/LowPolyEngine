@AGENTS.md

# Claude Code — project entry point

Before making architectural or cross-module changes, read:
- `docs/ENGINE_PLAN.md`
- `docs/TECH_STACK.md`
- `docs/PROJECT_STRUCTURE.md`
- `docs/TESTING_AND_CI.md`

For engine implementation tasks, use the `/engine-dev` skill. Treat the roadmap as staged: do not implement future milestones unless explicitly requested, and even then implement exactly one sub-milestone per task (`AGENTS.md` §12, `docs/DEVELOPMENT_WORKFLOW.md`). Keep changes small, buildable, tested, and documented. If the repository state disagrees with the plan, inspect the code and report the discrepancy before changing architecture.
