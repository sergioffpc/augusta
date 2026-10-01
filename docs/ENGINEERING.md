# Engineering Practices — FPS Simulator Engine

Covers CI/CD, code quality, design philosophy, observability, and
performance. Complements [ARCHITECTURE.md](./ARCHITECTURE.md) (system
design) and [ROADMAP.md](./ROADMAP.md) (milestones).

## Design Philosophy

A short set of principles this project holds itself to, distilled from
the decisions already made in ARCHITECTURE.md:

- **Data-oriented, ECS-first.** Simulation state lives in Flecs worlds,
  not scattered object graphs.
- **Mechanism vs. policy vs. data.** Engine mechanism (C++), game policy
  (sandboxed Lua), and tunable values (config/data) are three distinct
  categories, kept apart so each can change independently.
- **No I/O inside ECS worlds.** Worlds are pure state transformations;
  device input, networking, rendering, and audio live at the boundaries.
- **Determinism is not assumed, it's engineered around.** PhysX doesn't
  guarantee cross-platform determinism (ADR-0004) — the architecture
  corrects for reality (restoring the server's state and replaying from it)
  instead of pretending otherwise.
- **The server is the only source of truth.** Nothing from a client is
  trusted until validated (US-15).
- **Content is signed and verified, not just loaded.** Integrity is
  structural (ADR-0018), not an afterthought.
- **No premature optimization.** Profile first (NVTX ranges in Nsight Systems), then optimize;
  don't build custom allocators or job systems speculatively (ADR-0005,
  ADR-0007 risk notes).
- **Recoverable failures are values, not control flow.** `std::expected`
  for expected failure modes; exceptions only for truly unrecoverable
  startup errors; never exceptions in the per-tick hot path.
- **Prefer proven tools over reinventing them — except where reinventing
  is the point.** Ballistics, the ECS integration, and the network
  protocol are hand-built because that's the learning goal (VISION.md);
  rendering, physics, and transport are not.

## CI/CD

- **Provider:** GitHub Actions — native Windows and Linux runners match
  the client/server platform split exactly.
