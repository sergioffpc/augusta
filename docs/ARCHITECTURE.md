# arc42 Architecture Document — FPS Simulator Engine

## 1. Introduction and Goals
See [VISION.md](./VISION.md) for full vision. Summary: a realistic, physics-driven,
server-authoritative multiplayer FPS simulator engine, built in C++ with a
Windows client (rendering via NVIDIA Falcor/D3D12) and a headless Linux
server, as a learning project in low-level systems and networking programming.

Top quality goals (see [REQUIREMENTS.md](./REQUIREMENTS.md) for full NFR list):
1. Server-authoritative correctness (NFR-05)
2. Realistic, consistent ballistics (NFR-03)
3. Stable performance under v1 load (NFR-01, NFR-06)
4. Platform targeting, Windows client + Linux server (NFR-04)

## 2. Architecture Constraints
- **Technical:** C++, CMake + Ninja + sccache. Client: Windows-only, rendering
  via NVIDIA Falcor (D3D12). Server: Linux-only, headless.
- **Licensing:** third-party dependencies must be free/open-source (Flecs
  [MIT], PhysX [BSD-3], GameNetworkingSockets [BSD-3], Falcor [BSD-3],
  Steam Audio [Apache 2.0], miniaudio [MIT], Slang [Apache 2.0])
- **Language:** C++23 (avoid C++23 std modules/`import std` — still immature
  on MSVC and GCC/Clang)
- **Coding style:** Google C++ Style Guide
- **Testing:** GoogleTest (unit) + Google Benchmark (micro-benchmarks)
- **Content tooling:** OpenUSD (Tomorrow Open Source Technology License 1.0,
  Apache-derived) for offline map authoring/baking only — not linked into
  shipped client or server binaries. Same constraint for usd-optimize
  (Apache 2.0, stage cleanup), usd-validation-nvidia (Apache 2.0 +
  CC-BY-4.0, validation), and Adobe's USD-Fileformat-plugins (Apache 2.0,
  glTF/FBX/OBJ ingestion as USD layers, ADR-0016) — all offline/build-time
  only.
- **Organizational:** solo developer / small informal team, hobby project, no
  fixed deadline, milestone-driven

## 3. System Scope and Context
**Business context:** Players connect directly to a dedicated server via IP:port.
No matchmaking, master server, or third-party platform integration in v1.

**Technical context:**
- Client executable (Windows only): rendering (Falcor/D3D12), input, audio,
  local prediction
- Dedicated server executable (Linux only): headless, authoritative simulation
- Communication: GameNetworkingSockets over UDP, unencrypted in v1

```
+--------+          +------------------------+
| Player |--------->| FPS Simulator Engine   |
+--------+          +------------------------+
```

