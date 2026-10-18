# Roadmap — FPS Simulator Engine

Milestones are ordered, not dated (no fixed deadline — see VISION.md). Sizes are
relative effort, not calendar estimates: S / M / L. They are grouped by the
release that shipped them, or the one they are planned for; each requirement in
REQUIREMENTS.md names the release that first met it, or the one it is planned
for. A planned release is a delivery point: it ships once its milestones meet
their exit criteria, and its version is the next major when it is cut.

## Contents

<!-- pyml disable md051 -->

- [v1 — Playable Match (1.0.0, 2026-10-02)](#v1--playable-match-100-2026-10-02)
    - [M0 — Project & Infrastructure Setup (S)](#m0--project--infrastructure-setup-s)
    - [M1 — De-risking Spikes (S)](#m1--de-risking-spikes-s)
    - [M2 — Asset Pipeline (M)](#m2--asset-pipeline-m)
    - [M3 — Networked Movement Skeleton (M)](#m3--networked-movement-skeleton-m)
    - [M4 — Combat Skeleton (L)](#m4--combat-skeleton-l)
    - [M5 — Full Match Loop (M)](#m5--full-match-loop-m)
    - [M6 — Hardening & v1 Release (S)](#m6--hardening--v1-release-s)
- [v2 — Operable and Testable (2.0.0, 2026-10-07)](#v2--operable-and-testable-200-2026-10-07)
    - [M7 — Observability (M)](#m7--observability-m)
    - [M8 — Crash Diagnosability & Operations (S)](#m8--crash-diagnosability--operations-s)
    - [M9 — Scenario Composition (S)](#m9--scenario-composition-s)
    - [M10 — Test & Performance Tooling (M)](#m10--test--performance-tooling-m)
    - [M11 — Codebase & Developer Experience (M)](#m11--codebase--developer-experience-m)
- [v3 — Reproducible (3.0.0, 2026-10-11)](#v3--reproducible-300-2026-10-11)
    - [M12 — Agents (M)](#m12--agents-m)
    - [M13 — Match Capture & Reenactment (M)](#m13--match-capture--reenactment-m)
    - [M14 — Replay (M)](#m14--replay-m)
    - [M15 — Cluster Hardening (M)](#m15--cluster-hardening-m)
    - [M16 — Hardening & v3 Release (S)](#m16--hardening--v3-release-s)
- [v4 — Authoritative Body (planned)](#v4--authoritative-body-planned)
    - [M17 — One Pack Format (S)](#m17--one-pack-format-s)
    - [M18 — Skeleton (M)](#m18--skeleton-m)
    - [M19 — Muzzle (S)](#m19--muzzle-s)
    - [M20 — Clips & the Hitbox Pose (M)](#m20--clips--the-hitbox-pose-m)
- [v5 — The Image (planned)](#v5--the-image-planned)
    - [M21 — Deferred Frame & Looks (M)](#m21--deferred-frame--looks-m)
    - [M22 — Upscaling & Graphics Settings (S)](#m22--upscaling--graphics-settings-s)
    - [M23 — Ray-traced Lighting (M)](#m23--ray-traced-lighting-m)
    - [M24 — Drawing the Body (M)](#m24--drawing-the-body-m)
- [v6 — The World Answers (planned)](#v6--the-world-answers-planned)
    - [M25 — Props & Surface Materials (L)](#m25--props--surface-materials-l)
    - [M26 — Impact Effects (S)](#m26--impact-effects-s)
    - [M27 — Ragdolls (S)](#m27--ragdolls-s)
- [v7 — The Sound (planned)](#v7--the-sound-planned)
    - [M28 — Acoustics (L)](#m28--acoustics-l)
    - [M29 — Footsteps (S)](#m29--footsteps-s)
- [v8 — Teams (planned)](#v8--teams-planned)
    - [M30 — Team Match (M)](#m30--team-match-m)
    - [M31 — Voice Chat (M)](#m31--voice-chat-m)
- [v9 — Explosions, Destruction & Smoke (planned)](#v9--explosions-destruction--smoke-planned)
    - [M32 — Explosions, Destruction & Smoke (L)](#m32--explosions-destruction--smoke-l)
- [v10 — VR (planned)](#v10--vr-planned)
    - [M33 — VR Play Mode (L)](#m33--vr-play-mode-l)
- [Later — Scalable (planned, no version yet)](#later--scalable-planned-no-version-yet)
    - [M34 — Client-side Metrics (S)](#m34--client-side-metrics-s)
    - [M35 — Horizontal Scalability (M)](#m35--horizontal-scalability-m)
- [Not yet planned](#not-yet-planned)

<!-- pyml enable md051 -->

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
- Windows bootstrap script (`scripts/bootstrap.sh`): VS Build Tools installed
  system-wide, Windows SDK, CMake, Ninja, Git, vcpkg
- Linux dev container (`.devcontainer/`): CMake, Ninja, vcpkg, clang-tidy,
  clang-format, lldb, GitHub CLI, kubectl, helm
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

- NVRHI renders a textured, rotating primitive on the Windows target
- Minimal GameNetworkingSockets round-trip: Windows client ↔ Linux dedicated
  server
- Minimal PhysX prediction/reconciliation test: one entity, restore-and-replay
  correction (ADR-0004) visibly acceptable (no wild jitter)

**Exercises:** ADR-0002, ADR-0003, ADR-0004, ADR-0059, ADR-0011 (C++23),
ADR-0012 (Google C++ Style Guide), ADR-0013 (GoogleTest/Benchmark) — the first
code written on this project, so where these foundational tooling ADRs are first
exercised in practice **Exit criteria:** all three spikes run standalone and
demonstrably work

### M2 — Asset Pipeline (M)

Built before any gameplay milestone needs a test map, so nothing downstream ever
touches a hardcoded placeholder.

- Level baking tool: usd-optimize (stage cleanup) + usd-validation-nvidia
  (validation) on the OpenUSD-authored test map, baked to runtime format
- Asset cooker (`tools/pack`, Python over one native module,
  `tools/pack/native/`, no OpenUSD - see ADR-0030): meshoptimizer (meshes, read
  directly from the cleaned USD stage), libktx (textures), and `augusta_assets`,
  linked through its binding, which writes and verifies the packs
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

- A load test: a scenario's Player count of headless clients against a server,
  each playing a fixed behaviour
- Netcode tests: those clients through a simulated impaired link, nightly
- Benchmarks of the server tick (per phase), the protocol, pack loading and
  ballistics

**Exercises:** ADR-0013 **Exit criteria:** the load test plays a server through
its Match ends; the nightly runs the netcode tests and the benchmarks

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

## v3 — Reproducible (3.0.0, 2026-10-11)

v3 makes what goes wrong under load reproducible. Players with no one at the
keyboard come first: Agents are the load, and Match captures bring back what
fails under it. It also hardens the Node and cluster that run the Servers. It
meets US-21, US-23, NFR-09 and NFR-15.

### M12 — Agents (M)

- Intents in `augusta_harness`: movement, aim and trigger channels, plus Raw,
  carried out every tick under the Runner
- `tools/agent`: the netcode and Match loop tests over a fixture of Intents
- Python in the root build under `AUGUSTA_TOOLS`, and the pybind11 module over
  the Harness
- The `augusta_agent` package (asyncio), its pytest smoke test against an
  in-process server, and the `load_test.py` and `raw_hold.py` examples

**Exercises:** US-23, ADR-0052, ADR-0005, ADR-0013 **Exit criteria:**
`load_test.py` plays a server through its Match ends against `develop`

### M13 — Match Capture & Reenactment (M)

- `augustad` captures every Match's client actions
- The Reenact request, and `augustac --reenact` playing one Captured player
  against a live server

**Exercises:** ADR-0050, ADR-0038, US-21 **Exit criteria:** a playtest's capture
is reenacted against a real server, each Captured player sending its Commands at
their ticks

### M14 — Replay (M)

- `augustad` in replay mode re-runs a capture on a fresh SimulationWorld
- Replay viewers: `augustac --replays` and `--replay <capture>`

**Exercises:** ADR-0051, US-21, NFR-09 **Exit criteria:** a capture is watched
from any player's view, its Deaths and Match end as captured

### M15 — Cluster Hardening (M)

- Kyverno in the cluster, and the same policies over the rendered manifests in
  CI
- Flux running only signed revisions; `staging` following signed release tags
- k3s's configuration in Git, measured against the CIS k3s profile
- The Node's OS: nftables firewall, key-only SSH, unattended security updates,
  auditd
- The Node's kernel: hardened parameters, tuning for UDP servers (socket
  buffers, bounded writeback, NIC rings, huge pages on request only), and no
  swap
- Pod Security levels per namespace; packs and captures on `local`
  PersistentVolumes; `augustad` in a user namespace under its own seccomp
  profile
- Default-deny NetworkPolicies; signed or digest-pinned images; no credential in
  Git; least-privilege operator and publish credentials
- Security logs in Loki and their alerts; every policy enforced, failing closed

**Exercises:** NFR-15, ADR-0056, ADR-0057, ADR-0026 **Exit criteria:**
`kube-bench` reports no failure outside the recorded exceptions, and every
negative check of the Node runbook is refused on the live cluster

### M16 — Hardening & v3 Release (S)

- Verification of every requirement planned for v3; each then names v3
- Release 3.0.0

**Exit criteria:** every requirement planned for v3 is met

## v4 — Authoritative Body (planned)

v3 made what goes wrong under load reproducible; v4 makes a Shot land where the
body is. A Character gets a Skeleton, its Hitboxes follow Joints posed from
replicated state, and rounds leave the rifle's Muzzle. All of it is the
server's: clients draw the same rigid body until v5. First, the pack format is
written once, in C++, since every release from here adds blobs to it.

### M17 — One Pack Format (S)

- Match captures and packs under extensions of their own, `.acap` and `.apak`
- The cooker's native module built in the main CMake project; `augusta-pack`
  writes packs, and `augusta-inspect`, `augusta-verify` and `augusta-keygen`
  read them and make keys, through `augusta_assets`

**Exercises:** ADR-0030, ADR-0031, ADR-0050 **Exit criteria:** no pack blob is
encoded or decoded in Python, and the golden packs cook unchanged through the
C++ encoder

### M18 — Skeleton (M)

- A Character's UsdSkel Skeleton cooked into both packs, each Hitbox bound to
  its Joint
- The server loads the Skeleton and refuses a Joint name that does not resolve
- A Posing phase poses a player's Hitboxes from its Skeleton; the Hitbox history
  keeps posed Joints and interpolates them Joint by Joint
- The presentation animation stub deleted

**Exercises:** ADR-0040, ADR-0061, ADR-0044, ADR-0023 **Exit criteria:**
Hitboxes posed from the Rest pose hit exactly where the rigid placement did, now
through their Joints

### M19 — Muzzle (S)

- A round leaves the rifle's Muzzle, placed from the desktop hand and the
  shoulder Joint, not from the Eye
- A Muzzle inside the Map's geometry strikes the Map there

**Exercises:** ADR-0064, NFR-03 **Exit criteria:** every round's trajectory
starts at its rifle's Muzzle

### M20 — Clips & the Hitbox Pose (M)

- A Clip format, cooked and sampled: locomotion, stance, aim and grip
- The Authoritative State carries what the pose needs; the server poses every
  player's Hitboxes from its Clips
- A client evaluates the same pose from interpolated state

**Exercises:** ADR-0066, ADR-0061, ADR-0038 **Exit criteria:** a crouching,
aiming player is hit where its posed arm is, and a client's pose agrees with the
server's within the tolerance

## v5 — The Image (planned)

v4 poses the body; v5 draws it, in a world with materials and light. It meets
NFR-17.

### M21 — Deferred Frame & Looks (M)

- A deferred frame drawing UsdPreviewSurface Looks from the cooked textures
- A Map's sun and sky, translucency and the HDR image
- A pack needing more memory than the GPU has refused

**Exercises:** ADR-0067, ADR-0017, ADR-0033 **Exit criteria:** the test map
draws its Looks under its sun and sky on both platforms

### M22 — Upscaling & Graphics Settings (S)

- One `Upscaler`: DLSS Super Resolution or DLAA on NVIDIA GPUs, FSR 3.1 on the
  others
- Resolution, window mode, frame cap and brightness in the client config

Before ray tracing because the deferred frame has no anti-aliasing, and ray
tracing needs the reduced render resolution.

**Exercises:** ADR-0067, ADR-0034, ADR-0058 **Exit criteria:** every edge is
anti-aliased with either upscaler, and each setting takes effect from the config

### M23 — Ray-traced Lighting (M)

- Shadows, local lights, reflections and indirect light, ray traced every frame
  with nothing baked

**Exercises:** ADR-0067, ADR-0059 **Exit criteria:** the test map's shade,
lamps, reflections and bounced light all come from its geometry, with no baked
lighting in the pack

### M24 — Drawing the Body (M)

- The client's Animation phase, Cosmetic layers and GPU skinning
- The local player's own body in first person

**Exercises:** ADR-0066, ADR-0061 **Exit criteria:** every player is drawn
skinned in its pose, the local player sees its own body, and an 8-player Match
holds NFR-17

## v6 — The World Answers (planned)

v5 makes the world visible; v6 makes it physical. Objects move when pushed or
shot, rounds go through what they can, and strikes show what they hit.

### M25 — Props & Surface Materials (L)

- Prop kinds, a Dynamics phase in both Worlds, and replicated dynamic bodies
- One Surface material catalogue for colliders, Props and Hitboxes
- Penetration, ricochet and rounds through players

The two are built in parallel; Props join as surfaces when they land.

**Exercises:** ADR-0045, ADR-0065, ADR-0002, NFR-03 **Exit criteria:** a round
passes through a wooden door and stops in a concrete wall, and a struck crate
moves the same way on every client

### M26 — Impact Effects (S)

- Particles, a decal in the G-buffer and a sound for each Surface material
- Debris as the client's dynamic bodies

**Exercises:** ADR-0065, ADR-0045 **Exit criteria:** a strike on glass, wood,
metal and concrete each looks and sounds its own

### M27 — Ragdolls (S)

- A ragdoll authored per Character, cooked, and thrown from the drawn pose

**Exercises:** ADR-0066, ADR-0045 **Exit criteria:** a killed player falls as a
ragdoll from its drawn pose, thrown along the killing blow

## v7 — The Sound (planned)

v7 makes the Map heard: sound is occluded, reflected and routed by its geometry,
and players' feet make noise.

### M28 — Acoustics (L)

- Steam Audio's overlay port with Embree, first: its fallback to the built-in
  ray tracer is decided before anything depends on it
- One sound shape for every cue; a player-chosen SOFA HRTF
- Air absorption, cue directivity, occlusion and transmission from the Map's
  Acoustic geometry and placed Props
- Baked reflections in Probe volumes and baked pathing around corners; the local
  player's own gunshot rings in its room

**Exercises:** ADR-0010, ADR-0065 **Exit criteria:** a shot behind a concrete
wall is muffled, one around a corner is heard around it, and a shot in a
corridor rings

### M29 — Footsteps (S)

- Footsteps at locomotion Clips' Foot contacts, by Surface material and gait

**Exercises:** ADR-0062, ADR-0066, US-04 **Exit criteria:** a player running
over a metal floor is heard, and one walking is quieter

## v8 — Teams (planned)

v8 turns the last player standing into two sides. It meets US-26 and US-27.

### M30 — Team Match (M)

- Two Teams, each in its Uniform, spawning at its Team's Spawn points
- A Team as the Winner; once dead, a player watches only its living teammates

**Exercises:** ADR-0068, US-14 **Exit criteria:** a 4v4 Match ends when one Team
has no one left alive

### M31 — Voice Chat (M)

- Push-to-talk by radio to the whole Team, and aloud to anyone near
- Opus Speech relayed by the server

**Exercises:** ADR-0069, NFR-12 **Exit criteria:** radio is heard by every
teammate and no enemy, and Speech aloud is heard by anyone near, through the
acoustics

## v9 — Explosions, Destruction & Smoke (planned)

v9 adds what blows up, breaks and hides. It builds on the Props, Surface
materials and impact effects of v6, and on the Teams of v8 for friendly fire.

### M32 — Explosions, Destruction & Smoke (L)

- Explosions and their Fragments, Breakable Props and Integrity, Smoke screens,
  the flashbang's Dazzle, launched rounds, Effects and the Loadout

Its specs are not written yet: they are sliced, ordered and split into
milestones once its decisions (ADR-0070 to ADR-0073) are recorded.

**Exercises:** ADR-0070, ADR-0071, ADR-0072, ADR-0073 **Exit criteria:** set
with its specs

## v10 — VR (planned)

v10 lets a player play with a PC VR headset, in the same Matches as desktop
players. It meets US-25 and NFR-16.

### M33 — VR Play Mode (L)

- The Play mode and the Tracked pose on the server, bounded to the Character's
  Joints
- The OpenXR client: head and hands, the deferred frame drawn with multiview
- Body tracking from the player's cameras

**Exercises:** ADR-0063, ADR-0064, ADR-0061, US-25, NFR-16 **Exit criteria:** a
VR player plays a full Match holding the rifle in two hands, hit on its tracked
body, at NFR-16's frame rate

## Later — Scalable (planned, no version yet)

A later release runs more than one Match of a scenario at once, with Agents as
the load that proves it. It meets US-24, NFR-13 and NFR-14; its version is
chosen when it is cut.

### M34 — Client-side Metrics (S)

- Each Agent's NetcodeStats (Reconciliation corrections, interpolation running
  dry, fire) and connection statistics served on `/metrics` from the load test
- The load test as a Kubernetes `Job` in the cluster, from an `augusta-agent`
  Linux image, about 16 Agents per Pod, scraped through a `PodMonitor`

**Exercises:** NFR-14, ADR-0049, ADR-0052, ADR-0026 **Exit criteria:** a load
test Job started by one Git change shows its Agents' metrics in Grafana beside
the server's

### M35 — Horizontal Scalability (M)

- An ADR for NFR-13, amending ADR-0026 and ADR-0034: several servers of a
  scenario declared in Git, each with its own Service
- `network.servers` in place of `network.server_address`: augustac and Agents
  try each in turn, moving on at _lobby full_ or _match in progress_

**Exercises:** NFR-13, US-24, NFR-11, NFR-01, ADR-0026, ADR-0034 **Exit
criteria:** 4 servers of one scenario run 8-player Matches of Agents at once at
60 Hz with no missed ticks; adding or removing a server is one Git change

---

## Not yet planned

- More weapons, more maps
- Network authentication: certificates signed by a project certificate
  authority, so peers prove who they are (connections are already encrypted;
  trusted-LAN-only until then, see ARCHITECTURE.md §8)
- Matchmaking/master server
- Incremental asset rebuild (vs. full rebake, see ADR risk notes)
- LuaJIT, if policy-script performance ever becomes a bottleneck
- Production (`main`) deployment target — explicitly undecided for now (see
  ENGINEERING.md, Deployment & CD)
- Agones, if fleet-scale dynamic server allocation is ever needed
- NUMA-aware Servers (automatic NUMA balancing off, CPU manager and topology
  manager pinning each to one socket), if an Agent load shows the Node's
  balancing in the tick's late share or p99
- Remote/public access to non-production environments (VPN or port-forwarding) —
  LAN-only for now
- Client-side metrics from the real client, and an alert receiver (see ADR-0049)
