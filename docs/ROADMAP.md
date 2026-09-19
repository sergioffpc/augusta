# Roadmap — FPS Simulator Engine

Milestones are ordered, not dated (hobby project, no fixed deadline — see
VISION.md). Sizes are relative effort, not calendar estimates: S / M / L.

## M0 — Project & Infrastructure Setup (S)
No engine code yet — get the project, tooling, and pipelines standing.
Split into two sub-milestones: M0a covers everything needed to write,
build, and test code locally and in CI; M0b covers containerizing and
deploying the server onto the self-hosted k3s cluster.

### M0a — Client & Dev Environment
- GitHub repository, Git Flow branches (`main` + `develop`), branch
  protection, PR-gated merges
- `.gitignore`, `.editorconfig`, local `commit-msg` hook (Conventional
  Commits validation)
- `vcpkg.json` manifest + CMake toolchain wiring
- Windows bootstrap script (`scripts/bootstrap-windows.ps1`): VS Build
  Tools installed system-wide, Windows SDK, CMake, Ninja, Git, vcpkg
- WSL bootstrap script (`scripts/bootstrap-wsl.sh`): CMake, Ninja, vcpkg,
  clang-tidy, clang-format, gdb, GitHub CLI, kubectl, helm
- `.vscode/extensions.json` recommended extensions
- GitHub Actions CI pipeline: build+test client (Windows runner) and
  server (Linux runner), clang-format/clang-tidy, warnings-as-errors,
  ASan+UBSan

**Exercises:** ENGINEERING.md's Developer Environment, Code Quality, and
Git Workflow sections; ADR-0008, ADR-0011, ADR-0012, ADR-0013, ADR-0025
**Exit criteria:** a fresh clone + the two bootstrap scripts produce a
working build environment on both sides; CI is green on a skeleton commit

### M0b — Server & k8s Infra
- Self-hosted k3s cluster (single node, own hardware)
- Flux installed in the cluster, reconciling `main` and `develop` from
  Git (GHCR image + Helm chart). No self-hosted GitHub Actions runner
  anywhere in this pipeline — `feature/*`/`hotfix/*`/`release/*`
  branches are not deployed to k3s at all (see ADR-0026)
- Server Dockerfile; Helm chart(s) for the dedicated server
- Shared `hostPath` volume for signed asset packs
- CI addition: `helm lint` + `docker build` validation on every PR
  touching the chart/Dockerfile

**Exercises:** ENGINEERING.md's CI/CD and Deployment & CD sections;
ADR-0026
**Exit criteria:** Flux reconciles `main`/`develop` to a hello-world
server automatically on merge, in their respective namespaces

## M1 — De-risking Spikes (S)
Prove the riskiest unknowns work in isolation before building on them.
No gameplay yet.
- Falcor renders a textured, rotating primitive on the Windows target
- Minimal GameNetworkingSockets round-trip: Windows client ↔ Linux
  dedicated server
- Minimal PhysX prediction/reconciliation test: one entity, snap/blend
  correction (ADR-0004) visibly acceptable (no wild jitter)

**Exercises:** ADR-0002, ADR-0003, ADR-0004, ADR-0009, ADR-0011 (C++23), ADR-0012
(Google C++ Style Guide), ADR-0013 (GoogleTest/Benchmark) — the first code
written on this project, so where these foundational tooling ADRs are
first exercised in practice
**Exit criteria:** all three spikes run standalone and demonstrably work

## M2 — Asset Pipeline (M)
Built before any gameplay milestone needs a test map, so nothing
downstream ever touches a hardcoded placeholder.
- Level baking tool: usd-optimize (stage cleanup) + usd-validation-nvidia
  (validation) on the OpenUSD-authored test map, baked to runtime format
- Asset cooker (`tools/pack`, pure Python): meshoptimizer
  (meshes, read directly from the cleaned USD stage) and DirectXTex
  (textures) via two small native bindings (`tools/pack/cpp/`,
  no OpenUSD - see ADR-0030)
- Signed, verified packs (client + server split)
- CI addition: asset-pipeline check — generate a fresh throwaway Ed25519
  keypair for the run, cook the test assets, sign with the ephemeral key,
  verify the signed pack loads end to end

**Exercises:** ADR-0015 through ADR-0020, ADR-0030, ADR-0031
**Exit criteria:** both executables load exclusively from signed, verified
packs produced by the cooker; a real (if simple) test map exists for every
milestone from here on to use

## M3 — Networked Movement Skeleton (M)
- US-01 Connect to Dedicated Server
- US-02 Join a Match (2–8 Players)
- US-04 Move Player Character
- US-05 Manage Stamina
- Test space loaded from the real asset pipeline's signed pack (M2)

**Exercises:** ADR-0001, ADR-0005, ADR-0006, ADR-0021, ADR-0024
**Exit criteria:** 2–8 Windows clients connect to the Linux server, join a
match, move (walk/run/crouch/prone) in the pipeline's test map, see each
other with prediction + reconciliation working

## M4 — Combat Skeleton (L)
- US-06 Aim Weapon, US-07 Fire Rifle, US-08 Reload Rifle,
  US-09 Apply Weapon Recoil
- US-10 Simulate Bullet Ballistics, US-11 Detect Hit by Impact Location,
  US-12 Apply Damage by Hit Location

**Exercises:** ADR-0002, ADR-0023 (through Damage phase), ADR-0024
(WeaponHandling), ADR-0014 (Slang shaders exercised by weapon-related
rendering, e.g. muzzle flash/tracer effects)
**Exit criteria:** players aim/fire/reload a rifle; bullets follow a real
server-computed physics trajectory; hits resolve by body part with damage
applied (debug HUD/log is enough, no scoring yet)

## M5 — Full Round Loop (M)
- US-03 Spawn into a Round, US-13 Player Death (No Respawn),
  US-14 Determine Round End / Win Condition

**Exercises:** ADR-0022 (Lua), ADR-0023 (Scripts/Behaviours phase), ADR-0010
(Steam Audio — first point in the roadmap where audio cues, e.g. death/
round-end stingers, become meaningful to exercise)
**Exit criteria:** a complete round is playable start to finish — spawn,
fight, permanent death for the round, win condition ends the round, next
round starts automatically

## M6 — Hardening & v1 Release (S)
- US-15 Server-Side Validation (Anti-Cheat Baseline)
- Full v1 "definition of done" verification pass (REQUIREMENTS.md)

**Exercises:** §8 Security concept, ADR-0018 (signing enforced)
**Exit criteria:** matches REQUIREMENTS.md's v1 definition of done exactly

---

## Beyond v1 (not planned in detail)
- More weapons, more maps
- Network encryption (deferred per ADR/§8, trusted-LAN-only in v1)
- Matchmaking/master server
- Cross-platform client (would require revisiting Falcor's Linux/Vulkan
  path — currently Windows-only, see ADR-0009)
- Incremental asset rebuild (vs. full rebake, see ADR risk notes)
- LuaJIT, if policy-script performance ever becomes a bottleneck
- Production (`main`) deployment target — explicitly undecided for now
  (see ENGINEERING.md, Deployment & CD)
- Agones, if fleet-scale dynamic server allocation is ever needed
- Remote/public access to non-production environments (VPN or
  port-forwarding) — LAN-only for now