## 4. Solution Strategy
- ECS-based simulation core (Flecs), shared between client and server
- PhysX for general collision/movement; custom-built ballistics module for bullet
  physics (the project's core learning focus)
- Server-authoritative model: server is the single source of truth for all
  gameplay-affecting state
- Client-side prediction for responsiveness, reconciled via smooth correction
  against authoritative server snapshots (not exact replay — see ADR-0004)
- Multithreaded from v1: dedicated Main/Render, Simulation, and Network I/O threads
- Custom lightweight binary protocol for game-state messages
- Mechanism vs. policy vs. data separation: engine mechanism (movement,
  physics, ballistics, hit detection) is C++; game policy (round
  lifecycle, win conditions, spawn rules) is encapsulated in sandboxed
  Lua scripts run in a dedicated Scripts/Behaviours phase; tunable
  balance values are data-driven configuration — a third category (see
  §8, ADR-0022, ADR-0023)
- Rendering built on NVIDIA Falcor (D3D12), used exclusively by the Windows
  client. The server is Linux-only, headless, and entirely decoupled from
  Falcor/graphics-API concerns.
- Audio via Valve's Steam Audio (Apache 2.0), used by the Windows client for
  spatial audio; no audio dependency on the headless Linux server.
- Shaders authored in Slang — already Falcor's default shader compiler
  (targets D3D12/HLSL); adopting it explicitly formalizes an existing
  transitive dependency rather than adding a new one.
- Level/map content authored in OpenUSD (interchange/authoring format only)
  and baked at build time into the engine's own lightweight runtime level
  format. OpenUSD, Hydra, and their toolchain (TBB/Boost/Python) are a
  content-pipeline dependency only — never linked into the shipped client
  or server.

## 5. Building Block View

```
+--------+     +------------------------------------+
| Player |     |         FPS Simulator Engine        |
+--------+     |                                     |
    |          |    +--------+          +--------+   |
    +--------->|    | Client |<-------->| Server |   |
               |    +--------+          +--------+   |
               +-------------------------------------+
```

**Shared Core** (compiled into both client and server)
- ECS (Flecs) — shared entity/component data: players, bullets, round
  state. Each World below is its own Flecs world instance built
  directly on the library; not a separate wrapped module in its own
  right.
- Physics — PhysX wrapper (collision, movement); one interface used
  identically by PredictionWorld and SimulationWorld
- WeaponHandling — aim/ADS, fire, reload, recoil, ammo rules; one
  interface used identically by both Worlds. The client/server
  difference isn't in this module's logic, it's in what each World
  does with the result: the server treats it as authoritative and
  feeds Ballistics, the client uses it only for local predicted
  feedback pending reconciliation - same split as Physics.
- Networking Protocol — message definitions + custom binary serialization
- Match/Round State — round lifecycle, win conditions
- Level Data — lightweight custom runtime format, baked offline from
  OpenUSD source

**Client-only** (Windows-only)
- Input handling — turns keyboard/mouse events pushed by Renderer into
  commands for PredictionWorld. There is no separate Window module:
  Falcor fuses window creation with its GPU device/swapchain into one
  object (ADR-0009), so Renderer owns the OS window and pushes device
  events to Input rather than a third module managing the window handle
  independently
- Networking — sends commands, receives authoritative server state
- ClientRuntime
  - PredictionWorld (ECS) — consumes commands + authoritative server
    state; runs client-side prediction and reconciliation (ADR-0004); emits
    an immutable prediction state each simulation tick
  - PresentationWorld (ECS) — consumes the prediction state; interpolates/
    smooths for display; emits presentation state each render frame
- Renderer — NVIDIA Falcor (D3D12), shaders authored in Slang; owns the
  client's single OS window (see Input handling, above) and consumes
  presentation state. Exposes pumping window/device events and rendering
  a frame as two separate operations rather than one combined loop, so
  the Main/Render thread can drain events at a different cadence than it
  presents frames
- Audio — Steam Audio; consumes presentation state
- HUD/UI

```
+-------+     +------------+
| Input |     | Networking |
+-------+     +------------+
    |               |  ^
    | Commands      |  | Authoritative State
    v               v  |
  +-------------------------------+
  |         ClientRuntime          |
  |                                |
  |   +-------------------+        |
  |   |  PredictionWorld  |        |
  |   +-------------------+        |
  |             |                  |
  |             | Prediction State |
  |             v                  |
  |   +-------------------+        |
  |   | PresentationWorld |        |
  |   +-------------------+        |
  |             |                  |
  +-------------|------------------+
                | Presentation State
         +------+------+
         v             v
    +----------+   +-------+
    | Renderer |   | Audio |
    +----------+   +-------+
```

**PredictionWorld phases** (Simulation thread, fixed tick, once per tick,
in execution order: CommandIngestion → Reconciliation → Movement →
WeaponHandling → Commit)

| Phase | Category | Responsibility |
|---|---|---|
| CommandIngestion | Mechanism | Applies this tick's local input commands |
| Reconciliation | Mechanism | Ingests any newly arrived authoritative state; applies smooth snap/blend correction (ADR-0004) — no rollback/resimulate |
| Movement | Mechanism | Predicted PhysX movement, stamina |
| WeaponHandling | Mechanism | Predicts local fire feedback only (muzzle flash, sound cue, recoil, ammo count) — no bullet trajectory; hit/damage stays server-authoritative |
| Commit | Mechanism | Packages the tick's predicted state into the immutable Prediction State |

**PresentationWorld phases** (Main/Render thread, per render frame, in
execution order: Interpolation → Camera → Animation → AudioCues →
Commit)

| Phase | Category | Responsibility |
|---|---|---|
| Interpolation | Mechanism | Interpolates between the last two Prediction States for smooth motion at render frame rate |
| Camera | Mechanism | View camera — position/orientation, ADS zoom transition, recoil kick decay, view bob |
| Animation | Mechanism | Drives skeletal/procedural animation from interpolated movement and weapon state |
| AudioCues | Mechanism | Translates events carried in the Prediction State (e.g., fire, footstep) into spatialized audio cues |
| Commit | Mechanism | Packages the frame's presentation data into Presentation State |

Neither client world contains a Scripts/Behaviours phase — game policy is
exclusively server-authoritative.

**Server-only** (Linux-only, headless)
- Networking — receives client commands, sends authoritative state; the
  only server component that touches the network
- Input Validation — anti-cheat baseline (US-15); rejects/filters invalid
  commands before they reach the world (does not apply to outbound
  authoritative state)
- Ballistics — custom bullet trajectory simulation (gravity, travel
  time), hand-rolled instead of PhysX's generic projectile handling
  (ADR-0002). Exclusively server-side (ADR-0024): the client never
  simulates a bullet's outcome, only predicts local fire feedback, so
  this isn't part of Shared Core despite being physics-adjacent.
- Scripting (Lua) — sandboxed script hooks for game policy (round
  lifecycle, win conditions, spawn rules); small interface (e.g. a
  RunHook call) hiding the Lua embedding and the restricted-environment
  sandbox (§8) that upholds "no I/O inside ECS worlds" structurally.
  Server-only - game policy is exclusively server-authoritative, never
  run by either client world.
- ServerRuntime
  - SimulationWorld (ECS) — the single authoritative world (no prediction,
    no presentation needed). Runs mechanism systems in C++ (movement via
    PhysX, ballistics, hit detection, damage) and policy via a
    Scripts/Behaviours phase (Lua, sandboxed — round lifecycle, win
    conditions, spawn rules); emits authoritative state each tick

```
+------------+
| Networking |
+------------+
    |     ^
Raw |     | Authoritative
Cmds|     | State
    v     |
+-------------------+
| Input Validation  |
+-------------------+
    |
    | Validated Commands
    v
+---------------------------+
|       ServerRuntime        |
|                            |
|   +---------------------+  |
|   |   SimulationWorld   |  |
|   +---------------------+  |
|                            |
+----------------------------+
```
*(the Authoritative State emitted by `SimulationWorld` goes directly to
`Networking`, bypassing `Input Validation` — validation only applies to
inbound commands)*

**SimulationWorld phases** (executed in order, once per tick: Command
Ingestion → Movement → WeaponHandling → Ballistics → HitDetection →
Damage → Scripts/Behaviours → Commit)

| Phase | Category | Responsibility |
|---|---|---|
| CommandIngestion | Mechanism | Applies validated client commands to this tick's entities |
| Movement | Mechanism | PhysX integration, stamina, collision resolution (US-04, US-05) |
| WeaponHandling | Mechanism | Aim/ADS, fire, reload, recoil (US-06–US-09) |
| Ballistics | Mechanism | Advances in-flight bullet trajectories (US-10) |
| HitDetection | Mechanism | Resolves impact point + body part (US-11) |
| Damage | Mechanism (reads Data/Config) | Applies damage, marks death/spectator (US-12, US-13) |
| Scripts/Behaviours | Policy (Lua, sandboxed) | Win condition, round transitions, spawn logic (US-14, US-03) |
| Commit | Mechanism | Packages tick state into Authoritative State for Networking |

**Tooling** (offline, not shipped)
- Level baking tool — cleans up the OpenUSD-authored (ADR-0015) map with
  usd-optimize (dedup instances, flatten hierarchy, remove degenerate
  geometry), validates it with usd-validation-nvidia, then converts it into the
  engine's runtime level format, including Steam Audio baked
  reflection/occlusion data
- Asset cooker (`tools/asset-pipeline`, a pure-Python project - `cooker`
  console-script entry point) — walks the cleaned OpenUSD stage via
  usd-optimize's own `pxr` build, optimizes meshes via meshoptimizer and
  compresses textures via DirectXTex (BC7/BC5/BC4, DDS) through two small
  native pybind11 modules (`tools/asset-pipeline/cpp/`, per ADR-0025's
  vcpkg ports - neither links OpenUSD, see ADR-0030), and packages
  everything into signed, verified pack files (separate client and server
  packs) via a pure-Python reimplementation of augusta_assets' wire format.
  Full ordered pipeline: ADR-0030.

## 6. Runtime View

**Scenario: Fire Rifle**
1. Client predicts local fire feedback (muzzle flash, sound, recoil) immediately
2. Client sends fire input to server via GameNetworkingSockets
3. Server simulates bullet trajectory (custom ballistics: gravity, travel time)
4. Server resolves hit location against player hitboxes
5. Server applies damage by hit location, broadcasts authoritative result
6. Client reconciles: confirms/corrects predicted outcome (hit marker, damage state)

**Scenario: Player Movement with Reconciliation**
1. Client applies input locally (predicted movement)
2. Client sends input to server
3. Server simulates authoritative movement (PhysX)
4. Server broadcasts authoritative position/state
5. Client compares against its predicted state; if divergent, smoothly
   corrects (snap/blend) — no exact-replay assumption (see ADR-0004)

**Scenario: Round End**
1. Server evaluates win condition each tick (e.g., one side eliminated)
2. On match, server ends round, declares winner, broadcasts result
3. Server schedules next round start after a fixed delay

## 7. Deployment View
v1 gameplay: a Linux dedicated server process and up to 8 Windows client
processes, on the same LAN/localhost.

Non-production development/test deployment: the server also runs on a
self-hosted, single-node k3s cluster (developer's own hardware), two fixed,
long-lived Kubernetes namespaces (`staging` tracks `main`, `develop` tracks
`develop`) — no per-branch/ephemeral namespaces — see ENGINEERING.md,
Deployment & CD. LAN-only access; this removes the need for a separate
Linux VM/WSL2 just to run the server locally, since k3s now hosts it.

Production deployment (`main`) is explicitly out of scope/undecided for
now.

## 8. Crosscutting Concepts
- **Units:** 1 engine unit = 1 meter (real-world scale, required for realistic
  ballistics)
- **Threading:** fixed dedicated threads, no generic job/task scheduler in
  v1. Client: 3 threads (Main/Render, Simulation [ECS + PhysX], Network
  I/O). Server: 2 threads (Simulation, Network I/O) — no render thread,
  since it's headless (see ADR-0005).
- **Determinism strategy:** PhysX does not guarantee cross-platform bit-exact
  determinism (confirmed: NVIDIA docs state cross-platform determinism is
  unsupported). Client prediction is therefore treated as approximate/visual
  only; authoritative correction is applied via smooth snap/blend, never
  exact replay-and-diff.
- **Serialization:** custom lightweight binary format for game-state messages
- **Security:** server validates all client input (US-15); encryption
  deliberately deferred past v1 (trusted LAN testing only)
- **No I/O inside ECS worlds:** ECS worlds are pure state transformations.
  Device input, networking, rendering, and audio output are all handled by
  dedicated boundary components outside the worlds, which translate
  between the outside world and the data worlds consume/emit.
- **Mechanism vs. policy vs. data:** engine mechanism (movement, physics,
  ballistics, hit detection) is C++ code inside SimulationWorld's core
  phases; game policy (round lifecycle, win conditions, spawn rules) is
  encapsulated in sandboxed Lua scripts run in a dedicated
  Scripts/Behaviours phase; tunable balance values (e.g., damage by hit
  location/ammo type) are a third category — data-driven configuration,
  read by mechanism code but decided by neither the mechanism nor the
  policy scripts. Keeping these separate means gameplay rules and balance
  numbers can change without touching engine internals.
- **Scripting sandbox:** Lua scripts run with a restricted global
  environment — no `io`, `os.execute`, `package.loadlib`, or filesystem/
  network access — upholding "no I/O inside ECS worlds" structurally,
  not just by convention.
- **Asset packaging & integrity:** runtime assets ship as a single signed
  pack file per target (client/server), never as loose files. Content is
  hashed with BLAKE3 and signed with Ed25519; both the client and server
  take the pack path and the expected public key as external inputs
  (CLI arguments, issue #60) rather than embedding the public key in the
  binary, the private key never leaves the developer's machine. A failed
  verification refuses to load and exits with an error. Assets are
  addressed by relative path within the pack.

## 9. Architecture Decisions (ADRs)

Full decisions live in [`docs/adr/`](./adr/); ADR numbers are stable
identifiers assigned in decision order. The groupings below are a reading
aid only and do not affect numbering.

### Core Engine
- [ADR-0001 — ECS library: Flecs](./adr/0001-ecs-library.md)
- [ADR-0002 — Physics & ballistics](./adr/0002-physics-and-ballistics.md)
- [ADR-0003 — Networking transport: GameNetworkingSockets](./adr/0003-networking-transport.md)
- [ADR-0004 — Reconciliation model](./adr/0004-reconciliation-model.md)
- [ADR-0005 — Threading model](./adr/0005-threading-model.md)
- [ADR-0006 — Shared client/server codebase](./adr/0006-shared-client-server-codebase.md)
- [ADR-0007 — Serialization format](./adr/0007-serialization-format.md)

### Tooling & Build
- [ADR-0008 — Build tooling](./adr/0008-build-tooling.md)
- [ADR-0011 — Language standard: C++23](./adr/0011-language-standard.md)
- [ADR-0012 — Coding style](./adr/0012-coding-style.md)
- [ADR-0013 — Testing & benchmarking](./adr/0013-testing-and-benchmarking.md)
- [ADR-0025 — Dependency manager: vcpkg](./adr/0025-dependency-manager.md)

### Rendering & Audio
- [ADR-0009 — Renderer: NVIDIA Falcor](./adr/0009-renderer.md)
- [ADR-0010 — Audio: Steam Audio](./adr/0010-audio.md)
- [ADR-0014 — Shading language: Slang](./adr/0014-shading-language.md)
- [ADR-0028 — Audio output: miniaudio](./adr/0028-audio-output.md)

### Asset Pipeline
- [ADR-0015 — Map/level authoring format: OpenUSD](./adr/0015-map-authoring-format.md)
- [ADR-0016 — Mesh import & optimization](./adr/0016-mesh-import-and-optimization.md)
- [ADR-0017 — Texture compression](./adr/0017-texture-compression.md)
- [ADR-0018 — Runtime asset format](./adr/0018-runtime-asset-format.md)
- [ADR-0019 — Client/server pack split](./adr/0019-client-server-pack-split.md)
- [ADR-0020 — Audio asset format](./adr/0020-audio-asset-format.md)

### Client Runtime
- [ADR-0021 — Client runtime decomposition](./adr/0021-client-runtime-decomposition.md)
- [ADR-0024 — Client world phase pipelines](./adr/0024-client-world-phase-pipelines.md)

### Server Runtime
- [ADR-0022 — Gameplay scripting language: Lua](./adr/0022-gameplay-scripting-language.md)
- [ADR-0023 — SimulationWorld phase pipeline](./adr/0023-simulationworld-phase-pipeline.md)

### Infrastructure & CD
- [ADR-0026 — CD strategy: Flux for main/develop, push-based for ephemeral environments](./adr/0026-cd-strategy.md)

## 10. Quality Requirements
See [REQUIREMENTS.md](./REQUIREMENTS.md) — Non-Functional Requirements
(NFR-01 to NFR-06).

## 11. Risks and Technical Debt
- **PhysX cross-platform determinism gap:** client/server divergence is
  expected; mitigated by correction-based reconciliation, but may produce
  visible corrections ("rubber-banding") if divergence grows too fast.
- **Scope ambition vs. solo-dev bandwidth:** ECS + custom physics/ballistics +
  client prediction + multithreading + a new networking library is a lot of
  new surface area to learn and integrate simultaneously for v1.
- **GameNetworkingSockets build complexity:** pulls in transitive dependencies
  (protobuf, OpenSSL) that add cross-platform build maintenance overhead.
- **No encryption in v1:** acceptable only under the stated trusted-LAN
  assumption; must be revisited before any non-trusted deployment.
- **No automated test strategy defined yet** for physics/networking
  determinism-sensitive code — worth addressing early given the reconciliation
  risk above.
- **Falcor dependency** (see ADR-0009): a fork/vendor of the source is
  recommended to insulate against upstream abandonment.
- **Cross-OS local development:** building and testing requires both a
  Windows and a Linux environment simultaneously — largely mitigated now
  that the self-hosted k3s cluster (§7) serves as a standing Linux
  environment for the server, rather than needing a VM/WSL2 set up per
  session.
- **C++23 module support (`import std;`)** is still immature on both MSVC
  and GCC/Clang — avoid depending on it; stick to headers.
- **OpenUSD tooling weight** (see ADR-0015): revisit if it becomes
  disproportionate to actual map count/complexity.
- **Signing key management:** losing or leaking the pack-signing private
  key would require re-keying and re-signing all shipped packs — back it
  up securely and keep it out of version control.
- **Full rebake on every cook** is acceptable at v1's asset scale; will
  need incremental invalidation (e.g., content-hash-based) if asset count
  grows significantly.
- **Lua sandbox correctness:** security relies on a carefully curated
  restricted environment; an incomplete sandbox (e.g., leaking
  `load`/`dofile`, or a C++ binding that exposes filesystem/network
  access) would undermine the "no I/O inside worlds" guarantee. Audit the
  exposed API surface before shipping any script content.

## 12. Glossary
See [`CONTEXT.md`](../CONTEXT.md) at the repo root for the project's domain vocabulary.
