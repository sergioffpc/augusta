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
  corrects for reality (smooth reconciliation) instead of pretending
  otherwise.
- **The server is the only source of truth.** Nothing from a client is
  trusted until validated (US-15).
- **Content is signed and verified, not just loaded.** Integrity is
  structural (ADR-0018), not an afterthought.
- **No premature optimization.** Profile first (Tracy), then optimize;
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
- **Trigger:** every push (solo project, no formal PR gating needed).
- **Pipeline stages:**
  1. Build + unit test the client on a Windows runner
  2. Build + unit test the server on a Linux runner
  - Dependency restore: `vcpkg install` (manifest mode) before the build
    step, both runners. Binary cache via a GitHub Packages NuGet feed
    (vcpkg's native GitHub-Actions-cache backend was removed upstream in
    2026 — a NuGet feed is now the supported caching path).
  3. `clang-format` check (fails on unformatted diffs)
  4. `clang-tidy` (Google style checks profile)
  5. Compile with a strict warning set, treated as errors
  6. ASan + UBSan test build, both platforms
  7. Asset pipeline check: build the asset cooker, generate a fresh
     throwaway Ed25519 keypair for this run, cook the test assets, sign
     with the ephemeral key, and verify the signed pack loads correctly
     end to end — the real release private key never touches CI
- **Not in CI:** TSan (expensive/noisy — run manually/periodically
  instead) and Tracy (interactive profiling tool, not a CI check).
- **Artifacts/releases:** out of scope for now — CI validates
  build+test+lint only. A publishing pipeline gets built when there's an
  actual release to make.

## Git Workflow

- **Branching model:** Git Flow — `main` (production/release) + `develop`
  (integration), with `feature/*`, `release/*`, `hotfix/*` branches.
- **Tags/releases:** created only when there's an actual release to make
  (e.g., reaching v1) — ROADMAP.md milestones (M0–M5) are internal
  checkpoints, not tagged releases.
- **Pull requests:** used even solo — `feature/*` → `develop` and
  `develop`/`hotfix/*` → `main` go through a PR so CI gates the merge;
  no formal review requirement, self-merge once CI passes.
- **Commit messages:** Conventional Commits, enforced via the local
  `commit-msg` hook (see Code Quality below).

## Deployment & CD (Non-Production)

Covers `develop`, `feature/*`, `hotfix/*`, and `release/*` — production
(`main`) deployment is explicitly out of scope here and remains
undecided/deferred.

- **Infrastructure:** self-hosted k3s, single node, on the developer's
  own hardware. No cloud provider involved.
- **Isolation:** one Kubernetes namespace per environment — `develop` is
  long-lived; `feature-*`, `hotfix-*`, `release-*` namespaces are
  ephemeral, created on branch push and deleted on branch delete. No
  concurrency limit on ephemeral namespaces for now.
- **Container images:** built in CI, pushed to GitHub Container Registry
  (GHCR).
- **Server exposure:** plain Kubernetes `Service` (`NodePort`, port
  auto-assigned by Kubernetes) — no Agones. Agones solves fleet-scale
  dynamic allocation, which this project doesn't need (one server
  instance per environment); revisit only if matchmaking/dynamic
  multi-server allocation is ever needed (Beyond v1).
- **CD mechanism:** push-based — a GitHub Actions workflow runs
  `helm upgrade --install` on push to a tracked branch, and
  `helm uninstall` (+ namespace deletion) on the corresponding branch's
  `delete` event. No ArgoCD/Flux — running a GitOps controller is
  unnecessary operational overhead for a solo developer when GitHub's
  native push/delete triggers already map directly onto
  create/destroy-environment.
- **Access:** LAN-only — no public exposure, no VPN/tunnel needed for now.
- **Asset packs:** built and signed manually, separately from the CD
  pipeline (see ADR-0018, CI/CD above). Packs are versioned independently
  of code deploys and can be shared across multiple server
  instances/versions. Stored on a shared `hostPath` persistent volume on
  the k3s node, populated manually after signing, mounted read-only into
  every server pod. Each environment's Helm values specify which
  `packVersion` to load (defaulting to the latest for ephemeral branches
  unless overridden).
