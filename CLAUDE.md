## Agent skills

### Main flow

Work moves idea → ship along one spine of skills, in order: `/grill-with-docs` →
`/to-spec` → `/to-tickets` → `/implement` → `/code-review` → `/retro`. The user
runs each step; `/code-review` and `/pr` are yours to invoke. When a step ends,
name the next one; `/ask-matt` routes when the next step is unclear.
`/implement` and `/implement-spec` work on a `feature/*` branch off `develop`
and end with a push and a PR into `develop`; `/pr` shapes its body inside
`.github/pull_request_template.md`.

### Engineering flow

Keeps the codebase and issue list healthy and feeds work into the main flow; its
skills are used as needed, in no order. The user runs
`/improve-codebase-architecture` (refactoring candidates), `/triage` (raw issues
into workable ones), `/implement-spec` (a whole spec at once with parallel
subagents, in place of `/implement`), `/wayfinder` (an effort too big for one
session, mapped as decision tickets before `/to-spec`), `/handoff` (carry a
session to a new directory or harness) and `/engineering-review` (a read-only
audit of standing code into a findings report; profile in
`docs/agents/engineering-review.md`). Yours to invoke: `/diagnosing-bugs` for a
hard bug, from a failing repro; `/prototype` for a design question settled only
by running code; `/research` for reading legwork against primary sources;
`/wizard` for setup steps only a human can perform.

### Git flow

Git Flow (`docs/ENGINEERING.md`, Branching model): `feature/*` → `develop`;
`release/*`/`hotfix/*` → `main`, then back into `develop` by
`docs/runbooks/cut-release.md` step 10 — a release is done once `develop`
contains its tag. A stacked PR takes its parent's fixes by merging the parent
branch. Build, test and lint with `make` (`make help`; `PRESET=windows-debug` on
Windows, through `scripts/dev-container.sh` on macOS): `test` is the one target
that builds the tests, and `prepush` runs a push's checks, uncommitted changes
included, before committing. Only CI's `client-x64-windows` job reproduces
Windows-only failures. After a push to a branch with an open PR,
`gh pr checks <pr> --watch`: a red check blocks the next merge onto that branch.
A worktree for parallel work comes and goes through
`scripts/worktree.sh add|remove`, which links the submodules a build needs.

### Issue tracker

Issues are tracked in this repo's GitHub Issues, using the `gh` CLI; a PR into
`develop` closes none of them, so they close by hand once it merges. See
`docs/agents/issue-tracker.md`.

### Triage labels

The five canonical roles under their own names (`needs-triage`, `needs-info`,
`ready-for-agent`, `ready-for-human`, `wontfix`), beside GitHub's `bug` and
`enhancement` as the categories. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout: `GLOSSARY.md` + `docs/adr/` at the repo root. See
`docs/agents/domain.md`.

### Coding standards

Judgment calls for generated C++ code that `clang-format`/`clang-tidy`
(ADR-0012) can't check — single responsibility, KISS, DRY, decision/mechanism
separation, comments, module READMEs, testing, naming, ownership, error
handling. See `docs/agents/coding-standards.md`.

Markdown, Python and shell follow Google's style guides for each; ADR-0012 says
which rules the formatters and linters enforce and which are written by hand.

### Windows debugging tools

Profiling (Nsight Systems), GPU debugging and crash dumps (Nsight Graphics,
Aftermath), and content authoring/physics debugging (Omniverse, OmniPVD): see
`docs/agents/tooling.md`.
