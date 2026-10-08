# Roadmap — FPS Simulator Engine

Milestones are ordered, not dated (no fixed deadline — see VISION.md). Sizes are
relative effort, not calendar estimates: S / M / L. They are grouped by the
release that shipped them; each requirement in REQUIREMENTS.md names the release
that first met it, or the one it is planned for.

## v1 — Playable Match (1.0.0, 2026-10-02)

### M0 — Project & Infrastructure Setup (S)

No engine code yet — get the project, tooling, and pipelines standing. Split
into two sub-milestones: M0a covers everything needed to write, build, and test
code locally and in CI; M0b covers containerizing and deploying the server onto
the self-hosted k3s cluster. Watching it there came with v2 (M7).

#### M0a — Client & Dev Environment

- GitHub repository, Git Flow branches (`main` + `develop`), branch protection,
  PR-gated merges
- `.gitignore`, `.editorconfig`, local `commit-msg` hook (Conventional Commits
  validation)
- `vcpkg.json` manifest + CMake toolchain wiring
- Windows bootstrap script (`scripts/bootstrap-windows.ps1`): VS Build Tools
  installed system-wide, Windows SDK, CMake, Ninja, Git, vcpkg
- Linux dev container (`.devcontainer/`): CMake, Ninja, vcpkg, clang-tidy,
  clang-format, gdb, GitHub CLI, kubectl, helm
- `.vscode/extensions.json` recommended extensions
- GitHub Actions CI pipeline: build+test client (Windows runner) and server
  (Linux runner), clang-format/clang-tidy, warnings-as-errors, ASan+UBSan

**Exercises:** ENGINEERING.md's Developer Environment, Code Quality, and Git
Workflow sections; ADR-0008, ADR-0011, ADR-0012, ADR-0013, ADR-0025 **Exit
criteria:** a fresh clone + the two bootstrap scripts produce a working build
environment on both sides; CI is green on a skeleton commit

#### M0b — Server & k8s Infra

- Self-hosted k3s cluster (single node, own hardware)
- Flux installed in the cluster, reconciling `main` and `develop` from Git (GHCR
  image + Helm chart). No self-hosted GitHub Actions runner anywhere in this
  pipeline — `feature/*`/`hotfix/*`/`release/*` branches are not deployed to k3s
  at all (see ADR-0026)
- Server Dockerfile; Helm chart(s) for the dedicated server
- Shared `hostPath` volume for signed asset packs
- CI addition: `helm lint` + `docker build` validation on every PR touching the
  chart/Dockerfile

**Exercises:** ENGINEERING.md's CI/CD and Deployment & CD sections; ADR-0026
**Exit criteria:** Flux reconciles `main`/`develop` to a hello-world server
automatically on merge, in their respective namespaces

### M1 — De-risking Spikes (S)

Prove the riskiest unknowns work in isolation before building on them. No
gameplay yet.

- Falcor renders a textured, rotating primitive on the Windows target
- Minimal GameNetworkingSockets round-trip: Windows client ↔ Linux dedicated
  server
- Minimal PhysX prediction/reconciliation test: one entity, restore-and-replay
  correction (ADR-0004) visibly acceptable (no wild jitter)

**Exercises:** ADR-0002, ADR-0003, ADR-0004, ADR-0009, ADR-0011 (C++23),
ADR-0012 (Google C++ Style Guide), ADR-0013 (GoogleTest/Benchmark) — the first
code written on this project, so where these foundational tooling ADRs are first
exercised in practice **Exit criteria:** all three spikes run standalone and
demonstrably work

### M2 — Asset Pipeline (M)

Built before any gameplay milestone needs a test map, so nothing downstream ever
touches a hardcoded placeholder.

- Level baking tool: usd-optimize (stage cleanup) + usd-validation-nvidia
  (validation) on the OpenUSD-authored test map, baked to runtime format
- Asset cooker (`tools/pack`, pure Python): meshoptimizer (meshes, read directly
  from the cleaned USD stage) and DirectXTex (textures) via two small native
  bindings (`tools/pack/cpp/`, no OpenUSD - see ADR-0030)
- Signed, verified packs (client + server split)
- CI addition: asset-pipeline check — generate a fresh throwaway Ed25519 keypair
  for the run, cook the test assets, sign with the ephemeral key, verify the
  signed pack loads end to end

**Exercises:** ADR-0015 through ADR-0020, ADR-0030, ADR-0031 **Exit criteria:**
both executables load exclusively from signed, verified packs produced by the
cooker; a real (if simple) test map exists for every milestone from here on to
use

### M3 — Networked Movement Skeleton (M)

