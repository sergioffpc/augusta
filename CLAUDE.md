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

### Profiling

To use nsys refer to 'C:\Program Files\NVIDIA Corporation\Nsight Systems
2026.5.1\skills\nsight-systems\SKILL.md'

### Graphics/GPU debugging

Nsight Graphics is the replacement for augusta_renderer's former full in-app
debug HUD (only a small FPS/RTT readout, `DebugHud`, remains): frame capture,
draw-call/pixel inspection, and shader debugging for the D3D12 backend. Launch
'C:\Program Files\NVIDIA Corporation\Nsight Graphics
2026.3.1\host\windows-desktop-nomad-x64\ngfx-ui.exe' and attach to (or launch)
`augustac.exe`.

renderer.cpp also requests `enableAftermath` unconditionally on the Falcor
`Device::Desc` - on a GPU crash/TDR this writes a `.nv-gpudmp` crash dump next
to `augustac.exe`, decodable with 'C:\Program Files\NVIDIA Corporation\Nsight
Graphics 2026.3.1\host\windows-desktop-nomad-x64\nv-aftermath-format.exe'.

### Content authoring/physics debugging

NVIDIA Omniverse USD Composer is where the ADR-0015 map-authoring toolchain's
scene assembly and PhysX authoring/debugging happens (colliders, joints, live
simulation) - see ADR-0015 for the full pipeline. OmniPVD (the `omni.physx.pvd`
extension, inside Composer) records/replays a PhysX simulation as USD for
offline inspection - a debugging aid only, produces no artifact that reaches the
runtime pack.
