# Development workflow

How work on this repository is organized: one checkout, one branch per milestone, one sub-milestone per task. The rules apply to people and AI agents alike. `AGENTS.md` §12 summarizes them, `.claude/skills/engine-dev/SKILL.md` turns them into a checklist, and `docs/TESTING_AND_CI.md` §8 lists the checks.

## 1. One checkout, no worktrees

- All work happens in the canonical checkout: the `LowPolyEngine/` folder that contains `.git/`. Branches are changed there with `git switch`.
- Do not create Git worktrees (`git worktree add`, or Claude Code's `.claude/worktrees/`). A second checkout of the same repository has made edits land in the wrong folder and blocked work in the main one. `.claude/worktrees/` is listed in `.gitignore`, so leftovers are never committed.
- Build outputs live in `build/<preset>/` and the local Python tools in `.venv/`; both are ignored by Git.

## 2. Branches

| Branch | Purpose | Created from | Merged into `main` |
|---|---|---|---|
| `main` | Stable integration branch. Every commit builds and passes CI. | — | — |
| `milestone/mN-<name>` | All work of milestone MN, e.g. `milestone/m2-renderer-materials`. | the current `main` | through one pull request, after the user accepts the milestone |

- Sub-milestones are commits on the milestone branch, not branches of their own.
- Nothing is committed directly to `main`.
- Published history is not rewritten: no force-push, and no `git commit --amend` or `git rebase` of pushed commits. A mistake in a pushed commit is fixed by a new commit.
- Destructive commands are not used: `git reset --hard`, `git clean -fd`/`-fdx`, `git checkout -- .` or `git restore .` over changes nobody has reviewed, `git branch -D`, `git worktree remove --force`. If one seems necessary, stop and ask.

## 3. Milestones and sub-milestones

- The roadmap and each milestone's exit criterion are in `docs/ENGINE_PLAN.md` §10.
- Before a milestone's first sub-milestone, its plan is committed on the milestone branch as `docs/milestones/mN-<name>.md`: numbered sub-milestones `MN.1`, `MN.2`, … with goal, scope, out of scope, acceptance criteria, tests and visual check, plus a status table.
- A sub-milestone is the smallest step that can be reviewed on its own: it builds, its tests pass, and its result can be checked by tests and, for visible changes, by a screenshot.

### The one-sub-milestone rule

**One task implements exactly one sub-milestone.** There is no exception inside a task:

- Do not implement two sub-milestones together, even if both are small or touch the same files.
- Do not start the next sub-milestone, and do not add code, stubs, parameters or hooks that only a later step needs.
- Do not pull in work from later milestones.
- If the current step cannot be finished without part of another one, the plan is wrong: stop and report.
- If two steps should land together, the plan is changed first. A separate `docs(milestones)` commit, approved by the user, merges them into one sub-milestone with one ID. Only then is the merged step implemented, as one task.
- A defect found in already committed code that the current step does not need is reported, not fixed in passing.

Part of the same sub-milestone, and therefore allowed in its task: its tests, its documentation, the status update in the milestone plan, and follow-up commits that fix its CI failures or review findings.

Preparation tasks (milestone plans, workflow documents) are not sub-milestones. They are separate `docs` commits on the milestone branch and contain no engine code.

## 4. A sub-milestone task, step by step

A sub-milestone task starts when the user names the sub-milestone, or when the user answers "yes" to the question at the end of the previous report (step 9). The user does not write a separate prompt for each sub-milestone: its section in `docs/milestones/` and the decisions recorded there and in the ADRs are the task description. The steps run in the order of the plan's status table.

1. **Check the state.** In the canonical checkout, `git branch --show-current` names the milestone branch, `git status` is clean, and after `git fetch` the branch is level with `origin`. The history ends with the previous sub-milestone (or the plan). Otherwise stop and report.
2. **Plan.** Read the sub-milestone in `docs/milestones/`. Before writing code, restate its scope, out-of-scope items, acceptance criteria, files, tests and visual check (or "no visible change"). If the repository disagrees with the plan, report the discrepancy first. Then set its status in the plan to `in progress`.
3. **Implement only this sub-milestone** (§3).
4. **Check.** Run the checks for the kind of change, `docs/TESTING_AND_CI.md` §8.
5. **Review the diff** for scope creep, OpenGL/API leakage, missing error handling, missing tests, and generated or personal files.
6. **Commit the change once** (§5): code, tests and documentation, with the status still `in progress`.
7. **Push** to the milestone branch and wait until CI is green for that commit. A failure is fixed by a new commit with the same sub-milestone suffix, pushed and checked the same way.
8. **Mark it done.** Only after CI is green, a separate commit `docs(milestones): mark MN.x done (MN.x)` changes the status line to `done` and nothing else. Push it and wait until its CI is green as well. The status is a commit of its own because `done` means "committed and CI green", which the change's own commit cannot know yet.
9. **Report, ask, stop** (§7). The report ends with one question: whether to continue with the next sub-milestone of the plan, named by ID and title. Then stop and wait.
   - **"Yes"** starts exactly that next sub-milestone as a new task, from step 1, using the plan and the recorded decisions. Do not ask the user to describe it again.
   - **"No"**, or any other answer, means stay stopped until the next instruction.
   - Each "yes" covers one sub-milestone only. The next one never starts without its own "yes".

### Decisions the plan leaves open

If a sub-milestone needs a decision its plan section does not make, choose the option that fits the plan, the accepted ADRs and `AGENTS.md`. Record it in the sub-milestone's section of the plan (and in an ADR where the plan asks for one) in the change commit, and list it in the report. Stop and ask before implementing only if the choice would change the plan's scope, add a dependency the plan does not name, or contradict an accepted ADR.

## 5. Commits

[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/), one logical change per commit:

```text
type(scope): summary in imperative mood (MN.x)
```

- `type`: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci` or `chore`.
- `scope`: the module or area, e.g. `geometry`, `render`, `core`, `app`, `sandbox`, `tests`, `build`, `workflow`, `milestones`.
- The suffix names the sub-milestone, e.g. `feat(geometry): add optional normals and UVs to MeshData (M2.1)`. Preparation commits have no suffix.
- A sub-milestone therefore ends with two commits: the change, and the status commit `docs(milestones): mark MN.x done (MN.x)` made after the change's CI is green (§4). Fix commits for CI failures or review findings come between them.
- The body says what changed, why, and which checks ran.
- `git log --grep "(M2.1)"` lists the commits of a sub-milestone, so the plan does not record hashes.
- M0 and M1 used the shorter `scope: summary (MN.x)` form; the convention above applies from M2 on.

## 6. Screenshots

- A sub-milestone with a visible change records at least one screenshot made by the engine itself (ADR 0006), from the repository root with the Debug build:
  `build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\mN\NNN-name.png`
- Files are numbered per milestone (`001-`, `002-`, …) and never overwritten; a newer state gets a new number.
- Each image gets a row in `docs/screenshots/mN/README.md` with file, commit and what it shows. The README also records the command and the machine (GPU, driver, framebuffer size). See `docs/screenshots/m1/README.md`.
- Screenshots taken with F12 at runtime land in `screenshots/`, which is ignored, and are not committed.

## 7. Report

Every sub-milestone task ends with this report and its closing question (§4, step 9), and then stops:

```text
Milestone:      MN — <milestone title>
Sub-milestone:  MN.x — <title>
Implemented:    what changed and why (behavior, public API, data formats)
Files changed:  list
Build:          exact commands and results (Debug; Release when required)
Tests:          exact commands, test counts and results
GPU:            GPU-tier result, or why it was not required
Formatting:     format-check and git diff --check results
Visual:         what was checked on screen, or "no visible change"
Screenshots:    files added, or "none"
Commits:        hash and subject of the change and of the status commit
Branch:         branch name; pushed or not
CI:             run link and result for each pushed commit
Limitations:    what was not done or not tested
Next:           the next sub-milestone in the plan (not started)

Continue with MN.y — <title>? (yes / no)
```

A check that was not run is reported as not run, never as passed.

## 8. Finishing a milestone

1. The last sub-milestone of each milestone is a final verification: the full check list in Debug and Release including the GPU tier, the screenshots, and the status in `docs/ENGINE_PLAN.md` and `README.md`. It adds no features.
2. A pull request from the milestone branch to `main` describes all sub-milestones and their checks.
3. The user checks the result, including manual checks such as flying the camera, and accepts it. Only then is the pull request merged, with a merge commit as for M0 and M1.
4. The next milestone branch is created from the updated `main`.