- US-01 Connect to Dedicated Server
- US-02 Join a Match (2–8 Players)
- US-04 Move Player Character
- US-05 Manage Stamina
- Test space loaded from the real asset pipeline's signed pack (M2)

**Exercises:** ADR-0001, ADR-0005, ADR-0006, ADR-0021, ADR-0024 **Exit
criteria:** 2–8 Windows clients connect to the Linux server, join a match, move
(walk/run/crouch/prone) in the pipeline's test map, see each other with
prediction + reconciliation working

### M4 — Combat Skeleton (L)

- US-06 Aim Weapon, US-07 Fire Rifle, US-08 Reload Rifle, US-09 Apply Weapon
  Recoil
- US-10 Simulate Bullet Ballistics, US-11 Detect Hit by Impact Location, US-12
  Apply Damage by Hit Location

**Exercises:** ADR-0002, ADR-0023 (through Damage phase), ADR-0024
(WeaponHandling), ADR-0044 (lag compensation, Shot and Hit confirmation
messages), ADR-0014 (Slang shaders exercised by weapon-related rendering, e.g.
muzzle flash/tracer effects) **Exit criteria:** players aim/fire/reload a rifle;
bullets follow a real server-computed physics trajectory; hits resolve by body
part with damage applied (debug HUD/log is enough, no scoring yet)

### M5 — Full Match Loop (M)

- US-03 Spawn into a Match, US-13 Player Death (No Respawn), US-14 Determine
  Match End / Win Condition

**Exercises:** ADR-0022 (Lua), ADR-0023 (Scripts/Behaviours phase), ADR-0010
(Steam Audio — first point in the roadmap where audio cues, e.g. death/
match-end stingers, become meaningful to exercise) **Exit criteria:** a complete
match is playable start to finish — spawn, fight, permanent death for the match,
win condition ends the match, next match starts automatically

### M6 — Hardening & v1 Release (S)

- US-15 Server-Side Validation (Anti-Cheat Baseline)
- Full v1 "definition of done" verification pass (REQUIREMENTS.md)

**Exercises:** §8 Security concept, ADR-0018 (signing enforced) **Exit
criteria:** matches REQUIREMENTS.md's v1 definition of done exactly

## v2 — Operable and Testable (2.0.0, 2026-10-07)

v1 made a Match playable; v2 made the server watchable, diagnosable and
deployable per scenario, and gave the engine the tools to test itself under
load. It added no new player-facing requirement: it met NFR-07 and NFR-10.

### M7 — Observability (M)

- kube-prometheus-stack (Prometheus, Alertmanager, Grafana) installed by Flux in
  a `monitoring` namespace, under an `infrastructure` Kustomization the `apps`
  one depends on
- `augustad` serves `/metrics` (prometheus-cpp) and `/livez`; the chart adds its
  Service, `ServiceMonitor`, liveness probe and alert rules
- The metrics catalogue instrumented, counted where the heartbeat counts
- Two Grafana dashboards as code: "Server" and "Connection health"

**Exercises:** ADR-0049, ADR-0005 (the Metrics thread), ADR-0026 **Exit
criteria:** a load test against `develop` shows up on both dashboards (NFR-07),
and a server whose tick loop is stalled is restarted by its liveness probe

### M8 — Crash Diagnosability & Operations (S)

- `augustad` logs a symbolized stack and leaves a core dump on a crash, with
  split debug info
- The chart declares `augustad`'s readiness, resources and grace period, and a
  startup probe holds the liveness probe back
- One server per scenario in the cluster, from the packs `augusta-publish` puts
  on the node
- Runbooks: Flux rollback, k3s node recovery, pack key rotation, cutting a
  release

**Exercises:** ADR-0047, ADR-0026, NFR-10, NFR-11 **Exit criteria:** a crashed
`augustad` leaves a symbolized stack in its log and a core dump the release's
debug info opens; each scenario's server runs from its published pack

### M9 — Scenario Composition (S)

- A scenario composed from the files `scenarios/<name>.yaml` names, cooked by
  the pack tool
- Characters named by their key in the scenario manifest, and by their path in
  the protocol (breaking for packs and clients of v1)

**Exercises:** ADR-0041, ADR-0042, US-22 **Exit criteria:** two scenarios cook
from their manifests and each runs on its own server

### M10 — Test & Performance Tooling (M)

- `augusta-swarm`: a scenario's Player count of Scripted players against a
  server, for load and end-to-end tests
- Netcode tests: Scripted players through a simulated impaired link, nightly
- Benchmarks of the server tick (per phase), the protocol, pack loading and
  ballistics
- Match recording and replay on SimulationWorld, checked by a golden match
  (ADR-0048, since superseded by ADR-0050 and ADR-0051 for v3)

