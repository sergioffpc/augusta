# Engineering Practices — FPS Simulator Engine

Covers CI/CD, code quality, design philosophy, observability, and performance.
Complements [ARCHITECTURE.md](./ARCHITECTURE.md) (system design) and
[ROADMAP.md](./ROADMAP.md) (milestones).

## Design Philosophy

A short set of principles this project holds itself to, distilled from the
decisions already made in ARCHITECTURE.md:

- **Data-oriented, ECS-first.** Simulation state lives in Flecs worlds, not
  scattered object graphs.
- **Mechanism vs. policy vs. data.** Engine mechanism (C++), game policy
  (sandboxed Lua), and tunable values (config/data) are three distinct
  categories, kept apart so each can change independently.
- **No I/O inside ECS worlds.** Worlds are pure state transformations; device
  input, networking, rendering, and audio live at the boundaries.
- **Determinism is not assumed, it's engineered around.** PhysX doesn't
  guarantee cross-platform determinism (ADR-0004) — the architecture corrects
  for reality (restoring the server's state and replaying from it) instead of
  pretending otherwise.
- **The server is the only source of truth.** Nothing from a client is trusted
  until validated (US-15).
- **Content is signed and verified, not just loaded.** Integrity is structural
  (ADR-0018), not an afterthought.
- **No premature optimization.** Profile first (NVTX ranges in Nsight Systems),
  then optimize; don't build custom allocators or job systems speculatively
  (ADR-0005, ADR-0007 risk notes).
- **Recoverable failures are values, not control flow.** `std::expected` for
  expected failure modes; exceptions only for truly unrecoverable startup
  errors; never exceptions in the per-tick hot path.
- **Prefer proven tools over reinventing them — except where reinventing is the
  point.** Ballistics, the ECS integration, and the network protocol are
  hand-built because that's the learning goal (VISION.md); rendering, physics,
  and transport are not.

## CI/CD

- **Provider:** GitHub Actions — native Windows and Linux runners match the
  client/server platform split exactly.
- **Trigger:** `push` to `main`/`develop`, and `pull_request` targeting either;
  a separate nightly workflow runs on `develop` (ADR-0013). A `changes` job
  diffs against the base commit first and skips build/test/lint entirely when
  nothing under `src/`, `tests/`, `tools/replay/` and `tools/swarm/` (the C++
  tools, built and tested with the runtime), `tools/composer/examples/` (the
  example scenario a test loads), `tools/pack/cpp/` (formatted by the `format`
  job, though CI doesn't build it), `cmake/`, `config/` (the example configs a
  test loads), `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, the
  `third_party` submodule pointer, `.clang-format`/`.clang-tidy`, or the
  workflow file itself or the composite actions it shares (`.github/actions/`)
  changed (a docs-only PR shouldn't pay for a full build). `concurrency` cancels
  a still-running run for the same branch/PR when a new push arrives, so
  superseded runs don't keep burning minutes.
- **Pipeline stages:**
    1. `clang-format` check, alone in its own fast job — gates everything below
       (`needs:`), so a formatting slip fails in seconds instead of after a full
       Windows + Linux + sanitizers build
    2. Build + test the client on a Windows runner (MSVC), with the server's
       development-only Windows x64 build (NFR-04)
    3. Build + test the server on a Linux runner (clang, ADR-0008), its
       production platform, Linux x86-64 (NFR-04), plus `clang-tidy` (Google
       style checks profile)
    4. ASan + UBSan test build and a short fuzzing run per target (both Linux
       only), only for `pull_request` runs — skipped on the `push` that lands
       after merge, since the PR already validated it
    5. Compile with a strict warning set, treated as errors
    6. the `tools` job, when `tools/` changed: on a Windows runner, build the
       asset cooker's native modules and run its pytest suite, which also
       requires that cooking the example scenario still gives the golden packs
       in `tests/fixtures/example-packs/` byte for byte; the C++ tests in 2 and
       3 load those same packs — the contract between the Python writer and the
       C++ reader of the pack format (ADR-0013). The golden packs are signed
       with a committed test key; the real release private key never touches CI
    - Dependency restore: `vcpkg install` (manifest mode) before the build step,
      both runners. Binary cache via a GitHub Packages NuGet feed on Windows
      (vcpkg's native GitHub-Actions-cache backend was removed upstream in
      2026). On Linux, where nuget.exe runs under Mono and fails certificate
      checks, it is a files cache in the Actions cache, one entry for every
      Linux job (all clang), saved only when a job built a package it didn't
      restore.
    - Falcor is not built on every run: the `falcor-prebuilt` workflow builds it
      once for each combination of submodule commit, `falcor.patch` and Falcor
      features, and publishes it as an asset of a `falcor-*` release, which the
      Windows builds download at configure time (`cmake/FalcorPrebuilt.cmake`).
      A build with no matching package — a pull request that changes Falcor, or
      an Aftermath-enabled local build — builds Falcor from source.
    - The Actions cache (10 GB per repository, least recently used evicted
      first) holds only what a pull request restores from `develop`: the vcpkg
      binaries, packman's downloads for a Falcor built from source, sccache
      objects. The server image's Docker layers live in GHCR
      (`augustad:buildcache`) instead: at several GB they would evict the rest,
      and every pull request would rebuild its dependencies from source.

- **Nightly** (on `develop`): long fuzzing runs, TSan, property-based tests at a
  high case count, a `llvm-cov` coverage report, and the hot-path
  micro-benchmarks, whose history is kept on the `benchmarks` branch (started by
  the first night if absent) and charted on the documentation site; a failure, a
  benchmark more than twice as slow as the night before among them, opens or
  updates a `nightly-failure` issue (ADR-0013).
- **Not in CI:** profiling (NVTX with Nsight Systems/Graphics, interactive
  tools, not CI checks), and NFR-01's tick rate under load (checked by hand on
  the cluster before a release, ADR-0013).
- **Releases:** a separate workflow, triggered only on `v*` tags, builds
  Release-config client/server binaries, runs the tests and the asset pipeline
  check against them, and attaches them to a GitHub Release, whose notes are
  generated from the commits (Git Workflow below) — not run on every push, so
  cutting a release is a deliberate tag rather than automatic.
- **Documentation site:** the `docs` workflow builds the site (MkDocs Material
  for `docs/`, `README.md` and `CONTEXT.md`, Doxygen for the C++ API) on every
  pull request, and publishes `main`'s on GitHub Pages (ADR-0046). `make docs`
  builds it locally into `build/docs-site/`, with the `uv` and Doxygen both
  bootstraps install.
- **Release signing:** release packs are signed by the developer, on the
  developer's machine, never by a workflow. The release Ed25519 keypair is
  generated offline with `augusta-keygen`. It is distinct from the committed
  test key and from any development key. Its private key is kept off the repo
  and out of every CI secret. The client and server packs of a release come from
  one cook run signed with it, since Join refuses a client pack not cooked with
  the server pack (ADR-0019, ADR-0038). Only the public key, `signing.pub`,
  travels with the packs, named by each executable's config (ADR-0034).
- **Provenance and SBOMs:** what packs get from their signature, the release
  executables and every published server image get from GitHub artifact
  attestations, signed keylessly (Sigstore) by the workflow that built them:
  SLSA build provenance, naming the commit and the workflow run, and an SPDX
  SBOM. Each is attested where it is built, so the image is attested by CI on
  the push that publishes it, and the image a release runs (`sha-<12>` of its
  tag's commit) needs nothing more from the release. An executable's SBOM,
  attached to the Release, lists the vcpkg ports it was built with, taken from
  vcpkg's own per-port SPDX documents (`scripts/vcpkg-sbom.py`), plus the
  submodules it uses, by commit: syft finds nothing in a statically linked C++
  binary. The image's SBOM is syft's, of its Ubuntu packages. Verified with:

    ```sh
    gh attestation verify augustac-windows-x64.exe --repo sergioffpc/augusta
    gh attestation verify augustad-linux-x64 --repo sergioffpc/augusta
    gh attestation verify oci://ghcr.io/sergioffpc/augustad:sha-<12> \
      --repo sergioffpc/augusta
    # The SBOM attestation, rather than the provenance:
    gh attestation verify augustad-linux-x64 --repo sergioffpc/augusta \
      --predicate-type https://spdx.dev/Document/v2.3
    ```

- **Vulnerability scanning:** the server image is scanned by trivy as CI builds
  it, and is published only if clean. The latest release's executables are
  scanned every night by grype, from their SBOMs (`nightly-jobs.yml`'s
  `sbom-scan`), so a CVE published after the release still fails a run. No
  vulnerability database knows vcpkg's purls, so `scripts/sbom-cpes.py` first
  adds each port's NVD CPE, and fails on a port that has neither a CPE nor a
  written reason for having none. Both fail on a high or critical CVE; the image
  only on one with a fix available, the executables on any, since the NVD gives
  most of its ranges with no fixed version.

## Git Workflow

- **Branching model:** Git Flow — `main` (production/release) + `develop`
  (integration), with `feature/*`, `release/*`, `hotfix/*` branches.
- **Tags/releases:** created only when there's an actual release to make (e.g.,
  v1.0.0) — ROADMAP.md milestones (M0–M17) are internal checkpoints, not tagged
  releases.
- **Pull requests:** used even solo — `feature/*` → `develop`,
  `release/*`/`hotfix/*` → `main`, and the same `release/*`/`hotfix/*` branch
  back into `develop`, go through a PR so CI gates the merge; no formal review
  requirement, self-merge once CI passes. Cutting a release step by step is
  [docs/runbooks/cut-release.md](runbooks/cut-release.md).
- **Commit messages:** Conventional Commits, enforced via the local `commit-msg`
  hook (see Code Quality below).
- **Changelog and release notes:** generated from the Conventional Commits by
  git-cliff, never written by hand; `cliff.toml` decides which types are listed
  (`chore`, `ci` and `style` are not). A release's `CHANGELOG.md` section is cut
  with `scripts/changelog.sh` on its `release/*` or `hotfix/*` branch, in a
  `chore(release)` commit so the cut leaves itself out, and the release workflow
  publishes that section as the GitHub Release's notes, refusing a tag that has
  none.

## Deployment & CD

Covers `main` and `develop` only — `feature/*`, `hotfix/*`, and `release/*`
branches are not deployed to k3s at all (see ADR-0026 for why, and why there's
no self-hosted GitHub Actions runner in this pipeline).

- **Infrastructure:** self-hosted k3s, single node, on the developer's own
  hardware. No cloud provider involved.
- **Isolation:** two fixed, long-lived Kubernetes namespaces — `staging` (tracks
  `main`) and `develop` (tracks `develop`). No per-branch/ephemeral namespaces.
- **Container images:** built in CI, pushed to GitHub Container Registry (GHCR).
- **Server exposure:** plain Kubernetes `Service` (`NodePort`) — no Agones.
  Agones solves fleet-scale dynamic allocation, which this project doesn't need
  (one server instance per scenario per environment, each fixed in Git,
  ADR-0026); revisit only if matchmaking/dynamic multi-server allocation is ever
  needed. Each server pins its node port, so LAN clients keep one address, from
  its environment's own range so `develop` and `staging` never collide on the
  shared node: `develop` 30700-30799, `staging` 30800-30899. The chart refuses a
  server without a node port in its range.
- **CD mechanism:** pull-based via Flux, running inside the k3s cluster and
  reconciling each branch's `HelmRelease` from Git — nothing outside the cluster
  needs inbound access to the LAN, and no external PR can trigger execution on
  the cluster host, since there's no CI runner in this path at all (see
  ADR-0026).
- **Access:** LAN-only — no public exposure, no VPN/tunnel needed for now.
- **Asset packs:** built and signed manually, separately from the CD pipeline
  (see ADR-0018, CI/CD above). Packs are versioned independently of code deploys
  and can be shared across multiple server instances/versions. Stored on a
  shared `hostPath` persistent volume on the k3s node, which `augusta-publish`
  fills after signing (ADR-0026): `<hostPath>/<scenario>/<packVersion>/` holds a
  scenario's `server.pack` and the `signing.pub` key it is signed with,
  `<packVersion>` being the first 12 hex characters of the server pack's BLAKE3
  hash. A published folder is never rewritten. Each environment's Helm values
  list its servers, one per scenario, each naming the `packVersion` it runs; a
  server mounts only that folder, read-only. The chart writes each server's
  `augustad.yaml` from its values.
- **Crash dumps:** a crashing server logs its stack and leaves a kernel core
  dump. The k3s node must run `systemd-coredump` as its core handler, which
  keeps the dump (`coredumpctl`). A cluster core is read with the debug info CI
  publishes beside each image as its `sha-<12>-debuginfo` tag; a release's
  `augustad-linux-x64.debug` reads only that release binary, which no image runs
  (ADR-0047).

## Developer Environment

- **Model:** a single shared checkout on the Windows filesystem (NTFS) is used
  by both sides — no separate clones.
- **Client↔server local testing:** the dev container publishes `augustad`'s
  default listen port (UDP 27015) on the host, so a native Windows `augustac`
  reaches a container-hosted `augustad` at `127.0.0.1:27015`. Published rather
  than forwarded: VS Code's port forwarding is TCP-only.
- **Server / shared core (Linux, dev container or host):** `.devcontainer/`
  gives this side as a container, for VS Code or Codespaces, without mutating a
  host, and `scripts/bootstrap.sh` installs the same on an Ubuntu 26.04 host
  (its Linux half, `scripts/bootstrap/linux.sh`; the image hashes only the two,
  so a Windows-only change doesn't rebuild it). The script is the one recipe for
  it: the image runs its `toolchain` step, the container's post-create its
  `checkout` step (submodules, vcpkg, hooks), and a host both. It installs CI's
  runner Ubuntu release's packages, whose LLVM major is CI's, with the toolchain
  `.github/actions/setup-linux-build` installs (kept in step with it by hand),
  clang (ADR-0008), CMake, Ninja, vcpkg, clang-tidy, clang-format, gdb, GitHub
  CLI, kubectl, helm, Doxygen, the hooks' formatters and linters (uv for
  yamllint, ruff, shfmt, shellcheck, actionlint, gersemi, Prettier and
  pymarkdown, standalone yamlfmt, StyLua, luacheck and taplo, at CI's pinned
  versions), and CI's Linux vcpkg binary cache configuration (a files provider
  in the checkout's `.vcpkg-bincache`). The image builds for the host's
  architecture (amd64 or arm64) rather than emulating CI's amd64. sccache's
  cache lives in a volume shared by every container of the repository; the build
  trees in a volume per container, so they never collide with a Windows build of
  the same checkout. One recipe, not a container beside a separate host
  bootstrap: two recipes for the same toolchain drift apart. The client has no
  container equivalent (see below).
- **macOS (through the dev container):** nothing builds natively on macOS (the
  presets are Linux's and Windows'), and nothing is installed there but git, Git
  LFS and Docker or Podman. `scripts/dev-container.sh [command]` runs a command
  (a shell by default) in the dev container's image from a terminal: one
  container per checkout, kept running, with the checkout mounted at its host
  path so a git worktree's `.git` resolves inside too, a `build/` volume of its
  own, and sccache's and vcpkg's caches in volumes every checkout shares.
  `scripts/dev-container.sh make configure PRESET=linux-debug` gives the hooks
  the compile commands clang-tidy needs. On macOS the `pre-commit` and
  `pre-push` hooks run their formatters and linters through it; git itself
  (identity, signing, credentials, LFS) stays on the host. A second bootstrap of
  the hooks' tools for macOS would drift from the container's, as two recipes
  for one toolchain do.
- **Client (Windows, native):** built and run natively — never cross-compiled
  from Linux (not viable given Falcor/D3D12/NVIDIA SDK's MSVC-specific toolchain
  assumptions). The same `scripts/bootstrap.sh`, run in Git Bash as
  Administrator (its Windows half, `scripts/bootstrap/windows.sh`,
  winget-driven; one entry point and one `checkout` step and set of pinned
  formatter versions for both platforms, and no PowerShell: Git for Windows is
  the one prerequisite, which cloning needs anyway) installs Visual Studio Build
  Tools system-wide (default install location) — simpler than pinning a
  project-specific path, at the cost of not being able to side-by-side
  independent Build Tools versions per project — plus the Windows SDK, CMake,
  Ninja, GNU make, vcpkg, Git, uv (for yamllint and the formatters and linters
  uv runs, below), standalone yamlfmt, StyLua, luacheck and taplo, and LLVM's
  clang-format/clang-tidy (for the hooks below), pinned to the LLVM major CI's
  Ubuntu runner ships so the hooks agree with CI's gates. (A fully hermetic,
  registry-free alternative — clang-cl + xwin-extracted SDK/CRT — was considered
  and rejected: Falcor's CMake presets only test/support MSVC on Windows, and
  stacking an unsupported compiler on top of an already-unmaintained dependency,
  ADR-0009, isn't worth the purity.)
- **Asset cooker setup (opt-in):** `tools/pack/scripts/bootstrap.sh` (Windows in
  Git Bash, or Linux - the platforms vcpkg's DirectXTex port, which the cooker's
  `_textconv` wraps, builds for) builds the pack environment under a
  caller-chosen assets root (ADR-0030): a uv-managed Python environment with
  `tools/pack` installed editable, its native modules, signing keys and sample
  authoring content.
- **USD Composer setup (authoring-only, opt-in):**
  `tools/composer/scripts/bootstrap.sh` (Windows in Git Bash, or Linux) builds
  NVIDIA Omniverse USD Composer via kit-app-template and fetches Adobe's
  USD-Fileformat-plugins under the same assets root. These heavier,
  GPU-dependent tools are deliberately kept out of `scripts/bootstrap.sh` and
  are never linked into shipped binaries (ARCHITECTURE.md §2); only content
  authors need them. `meshoptimizer` and DirectXTex are `tools/pack/cpp`'s own
  C++ build dependencies (two small pybind11 modules, no OpenUSD - see ADR-0030)
  — vendored via `vcpkg.json` (ADR-0025) like the rest of the codebase, not
  fetched by this script.
- **Editor experience:** a committed `.vscode/extensions.json` lists recommended
  extensions (C++ tools, CMake Tools, clangd/clang-format, EditorConfig, Lua,
  YAML/Helm, GitHub Actions) — VS Code prompts to install these whenever the
  folder is opened, on either side (dev container or native Windows); the dev
  container installs the Linux-relevant subset itself.
- **Dependency hermeticity:** the `vcpkg.json` manifest (ADR-0025) is what
  actually makes dependency acquisition reproducible on both sides — not a
  container.

## Code Quality

- Google C++ Style Guide (ADR-0012), enforced via `clang-format` and
  `clang-tidy`; YAML uses the standalone `yamlfmt` v0.21.0 binary (two-space
  indentation) and `yamllint` 1.37.1. `clang-format` and `yamlfmt` auto-format
  staged files in the local `pre-commit` hook; `clang-tidy` checks changed C++
  and `yamllint` checks changed YAML in `pre-push`. CI's `format` job runs the
  formatters in check mode and strict YAML lint as the actual gate, since hooks
  can be skipped (`--no-verify`) or missing/mismatched locally. `clang-tidy`
  needs a full `compile_commands.json`, so it stays out of the commit hook and
  runs on changed C++ before push. `make format` applies the formatters,
  `make format-check` checks formatting and every linter but `clang-tidy`,
  `make lint` also runs `clang-tidy`, and `make tidy` alone runs `clang-tidy`.
- A scenario's Lua scripts (ADR-0022, ADR-0039) are formatted by StyLua 2.5.2
  (`stylua.toml`: two-space indentation, 120 columns, as the C++) and linted by
  luacheck 1.2.0 (`.luacheckrc`), whose standard library is the engine's Lua
  sandbox written out rather than a stock Lua's: what the sandbox takes out
  (`print`, `pcall`, `io`, `os`, `math.random`...) is a warning, and what the
  engine gives a script is known only where it is given - `server.tick_rate_hz`
  to a Parameters script, each Game policy hook to the rules. StyLua formats
  staged scripts in `pre-commit`, luacheck checks changed ones in `pre-push`,
  and CI's `format` job runs both in check mode.
- TOML files are formatted and linted by taplo 0.10.0 (`.taplo.toml`: the same
  width and indent, arrays kept one entry a line where written so), which is
  also the Even Better TOML extension's engine: it formats staged files in
  `pre-commit`, lints changed ones in `pre-push`, and CI's `format` job runs
  both in check mode.
- Python, shell, the workflows, CMake and Markdown each have a formatter, a
  linter or both, every one run by uv at a pinned version (`uv tool run`), so
  nothing is installed for them but uv. Python follows the Google Python Style
  Guide (ADR-0012): ruff 0.16.10 formats and lints (`ruff.toml`: 80 columns; the
  guide's checks - pylint's, naming, Google-convention docstrings, one import
  per line, no relative imports - and bugbear, pyupgrade and simplify). Shell
  scripts and the git hooks follow the Google Shell Style Guide (ADR-0012), in
  Bash: shfmt 4.2.0 formats (by `.editorconfig`: two-space indent, indented
  `case` patterns, a continued `|` or `&&` starting the next line) and
  shellcheck 0.11.0 lints (`.shellcheckrc`: the guide's optional checks,
  `[[ ]]`, braced and quoted expansions). The workflows: actionlint 1.7.12
  (`.github/actionlint.yaml`), with shellcheck on their `run:` scripts. CMake:
  gersemi 0.29.2 formats (`.gersemirc`: 120 columns, two-space indent, the
  project's own functions read from `cmake/`). Markdown follows the Google
  Markdown style guide (ADR-0012): Prettier 3.9.9 formats (`.prettierrc.yaml`),
  with Node run from its PyPI wheel: paragraphs wrapped at 80 columns, a nested
  list or a block in a list item indented 4 spaces (as the guide asks and MkDocs
  needs), and code blocks left as written. pymarkdown 0.9.40 lints with
  markdownlint's rules (`.pymarkdown.json`) set to the guide (80 columns but for
  headings, tables, code blocks and a long URL; ATX headings; fenced code
  blocks; no trailing whitespace), less what the guide leaves to the writer
  (ordered-list numbering, emphasis as a heading) and what Prettier decides
  (blank lines around lists, table alignment). The repository has no PowerShell
  scripts: its scripts are Bash, and the one Windows-native helper,
  `scripts/vcenv.cmd`, is a batch file. The formatters run on staged files in
  `pre-commit`, the linters on changed files in `pre-push`, and CI's `format`
  job runs all of them in check mode.
- JSON has no tool of its own: `CMakePresets.json` and `vcpkg.json` are
  validated by CMake and vcpkg on every configure, and the `.vscode` files are
  the editor's, formatted by it on save.
- Strict warnings-as-errors in CI (see CI/CD above).
- ASan/UBSan and fuzzing in CI; TSan nightly given multithreading (ADR-0005,
  ADR-0013).
- **Commit messages:** Conventional Commits format, enforced locally via a
  custom `commit-msg` git hook (a small regex-matching script) — no
  Node.js/`commitlint` dependency, consistent with keeping the toolchain to what
  the project already uses (C++, Lua, Python for asset tooling).
- Testing: GoogleTest, RapidCheck (property-based), libFuzzer, pytest (asset
  cooker), and Google Benchmark (micro-benchmarks); which kind runs at which
  stage is ADR-0013.
- No formal code review process — solo project; CI's build, test, lint, and
  sanitizer gates are the primary quality gate.

## Observability

- **Logging:** Boost.Log (ADR-0036) — mature, no reason to hand-roll one given
  the project's learning focus is elsewhere (ballistics, networking, ECS).
- **Profiling:** NVTX ranges in the code, read with NVIDIA Nsight Systems (CPU
  threads, timeline) and Nsight Graphics (GPU frames, D3D12 capture) — the
  primary tools for inspecting client and server performance during development.
- **Metrics:** `augustad` serves Prometheus metrics for its tick, Lobby and
  Match, Sessions and each client's Connection health, which a
  kube-prometheus-stack in the k3s cluster scrapes and Grafana draws, for both
  `develop` and `staging` (ADR-0049, which holds the catalogue). Everything is
  measured on the server: clients report nothing.
- **Log aggregation:** Loki keeps every pod's log for 15 days, shipped by Alloy,
  and Grafana queries it beside the metrics (ADR-0053).
- **Liveness:** `/livez` fails when the tick loop has not finished a tick for 5
  seconds, and is the Deployment's liveness probe, so a hung server restarts on
  its own (ADR-0049).
- **Alerts:** rules ship with the chart and show in Grafana and Alertmanager,
  with no receiver yet. One is chosen when the server runs unattended for real.

## Performance

- **NFR-01** (server tick rate ≥ 60 Hz, see REQUIREMENTS.md) remains the only
  formal, tested performance target.
- **No formal client frame-rate target.** Deliberately not turned into an NFR —
  frame rate is judged subjectively while playing/testing, not automated or
  gated in CI.
- **Memory strategy:** rely on Flecs' and PhysX's built-in allocators; no custom
  arena/pool allocators until profiling shows a concrete need.
- Google Benchmark is used for targeted micro-benchmarks of hot-path code (the
  server tick and its capture, replication, a PresentationWorld frame,
  ballistics, serialization, pack loading) — not a blanket requirement for every
  function. The nightly tracks them over time (ADR-0013).