- **Namespace naming:** derived from the branch name — lowercased, `/`
  and `_` replaced with `-`, truncated to fit Kubernetes' 63-character
  limit.

## Developer Environment

- **Model:** a single shared checkout on the Windows filesystem (NTFS) is
  used by both sides — no separate clones.
- **Server / shared core (Linux, via WSL2):** develop and build directly
  inside WSL2, accessing the repo via `/mnt/c/...`. No Docker container —
  a `scripts/bootstrap-wsl.sh` setup script installs CMake, Ninja,
  vcpkg, clang-tidy, clang-format, gdb, GitHub CLI, kubectl, and helm
  directly into the WSL environment. The cross-filesystem access cost
  (`/mnt/c`) is accepted here, since this side has the lighter build
  (no Falcor, D3D12, or Steam Audio).
- **Client (Windows, native):** built and run natively — never
  cross-compiled from WSL/Linux (not viable given Falcor/D3D12/NVIDIA
  SDK's MSVC-specific toolchain assumptions). A
  `scripts/bootstrap-windows.ps1` script (winget-driven) installs Visual
  Studio Build Tools to a custom, project-specific path
  (`--installPath`) — Microsoft's own supported side-by-side mechanism,
  letting different projects pin independent Build Tools versions —
  plus the Windows SDK, CMake, Ninja, vcpkg, and Git. (A fully hermetic,
  registry-free alternative — clang-cl + xwin-extracted SDK/CRT — was
  considered and rejected: Falcor's CMake presets only test/support
  MSVC on Windows, and stacking an unsupported compiler on top of an
  already-unmaintained dependency, ADR-0009, isn't worth the purity.)
- **Editor experience:** a committed `.vscode/extensions.json` lists
  recommended extensions (C++ tools, CMake Tools, clangd/clang-format,
  GitLens, EditorConfig, Lua, YAML/Helm, GitHub Actions) — VS Code
  prompts to install these whenever the folder is opened, on either
  side (WSL remote or native Windows), no container required.
- **Dependency hermeticity:** the `vcpkg.json` manifest (ADR-0025) is what
  actually makes dependency acquisition reproducible on both sides —
  not a container.

## Code Quality

- Google C++ Style Guide (ADR-0012), enforced via `clang-format` +
  `clang-tidy` in CI — not as local pre-commit hooks, to keep local
  tooling minimal; CI is the enforcement point.
- Strict warnings-as-errors in CI (see CI/CD above).
- ASan/UBSan in CI; TSan run manually/periodically given multithreading
  (ADR-0005).
- **Commit messages:** Conventional Commits format, enforced locally via
  a custom `commit-msg` git hook (a small regex-matching script) — no
  Node.js/`commitlint` dependency, consistent with keeping the toolchain
  to what the project already uses (C++, Lua, Python for asset tooling).
- Testing: GoogleTest (unit) + Google Benchmark (micro-benchmarks),
  per ADR-0013.
- No formal code review process — solo project; CI's build, test, lint,
  and sanitizer gates are the primary quality gate.

## Observability

- **Logging:** spdlog — mature, fast, no reason to hand-roll one given
  the project's learning focus is elsewhere (ballistics, networking, ECS).
- **Profiling:** Tracy — purpose-built for real-time, multithreaded frame
  profiling; the primary tool for inspecting client and server
  performance during development.
- No metrics/telemetry pipeline beyond Tracy for v1.
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
  v1; no custom arena/pool allocators until Tracy profiling shows a
  concrete need.
- Google Benchmark is used for targeted micro-benchmarks of hot-path code
  (e.g., ballistics math, serialization) as needed — not a blanket
  requirement for every function.