**Exercises:** ADR-0013, ADR-0048 **Exit criteria:** `augusta-swarm` plays a
server through its Match ends; the nightly runs the netcode tests and the
benchmarks

### M11 — Codebase & Developer Experience (M)

- Client and server split by responsibility (`augusta::client`,
  `augusta::server`); a Session's threads run under a Harness Runner
- The dev container as the one Linux environment
- A documentation site: MkDocs Material for the docs, Doxygen for the C++ API,
  on GitHub Pages
- Formatting and linting for every language in the repository (Python, shell,
  workflows, CMake, Markdown, PowerShell, Lua, TOML), following Google's style
  guides
- The C++ tools built together under `AUGUSTA_TOOLS`; one uv environment for
  `tools/`

**Exercises:** ADR-0012, ADR-0021, ADR-0046 **Exit criteria:** CI checks every
language's format and lint; the documentation site publishes from `develop`

## v3 — Reproducible and Scalable (planned, 3.0.0)

v3 makes what goes wrong under load reproducible, and runs more than one Match
of a scenario at once. Players with no one at the keyboard come first: Agents
are the load that proves the rest, and Match captures bring back what fails
under it. It meets US-21, US-23, US-24, NFR-09, NFR-13 and NFR-14.

### M12 — Agents (M)

- Intents in `augusta_harness`: movement, aim and trigger channels, plus Raw,
  carried out every tick under the Runner
- `tools/agent`: the netcode and Match loop tests over a fixture of Intents;
  `tools/swarm` removed
- Python in the root build under `AUGUSTA_TOOLS`, and the pybind11 module over
  the Harness
- The `augusta_agent` package (asyncio), its pytest smoke test against an
  in-process server, and the `load_test.py` and `raw_hold.py` examples

**Exercises:** US-23, ADR-0052, ADR-0005, ADR-0013 **Exit criteria:**
`load_test.py` replaces `augusta-swarm` and passes against `develop`

### M13 — Client-side Metrics (S)

- Each Agent's NetcodeStats (Reconciliation corrections, interpolation running
  dry, fire) and connection statistics served on `/metrics` from the load test
- The load test as a Kubernetes `Job` in the cluster, from an `augusta-agent`
  Linux image, about 16 Agents per Pod, scraped through a `PodMonitor`

**Exercises:** NFR-14, ADR-0049, ADR-0052, ADR-0026 **Exit criteria:** a load
test Job started by one Git change shows its Agents' metrics in Grafana beside
the server's

### M14 — Match Capture & Reenactment (M)

- `augustad` captures every Match's client actions
- The Reenact request, and `augustac --reenact` playing one Captured player
  against a live server

**Exercises:** ADR-0050, ADR-0038, US-21 **Exit criteria:** a playtest's capture
is reenacted against a real server, each Captured player sending its Commands at
their ticks

### M15 — Replay (M)

- `augustad` in replay mode re-runs a capture on a fresh SimulationWorld
- Replay viewers: `augustac --replays` and `--replay <capture>`

**Exercises:** ADR-0051, US-21, NFR-09 **Exit criteria:** a capture is watched
from any player's view, its Deaths and Match end as captured

### M16 — Horizontal Scalability (M)

- An ADR for NFR-13, amending ADR-0026 and ADR-0034: several servers of a
  scenario declared in Git, each with its own Service
- `network.servers` in place of `network.server_address`: augustac and Agents
  try each in turn, moving on at _lobby full_ or _match in progress_

**Exercises:** NFR-13, US-24, NFR-11, NFR-01, ADR-0026, ADR-0034 **Exit
criteria:** 4 servers of one scenario run 8-player Matches of Agents at once at
60 Hz with no missed ticks; adding or removing a server is one Git change

### M17 — Hardening & v3 Release (S)

- Verification of every requirement planned for v3; each then names v3
- Release 3.0.0

**Exit criteria:** every requirement planned for v3 is met

---

## Not yet planned

- More weapons, more maps
- Network authentication: certificates signed by a project certificate
  authority, so peers prove who they are (connections are already encrypted;
  trusted-LAN-only until then, see ARCHITECTURE.md §8)
- Matchmaking/master server
- Cross-platform client (would require revisiting Falcor's Linux/Vulkan path —
  currently Windows-only, see ADR-0009)
- Incremental asset rebuild (vs. full rebake, see ADR risk notes)
- LuaJIT, if policy-script performance ever becomes a bottleneck
- Production (`main`) deployment target — explicitly undecided for now (see
  ENGINEERING.md, Deployment & CD)
- Agones, if fleet-scale dynamic server allocation is ever needed
- Remote/public access to non-production environments (VPN or port-forwarding) —
  LAN-only for now
- Client-side metrics from the real client, and an alert receiver (see ADR-0049)