- **Trigger:** `push` to `main`/`develop`, and `pull_request` targeting
  either; a separate nightly workflow runs on `develop` (ADR-0013). A `changes` job diffs against the base commit first and skips
  build/test/lint entirely when nothing under `src/`, `tests/`,
  `tools/composer/examples/` (the example scenario a test loads),
  `tools/pack/cpp/` (formatted by the `format` job, though CI doesn't
  build it), `cmake/`, `config/` (the example configs a test loads),
  `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, the `third_party`
  submodule pointer, `.clang-format`/`.clang-tidy`, or the workflow file
  itself or the composite actions it shares (`.github/actions/`) changed
  (a docs-only PR shouldn't pay for a full build).
  `concurrency` cancels a still-running run for the same branch/PR when
  a new push arrives, so superseded runs don't keep burning minutes.
- **Pipeline stages:**
  1. `clang-format` check, alone in its own fast job — gates everything
     below (`needs:`), so a formatting slip fails in seconds instead of
     after a full Windows + Linux + sanitizers build
  2. Build + test the client on a Windows runner (MSVC)
  3. Build + test the server on a Linux runner (clang, ADR-0008), plus
     `clang-tidy` (Google style checks profile)
  4. ASan + UBSan test build and a short fuzzing run per target (both
     Linux only), only for `pull_request` runs — skipped on the `push`
     that lands after merge, since the PR already validated it
  - Dependency restore: `vcpkg install` (manifest mode) before the build
    step, both runners. Binary cache via a GitHub Packages NuGet feed on
    Windows (vcpkg's native GitHub-Actions-cache backend was removed
    upstream in 2026). On Linux, where nuget.exe runs under Mono and fails
    certificate checks, it is a files cache in the Actions cache, one entry
    for every Linux job (all clang), saved only when a job built a package
    it didn't restore.
  - Falcor is not built on every run: the `falcor-prebuilt` workflow builds
    it once for each combination of submodule commit, `falcor.patch` and
    Falcor features, and publishes it as an asset of a `falcor-*` release,
    which the Windows builds download at configure time
    (`cmake/FalcorPrebuilt.cmake`). A build with no matching package — a
    pull request that changes Falcor, or an Aftermath-enabled local build —
    builds Falcor from source.
  - The Actions cache (10 GB per repository, least recently used evicted
    first) holds only what a pull request restores from `develop`: the
    vcpkg binaries, packman's downloads for a Falcor built from source,
    sccache objects. The server image's
    Docker layers live in GHCR (`augustad:buildcache`) instead: at several
    GB they would evict the rest, and every pull request would rebuild its
    dependencies from source.
  5. Compile with a strict warning set, treated as errors
  6. the `tools` job, when `tools/` changed: on a Windows runner, build the
     asset cooker's native modules and run its pytest suite, which also
     requires that cooking the example scenario still gives the golden
     packs in `tests/fixtures/example-packs/` byte for byte; the C++ tests
     in 2 and 3 load those same packs — the contract between the Python
     writer and the C++ reader of the pack format (ADR-0013). The golden
     packs are signed with a committed test key; the real release private
     key never touches CI
- **Nightly** (on `develop`): long fuzzing runs, TSan, property-based
  tests at a high case count, and a `llvm-cov` coverage report; a
  failure opens or updates a `nightly-failure` issue (ADR-0013).
- **Not in CI:** profiling (NVTX with Nsight Systems/Graphics, interactive tools, not CI checks),
  micro-benchmarks (run by hand), and NFR-01's tick rate under load
  (checked by hand on the cluster before a release, ADR-0013).
- **Releases:** a separate workflow, triggered only on `v*` tags, builds
  Release-config client/server binaries, runs the tests and the asset
  pipeline check against them, and attaches them to a GitHub Release —
  not run on every push, so cutting a release is a deliberate tag rather
  than automatic.
- **Artifacts/releases:** out of scope for now — CI validates
  build+test+lint only. A publishing pipeline gets built when there's an
  actual release to make.

## Git Workflow

- **Branching model:** Git Flow — `main` (production/release) + `develop`
  (integration), with `feature/*`, `release/*`, `hotfix/*` branches.
- **Tags/releases:** created only when there's an actual release to make
  (e.g., reaching v1) — ROADMAP.md milestones (M0–M6) are internal
  checkpoints, not tagged releases.
- **Pull requests:** used even solo — `feature/*` → `develop` and
  `develop`/`hotfix/*` → `main` go through a PR so CI gates the merge;
  no formal review requirement, self-merge once CI passes.
- **Commit messages:** Conventional Commits, enforced via the local
  `commit-msg` hook (see Code Quality below).

## Deployment & CD

Covers `main` and `develop` only — `feature/*`, `hotfix/*`, and
`release/*` branches are not deployed to k3s at all (see ADR-0026 for
why, and why there's no self-hosted GitHub Actions runner in this
pipeline).

- **Infrastructure:** self-hosted k3s, single node, on the developer's
  own hardware. No cloud provider involved.
- **Isolation:** two fixed, long-lived Kubernetes namespaces —
  `staging` (tracks `main`) and `develop` (tracks `develop`). No
  per-branch/ephemeral namespaces.
- **Container images:** built in CI, pushed to GitHub Container Registry
  (GHCR).
- **Server exposure:** plain Kubernetes `Service` (`NodePort`, port
  auto-assigned by Kubernetes) — no Agones. Agones solves fleet-scale
  dynamic allocation, which this project doesn't need (one server
  instance per environment); revisit only if matchmaking/dynamic
  multi-server allocation is ever needed (Beyond v1).
- **CD mechanism:** pull-based via Flux, running inside the k3s cluster
  and reconciling each branch's `HelmRelease` from Git — nothing outside
  the cluster needs inbound access to the LAN, and no external PR can
  trigger execution on the cluster host, since there's no CI runner in
  this path at all (see ADR-0026).
- **Access:** LAN-only — no public exposure, no VPN/tunnel needed for now.
- **Asset packs:** built and signed manually, separately from the CD
  pipeline (see ADR-0018, CI/CD above). Packs are versioned independently
  of code deploys and can be shared across multiple server
  instances/versions. Stored on a shared `hostPath` persistent volume on
  the k3s node, populated manually after signing, mounted read-only into
  every server pod. Each environment's Helm values specify which
  `packVersion` to load: the folder `<hostPath>/<packVersion>/` holding
  that environment's `server.pack` and the `augusta.pub` key it is signed
  with. The chart writes the server's `augustad.yaml` from its values.

## Developer Environment

- **Model:** a single shared checkout on the Windows filesystem (NTFS) is
  used by both sides — no separate clones.
- **Client↔server local testing:** WSL2's default NAT networking gives the
  WSL VM its own IP, separate from the Windows host's `127.0.0.1` — a
  native Windows `augustac` can't reach a WSL-hosted `augustad` on
  `127.0.0.1` without it (UDP localhost forwarding, unlike TCP's, isn't
  reliable across WSL2 versions). Enable WSL2's mirrored networking mode
  instead, so the WSL VM shares the host's network interfaces (including
  loopback): add to `%UserProfile%\.wslconfig`
  ```ini
  [wsl2]
  networkingMode=mirrored
  ```
  then `wsl --shutdown` and restart WSL. After that, `127.0.0.1:<port>`
  reaches a WSL-hosted `augustad` from a native Windows `augustac`, no
  need to look up the WSL VM's IP. Requires a reasonably recent
  Windows 11 + WSL2 version; confirm with `wsl --version`.
- **Server / shared core (Linux, via WSL2):** develop and build directly
  inside WSL2, accessing the repo via `/mnt/c/...`. No Docker container —
  a `scripts/bootstrap-wsl.sh` setup script installs clang (ADR-0008), CMake, Ninja,
  uv (for yamllint) and standalone yamlfmt, vcpkg, clang-tidy, clang-format,
  gdb, GitHub CLI, kubectl, and helm
  directly into the WSL environment. It requires the Ubuntu release CI's
  runner uses, whose distro packages fix the same LLVM major as CI's. The cross-filesystem access cost
  (`/mnt/c`) is accepted here, since this side has the lighter build
  (no Falcor, D3D12, or Steam Audio).
- **Client (Windows, native):** built and run natively — never
  cross-compiled from WSL/Linux (not viable given Falcor/D3D12/NVIDIA
  SDK's MSVC-specific toolchain assumptions). A
  `scripts/bootstrap-windows.ps1` script (winget-driven) installs Visual
  Studio Build Tools system-wide (default install location) — simpler
  than pinning a project-specific path, at the cost of not being able to
  side-by-side independent Build Tools versions per project — plus the
  Windows SDK, CMake, Ninja, GNU make, vcpkg, Git, uv (for yamllint) and
  standalone yamlfmt, and LLVM's clang-format/clang-tidy (for the hooks below),
  pinned to the LLVM major CI's Ubuntu runner ships so the hooks agree
  with CI's gates.
  (A fully hermetic, registry-free alternative — clang-cl + xwin-extracted SDK/CRT — was
  considered and rejected: Falcor's CMake presets only test/support
  MSVC on Windows, and stacking an unsupported compiler on top of an
  already-unmaintained dependency, ADR-0009, isn't worth the purity.)
- **Asset cooker setup (opt-in):** `tools/pack/scripts/bootstrap-windows.ps1`
  builds the pack environment under a caller-chosen assets root (ADR-0030): a
  uv-managed Python environment with `tools/pack` installed editable, its
  native modules, signing keys and sample authoring content.
- **USD Composer setup (authoring-only, opt-in):**
  `tools/composer/scripts/bootstrap-windows.ps1` builds NVIDIA Omniverse USD Composer
  via kit-app-template and fetches Adobe's USD-Fileformat-plugins under the
  same assets root. These heavier, GPU-dependent tools are deliberately kept
  out of `bootstrap-windows.ps1` and are never linked into shipped binaries
  (ARCHITECTURE.md §2); only content authors need them.
  `meshoptimizer` and DirectXTex are `tools/pack/cpp`'s own
  C++ build dependencies (two small pybind11 modules, no OpenUSD - see
  ADR-0030) — vendored via `vcpkg.json` (ADR-0025) like the rest of the
  codebase, not fetched by this script.
- **Editor experience:** a committed `.vscode/extensions.json` lists
  recommended extensions (C++ tools, CMake Tools, clangd/clang-format,
  EditorConfig, Lua, YAML/Helm, GitHub Actions) — VS Code
  prompts to install these whenever the folder is opened, on either
  side (WSL remote or native Windows), no container required.
- **Dependency hermeticity:** the `vcpkg.json` manifest (ADR-0025) is what
  actually makes dependency acquisition reproducible on both sides —
  not a container.

## Code Quality

- Google C++ Style Guide (ADR-0012), enforced via `clang-format` and
  `clang-tidy`; YAML uses the standalone `yamlfmt` v0.21.0 binary (two-space indentation) and
  `yamllint` 1.37.1. `clang-format` and `yamlfmt` auto-format staged files in
  the local `pre-commit` hook; `clang-tidy` checks changed C++ and `yamllint`
  checks changed YAML in `pre-push`. CI's `format` job runs the formatters in
  check mode and strict YAML lint as the actual gate, since hooks can be
  skipped (`--no-verify`) or missing/mismatched locally. `clang-tidy` needs a
  full `compile_commands.json`, so it stays out of the commit hook and runs on
  changed C++ before push. `make format` applies both formatters,
  `make format-check` checks formatting and YAML lint, `make lint` also runs
  `clang-tidy`, and `make tidy` alone runs `clang-tidy`.
- Strict warnings-as-errors in CI (see CI/CD above).
- ASan/UBSan and fuzzing in CI; TSan nightly given multithreading
  (ADR-0005, ADR-0013).
- **Commit messages:** Conventional Commits format, enforced locally via
  a custom `commit-msg` git hook (a small regex-matching script) — no
  Node.js/`commitlint` dependency, consistent with keeping the toolchain
  to what the project already uses (C++, Lua, Python for asset tooling).
- Testing: GoogleTest, RapidCheck (property-based), libFuzzer, pytest
  (asset cooker), and Google Benchmark (micro-benchmarks); which kind runs
  at which stage is ADR-0013.
- No formal code review process — solo project; CI's build, test, lint,
  and sanitizer gates are the primary quality gate.

## Observability

- **Logging:** Boost.Log (ADR-0036) — mature, no reason to hand-roll one
  given the project's learning focus is elsewhere (ballistics, networking, ECS).
- **Profiling:** NVTX ranges in the code, read with NVIDIA Nsight
  Systems (CPU threads, timeline) and Nsight Graphics (GPU frames,
  D3D12 capture) — the primary tools for inspecting client and server
  performance during development.
- No metrics/telemetry pipeline beyond profiling for v1.
- No server watchdog/health-check for v1 — LAN-only, solo-tested; a
  hang is immediately visible. Revisit if the server is ever deployed
  unattended (see ROADMAP.md, Beyond v1).

## Performance

- **NFR-01** (server tick rate ≥ 60 Hz, see REQUIREMENTS.md) remains the
  only formal, tested performance target.
- **No formal client frame-rate target.** Deliberately not turned into
  an NFR — frame rate is judged subjectively while playing/testing, not
  automated or gated in CI.
- **Memory strategy:** rely on Flecs' and PhysX's built-in allocators for
  v1; no custom arena/pool allocators until profiling shows a
  concrete need.
- Google Benchmark is used for targeted micro-benchmarks of hot-path code
  (e.g., ballistics math, serialization) as needed — not a blanket
  requirement for every function.
