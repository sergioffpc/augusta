## Agent skills

### Main flow

Work moves idea → ship along one spine of skills, in order: `/grill-with-docs` →
`/to-spec` → `/to-tickets` → `/implement` → `/code-review` → `/retro`. The user
runs each step; only `/code-review` is yours to invoke. When a step ends, name
the next one.

### Engineering flow

Keeps the codebase and issue list healthy and feeds work into the main flow; its
skills are used as needed, in no order. The user runs
`/improve-codebase-architecture` (refactoring candidates), `/triage` (raw issues
into workable ones) and `/implement-spec` (a whole spec at once with parallel
subagents, in place of `/implement`). Yours to invoke: `/diagnosing-bugs` for a
hard bug, from a failing repro; `/wizard` for setup steps only a human can
perform.

### Git flow

Git Flow (`docs/ENGINEERING.md`, Branching model): `feature/*` → `develop`;
`release/*`/`hotfix/*` → `main`, then back into `develop` by
`docs/runbooks/cut-release.md` step 10 — a release is done once `develop`
contains its tag. A stacked PR takes its parent's fixes by merging the parent
branch. On macOS, build, test and lint through `scripts/dev-container.sh`; only
CI's `client` job reproduces Windows-only failures.

### Issue tracker

Issues are tracked in this repo's GitHub Issues, using the `gh` CLI. See
`docs/agents/issue-tracker.md`.

### Domain docs

Single-context layout: `CONTEXT.md` + `docs/adr/` at the repo root. See
`docs/agents/domain.md`.

### Coding standards

Judgment calls for generated C++ code that `clang-format`/`clang-tidy`
(ADR-0012) can't check — single responsibility, KISS, DRY, decision/mechanism
separation, comments, testing, naming, ownership, error handling. See
`docs/agents/coding-standards.md`.

Markdown, Python and shell follow Google's style guides for each; ADR-0012 says
which rules the formatters and linters enforce and which are written by hand.

### Windows debugging tools

Profiling (Nsight Systems), GPU debugging and crash dumps (Nsight Graphics,
Aftermath), and content authoring/physics debugging (Omniverse, OmniPVD): see
`docs/agents/tooling.md`.
