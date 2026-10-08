# arc42 Architecture Document — FPS Simulator Engine

## 1. Introduction and Goals

See [VISION.md](./VISION.md) for full vision. Summary: a realistic,
physics-driven, server-authoritative multiplayer FPS simulator engine, built in
C++ with a Windows client and a headless Linux server.

Top quality goals (see [REQUIREMENTS.md](./REQUIREMENTS.md) for full NFR list):

1. Server-authoritative correctness (NFR-05)
2. Realistic, consistent ballistics (NFR-03)
3. Stable performance under load (NFR-01, NFR-06)
4. Platform targeting, Windows client + Linux server (NFR-04)

## 2. Architecture Constraints

- **Technical:** C++, CMake + Ninja + sccache. Client: Windows-only, rendering
  via NVIDIA Falcor (D3D12). Server: Linux x86-64 in production (a Windows x64
  build for development only, NFR-04), headless.
- **Licensing:** third-party dependencies must be free/open-source (Flecs [MIT],
  PhysX [BSD-3], GameNetworkingSockets [BSD-3], Falcor [BSD-3], Steam Audio
  [Apache 2.0], miniaudio [MIT], Slang [Apache 2.0])
- **Language:** C++23 (avoid C++23 std modules/`import std` — still immature on
  MSVC and GCC/Clang)
- **Coding style:** Google C++ Style Guide
- **Testing:** GoogleTest (unit) + Google Benchmark (micro-benchmarks)
- **Content tooling:** OpenUSD (Tomorrow Open Source Technology License 1.0,
  Apache-derived) for offline map authoring/baking only — not linked into
  shipped client or server binaries. Same constraint for usd-optimize (Apache
  2.0, stage cleanup), usd-validation-nvidia (Apache 2.0 + CC-BY-4.0,
  validation), and Adobe's USD-Fileformat-plugins (Apache 2.0, glTF/FBX/OBJ
  ingestion as USD layers, ADR-0016) — all offline/build-time only.
- **Organizational:** solo developer / small informal team, no fixed deadline,
  milestone-driven

## 3. System Scope and Context

**Business context:** Players connect directly to a dedicated server via
IP:port.

**Technical context:**

- Client executable (Windows only): rendering (Falcor/D3D12), input, audio,
  local prediction
- Dedicated server executable (Linux in production, Windows for development
  only - NFR-04): headless, authoritative simulation
- Scripted players tool (`augusta-swarm`, Windows and Linux): a server's worth
  of headless clients, for load and end-to-end tests (ADR-0013)
- Communication: GameNetworkingSockets over UDP, encrypted (AES-GCM-256) but
  unauthenticated

```text
+--------+          +------------------------+
| Player |--------->| FPS Simulator Engine   |
+--------+          +------------------------+
```

## 4. Solution Strategy

- ECS-based simulation core (Flecs), shared between client and server
- PhysX for general collision/movement; custom-built ballistics module for
  bullet physics (the project's core learning focus)
- Server-authoritative model: server is the single source of truth for all
  gameplay-affecting state
- Client-side prediction for responsiveness, reconciled by restoring the
  authoritative server state and replaying the unacknowledged commands from it,
  with the visible jump smoothed in presentation (see ADR-0004)
- Multithreaded: dedicated Main/Render, Simulation, and Network I/O threads
- Custom lightweight binary protocol for game-state messages
- Mechanism vs. policy vs. data separation: engine mechanism (movement, physics,
  ballistics, hit detection) is C++; game policy (Match lifecycle, win
  conditions, spawn rules) is encapsulated in sandboxed Lua scripts run in a
  dedicated Scripts/Behaviours phase; tunable balance values are data-driven
  configuration — a third category (see §8, ADR-0022, ADR-0023)
- Rendering built on NVIDIA Falcor (D3D12), used exclusively by the Windows
  client. The server runs in production on Linux only (NFR-04), headless, and
  entirely decoupled from Falcor/graphics-API concerns.
- Audio via Valve's Steam Audio (Apache 2.0), used by the Windows client for
  spatial audio; no audio dependency on the headless Linux server.
- Shaders authored in Slang — already Falcor's default shader compiler (targets
  D3D12/HLSL); adopting it explicitly formalizes an existing transitive
  dependency rather than adding a new one.
- Level/map content authored in OpenUSD (interchange/authoring format only) and
  baked at build time into the engine's own lightweight runtime level format.
  OpenUSD, Hydra, and their toolchain (TBB/Boost/Python) are a content-pipeline
  dependency only — never linked into the shipped client or server.

## 5. Building Block View

```text
+--------+     +------------------------------------+
| Player |     |         FPS Simulator Engine        |
+--------+     |                                     |
    |          |    +--------+          +--------+   |
    +--------->|    | Client |<-------->| Server |   |
               |    +--------+          +--------+   |
               +-------------------------------------+
```

**Shared Core** (compiled into both client and server)

- ECS (Flecs) — shared entity/component data: players, bullets. Each World below
  is its own Flecs world instance built directly on the library; not a separate
  wrapped module in its own right.
- Physics — PhysX wrapper (collision, movement, rigid-body dynamics); one
  interface used identically by PredictionWorld and SimulationWorld. Props are
  simulated only by the server and moved kinematically on the client; Cosmetic
  bodies (ragdolls, debris) only on the client (ADR-0045). Particles are the
  renderer's, not physics'
- WeaponHandling — aim/ADS, fire, reload, recoil, ammo rules (augusta_weapon);
  one interface used identically by both Worlds. The client/server difference
  isn't in this module's logic, it's in what each World does with the result:
  the server treats it as authoritative and feeds Ballistics, the client uses it
  only for local predicted feedback pending reconciliation - same split as
  Physics.
- Ballistics — custom bullet trajectory simulation (gravity, travel time),
  hand-rolled instead of PhysX's generic projectile handling (ADR-0002). Its
  trajectory math is shared: the server advances every bullet with it, and each
  client's PresentationWorld draws every announced Shot's tracer and Map impact
  with it, a visual only. Deciding a bullet's outcome (hit detection, damage)
  stays server-side (ADR-0024, ADR-0044).
- Networking Protocol — message definitions + custom binary serialization
- Primitives — the Tick and command sequence widths and the player, command and
  recoil-kick bounds for the engine and the protocol to share
  (augusta_primitives), depending on no other module, so neither side needs the
  other's for them
- Command — one tick's player intent (augusta_command): what the client's Input
  handling samples and the server screens and simulates, so the server links no
  client input code
- Level Data — lightweight custom runtime format, baked offline from OpenUSD
  source. Its collision geometry is built into a physics::World's static meshes
  by one shared module (augusta_map), so PredictionWorld and SimulationWorld
  collide against the same map

**Client-only** (Windows-only)

- Input handling — turns keyboard/mouse events pushed by Renderer into commands
  for PredictionWorld. There is no separate Window module: Falcor fuses window
  creation with its GPU device/swapchain into one object (ADR-0009), so Renderer
  owns the OS window and pushes device events to Input rather than a third
  module managing the window handle independently
- Networking — sends commands, receives authoritative server state
- ClientRuntime
    - PredictionWorld (ECS) — consumes commands + authoritative server state;
      runs client-side prediction and reconciliation (ADR-0004); emits an
      immutable prediction state each simulation tick
    - PresentationWorld (ECS) — consumes the prediction state; interpolates/
      smooths for display; emits presentation state each render frame
- Renderer — NVIDIA Falcor (D3D12), shaders authored in Slang; owns the client's
  single OS window (see Input handling, above) and consumes presentation state.
  Exposes pumping window/device events and rendering a frame as two separate
  operations rather than one combined loop, so the Main/Render thread can drain
  events at a different cadence than it presents frames
- Audio — Steam Audio; consumes presentation state
- HUD/UI

```text
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

**PredictionWorld phases** (Simulation thread, fixed tick, once per tick, in
execution order: CommandIngestion → Reconciliation → Movement → WeaponHandling →
Commit)

| Phase            | Category  | Responsibility                                                                                                                                |
| ---------------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| CommandIngestion | Mechanism | Applies this tick's local input commands                                                                                                      |
| Reconciliation   | Mechanism | Ingests any newly arrived authoritative state; restores it and replays the unacknowledged commands from it (ADR-0004)                         |
| Movement         | Mechanism | Predicted PhysX movement, stamina                                                                                                             |
| WeaponHandling   | Mechanism | Predicts local fire feedback only (muzzle flash, sound cue, recoil, ammo count) — no bullet trajectory; hit/damage stays server-authoritative |
| Commit           | Mechanism | Packages the tick's predicted state into the immutable Prediction State                                                                       |

**PresentationWorld phases** (Main/Render thread, per render frame, in execution
order: Interpolation → Dynamics → Camera → Animation → AudioCues → Commit;
Dynamics is added with the client's first Prop or Cosmetic body, and until then
the other five run — ADR-0024)

| Phase         | Category  | Responsibility                                                                                                                                                                                                      |
| ------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Interpolation | Mechanism | Interpolates between the last two Prediction States, by the fraction of the tick elapsed at render time, for smooth motion at render frame rate                                                                     |
| Dynamics      | Mechanism | Moves Props to their interpolated poses and advances Cosmetic bodies: one fixed-step `simulate()` per tick Prediction advanced since the last frame, capped per frame (ADR-0045). Added with the first dynamic body |
| Camera        | Mechanism | View camera — position at the character's eye for the body's stance, orientation from the newest mouse-look every frame (not the tick's), ADS zoom transition, recoil kick decay, view bob                          |
| Animation     | Mechanism | Drives skeletal/procedural animation from interpolated movement and weapon state                                                                                                                                    |
| AudioCues     | Mechanism | Translates events carried in the Prediction State (e.g., fire, footstep) into spatialized audio cues                                                                                                                |
| Commit        | Mechanism | Packages the frame's presentation data into Presentation State                                                                                                                                                      |

Neither client world contains a Scripts/Behaviours phase — game policy is
exclusively server-authoritative.

**Server-only** (Linux in production, Windows for development; headless)

- Networking — receives client commands, sends authoritative state; the only
  server component that touches the network
- Input Validation — anti-cheat baseline (US-15); rejects/filters invalid
  commands before they reach the world (does not apply to outbound authoritative
  state)
- Match State — the Lobby, who is in each Match and its lifecycle (Join checks,
  Ready, Match start, Match end; ADR-0043); when a Match is won and over is game
  policy
- Scripting (Lua) — sandboxed script hooks for game policy (Match lifecycle, win
  conditions, spawn rules); small interface (load the scenario's scripts, call a
  hook by name with a read-only view, get back plain data C++ validates) hiding
  the Lua embedding and the restricted-environment sandbox (§8) that upholds "no
  I/O inside ECS worlds" structurally (ADR-0022). Server-only - game policy is
  exclusively server-authoritative, never run by either client world.
- ServerRuntime
    - SimulationWorld (ECS) — the single authoritative world (no prediction, no
      presentation needed). Runs mechanism systems in C++ (movement via PhysX,
      ballistics, hit detection, damage) and policy via a Scripts/Behaviours
      phase (Lua, sandboxed — Match lifecycle, win conditions, spawn rules);
      emits authoritative state each tick

```text
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

_(the Authoritative State emitted by `SimulationWorld` goes directly to
`Networking`, bypassing `Input Validation` — validation only applies to inbound
commands)_

**SimulationWorld phases** (executed in order, once per tick: Command Ingestion
→ Movement → Dynamics → WeaponHandling → Ballistics → HitDetection → Damage →
Scripts/Behaviours → Commit; Dynamics is added with the first Prop, and until
then the other eight run — ADR-0023). Each tick returns a `TickResult`: the
Authoritative State, its combat events included, and the Game policy actions
taken on it, typed and validated in C++ (ADR-0022), which `server::Host` acts on
after the tick.

| Phase              | Category                      | Responsibility                                                                                                                                 |
| ------------------ | ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| CommandIngestion   | Mechanism                     | Applies validated client commands to this tick's entities                                                                                      |
| Movement           | Mechanism                     | PhysX integration, stamina, collision resolution (US-04, US-05)                                                                                |
| Dynamics           | Mechanism                     | One fixed-step PhysX `simulate()`: Props, grenades, explosion impulses; reports Prop contacts for Damage (ADR-0045). Added with the first Prop |
| WeaponHandling     | Mechanism                     | Aim/ADS, fire, reload, recoil (US-06–US-09)                                                                                                    |
| Ballistics         | Mechanism                     | Advances in-flight bullet trajectories (US-10)                                                                                                 |
| HitDetection       | Mechanism                     | Resolves impact point + body part against hitboxes as they were the Shooter's delay ago (US-11, ADR-0044)                                      |
| Damage             | Mechanism (reads Data/Config) | Applies damage, marks death/spectator (US-12, US-13)                                                                                           |
| Scripts/Behaviours | Policy (Lua, sandboxed)       | Win condition, Match end, spawn logic (US-14, US-03); a hook's answer leaves as a typed action                                                 |
| Commit             | Mechanism                     | Packages tick state into Authoritative State for Networking                                                                                    |

**Tooling** (offline, not shipped)

- Level baking tool — cleans up the OpenUSD-authored (ADR-0015) map with
  usd-optimize (dedup instances, flatten hierarchy, remove degenerate geometry),
  validates it with usd-validation-nvidia, then converts it into the engine's
  runtime level format, including Steam Audio baked reflection/occlusion data
- Asset cooker (`tools/pack`, a pure-Python project - `augusta-pack`
  console-script entry point) — walks the cleaned OpenUSD stage via
  usd-optimize's own `pxr` build, optimizes meshes via meshoptimizer and
  compresses textures via DirectXTex (BC7/BC5/BC4, DDS) through two small native
  pybind11 modules (`tools/pack/cpp/`, per ADR-0025's vcpkg ports - neither
  links OpenUSD, see ADR-0030), and packages everything into signed, verified
  pack files (separate client and server packs) via a pure-Python
  reimplementation of augusta_assets' wire format. Full ordered pipeline:
  ADR-0030.

## 6. Runtime View

**Scenario: Fire Rifle**

1. Client predicts local fire feedback (muzzle flash, sound, recoil) immediately
2. Client sends the fire command to server via GameNetworkingSockets, with the
   Authoritative State tick it was showing and the interpolation fraction
3. Server fixes the Shooter's delay (capped at 250 ms) and announces the Shot
   (shooter, server tick, origin, direction) reliably to every client
4. Server simulates bullet trajectory (custom ballistics: gravity, travel time),
   testing each tick's segment against the Map and against player hitboxes as
   they were the Shooter's delay ago (lag compensation)
5. Server applies damage by hit location and sends the shooter a Hit
   confirmation (target, body part, damage)
6. Shooter's client shows the hit marker and sound only when the Hit
   confirmation arrives; it never predicts a hit
7. Every client draws each announced Shot's tracer and Map impact from its own
   computation of the same trajectory, a visual only; hits on players are drawn
   only from the server's hit messages (see ADR-0044)

**Scenario: Player Movement with Reconciliation**

1. Client applies input locally (predicted movement)
2. Client sends input to server
3. Server simulates authoritative movement (PhysX)
4. Server broadcasts authoritative position/state
5. Client compares against its predicted state after that same command; it
   restores the server's state and replays the commands sent since, and
   presentation smooths the jump (see ADR-0004)

**Scenario: Match End**

1. Server evaluates the win condition each tick (game policy, the scenario's
   rules; in v1 last player standing)
2. When it is met, the decision is a typed Match end action in that tick's
   result; the server ends the Match after the tick, removes every body and
   bullet in flight, and sends Match end, with the winner or a draw, reliably;
   everyone still connected returns to the Lobby
3. The next Match starts once the Lobby is full and Ready again, never less than
   5 seconds after the previous one ended (ADR-0043)

## 7. Deployment View

Linux dedicated server process and up to 8 Windows client processes, on the same
LAN/localhost.

Non-production development/test deployment: the server also runs on a
self-hosted, single-node k3s cluster (developer's own hardware), two fixed,
long-lived Kubernetes namespaces (`staging` tracks `main`, `develop` tracks
`develop`) — no per-branch/ephemeral namespaces — see ENGINEERING.md, Deployment
& CD. LAN-only access; this removes the need for a separate Linux VM/WSL2 just
to run the server locally, since k3s now hosts it.

Production deployment (`main`) is explicitly out of scope/undecided for now.

## 8. Crosscutting Concepts

- **Units:** 1 engine unit = 1 meter (real-world scale, required for realistic
  ballistics)
- **Threading:** fixed dedicated threads, no generic job/task scheduler. Client:
  3 threads (Main/Render, Simulation [ECS + PhysX], Network I/O). Server: 3
  threads (Simulation, Network I/O, Metrics) — no render thread, since it's
  headless (see ADR-0005, ADR-0049). Each piece of mutable state has one owning
  thread and crosses to another only as an immutable value: transport callbacks
  publish events that the Network I/O owner applies outside their locks, the
  client reads what the server said through one immutable Server view, and
  SimulationWorld returns a `TickResult`. A runtime supervisor owns the worker
  threads, their stop request and the first failure, which a runtime reports
  rather than terminating the process.
- **Determinism strategy:** PhysX does not guarantee cross-platform bit-exact
  determinism (confirmed: NVIDIA docs state cross-platform determinism is
  unsupported). Client prediction is therefore treated as approximate: the
  client restores the server's state and replays its own commands from it, never
  assuming the replay matches what the server did, and the next acknowledgement
  corrects what is left.
- **Serialization:** custom lightweight binary format for game-state messages
- **Security:** server validates all client input (US-15) and disconnects a peer
  that keeps sending malformed, impossible or out-of-turn data, past a threshold
  of such rejections within a sliding window, or that connects and is not
  admitted to the Lobby within a deadline (boundary constants, set so an honest
  client under NFR-02's latency and loss never reaches them; routine rejections
  never count). Every connection is encrypted, GameNetworkingSockets' default
  (AES-GCM-256, Curve25519 key exchange), but not authenticated: with no
  certificate authority, each peer presents a self-signed certificate, so a
  man-in-the-middle on the network goes undetected (trusted LAN only)
- **No I/O inside ECS worlds:** ECS worlds are pure state transformations.
  Device input, networking, rendering, and audio output are all handled by
  dedicated boundary components outside the worlds, which translate between the
  outside world and the data worlds consume/emit.
- **Mechanism vs. policy vs. data:** engine mechanism (movement, physics,
  ballistics, hit detection) is C++ code inside SimulationWorld's core phases;
  game policy (Match lifecycle, win conditions, spawn rules) is encapsulated in
  sandboxed Lua scripts run in a dedicated Scripts/Behaviours phase; tunable
  balance values (e.g., damage by hit location/ammo type) are a third category —
  data-driven configuration, read by mechanism code but decided by neither the
  mechanism nor the policy scripts. Keeping these separate means gameplay rules
  and balance numbers can change without touching engine internals.
- **Scripting sandbox:** Lua scripts run with a restricted global environment —
  no `io`, `os`, `package`, `require`, or filesystem/ network access — and an
  instruction limit per hook call, upholding "no I/O inside ECS worlds"
  structurally, not just by convention (ADR-0022).
- **Asset packaging & integrity:** runtime assets ship as a single signed pack
  file per target (client/server), never as loose files. Content is hashed with
  BLAKE3 and signed with Ed25519; both the client and server take the pack path
  and the expected public key as external inputs (from their YAML config file,
  ADR-0034) rather than embedding the public key in the binary, the private key
  never leaves the developer's machine. A failed verification refuses to load
  and exits with an error. Assets are addressed by relative path within the
  pack.

## 9. Architecture Decisions (ADRs)

Full decisions live in [`docs/adr/`](./adr/); ADR numbers are stable identifiers
assigned in decision order. The groupings below are a reading aid only and do
not affect numbering.

### Core Engine

- [ADR-0001](./adr/0001-ecs-library.md) — ECS library: Flecs
- [ADR-0002](./adr/0002-physics-and-ballistics.md) — Physics & ballistics
- [ADR-0003](./adr/0003-networking-transport.md) — Networking transport:
  GameNetworkingSockets
- [ADR-0004](./adr/0004-reconciliation-model.md) — Reconciliation model
- [ADR-0005](./adr/0005-threading-model.md) — Threading model
- [ADR-0006](./adr/0006-shared-client-server-codebase.md) — Shared client/server
  codebase
- [ADR-0007](./adr/0007-serialization-format.md) — Serialization format
- [ADR-0033](./adr/0033-error-handling.md) — Error handling: std::expected,
  exceptions and assertions by layer
- [ADR-0038](./adr/0038-networking-protocol-messages.md) — Networking Protocol:
  message catalogue and reliability split
- [ADR-0042](./adr/0042-character-selection.md) — Character selection: chosen in
  the client config, validated at join, replicated as an index
- [ADR-0044](./adr/0044-shot-lag-compensation-and-replication.md) — Shot lag
  compensation and replication
- [ADR-0045](./adr/0045-dynamic-bodies.md) — Dynamic bodies:
  server-authoritative Props, client-only cosmetics, fixed-step simulate
- [ADR-0048](./adr/0048-match-recording-and-replay.md) — (Superseded by
  ADR-0050, ADR-0051) Match recording and replay: SimulationWorld's input and
  outcome per tick, in the protocol's encoding
- [ADR-0050](./adr/0050-match-capture-and-reenactment.md) — Match capture and
  Reenactment: one Match's client actions, played again by augustac against a
  live server
- [ADR-0051](./adr/0051-replay.md) — Replay: augustad re-runs a Match capture
  and streams it to Replay viewers

### Tooling & Build

- [ADR-0008](./adr/0008-build-tooling.md) — Build tooling
- [ADR-0011](./adr/0011-language-standard.md) — Language standard: C++23
- [ADR-0012](./adr/0012-coding-style.md) — Coding style
- [ADR-0013](./adr/0013-testing-and-benchmarking.md) — Testing strategy
- [ADR-0025](./adr/0025-dependency-manager.md) — Dependency manager: vcpkg
- [ADR-0027](./adr/0027-logging.md) — Logging: `augusta::logging`, console-only
- [ADR-0029](./adr/0029-logging-policy.md) — Logging policy: level semantics and
  structured message format
- [ADR-0034](./adr/0034-runtime-config-file.md) — Runtime configuration: a YAML
  file next to the executable
- [ADR-0035](./adr/0035-boost-libraries.md) — Boost: individual libraries where
  the standard library stops
- [ADR-0036](./adr/0036-boost-log.md) — Logging library: Boost.Log replaces
  spdlog
- [ADR-0037](./adr/0037-include-order.md) — Include order: main header, standard
  library, third-party, project
- [ADR-0046](./adr/0046-documentation-site.md) — Documentation site: MkDocs
  Material for the docs, Doxygen for the C++ API, on GitHub Pages
- [ADR-0047](./adr/0047-server-crash-reports.md) — Server crash reports: kernel
  core dumps, a logged stack, and split debug info
- [ADR-0049](./adr/0049-metrics-and-liveness.md) — Metrics and liveness:
  Prometheus pull from augustad, kube-prometheus-stack via Flux

### Rendering & Audio

- [ADR-0009](./adr/0009-renderer.md) — Renderer: NVIDIA Falcor
- [ADR-0010](./adr/0010-audio.md) — Audio: Steam Audio
- [ADR-0014](./adr/0014-shading-language.md) — Shading language: Slang
- [ADR-0028](./adr/0028-audio-output.md) — Audio output: miniaudio

### Asset Pipeline

- [ADR-0015](./adr/0015-map-authoring-format.md) — Map/level authoring format:
  OpenUSD
- [ADR-0016](./adr/0016-mesh-import-and-optimization.md) — Mesh import &
  optimization
- [ADR-0017](./adr/0017-texture-compression.md) — Texture compression
- [ADR-0018](./adr/0018-runtime-asset-format.md) — Runtime asset format
- [ADR-0019](./adr/0019-client-server-pack-split.md) — Client/server pack split
- [ADR-0020](./adr/0020-audio-asset-format.md) — Audio asset format
- [ADR-0030](./adr/0030-asset-cooking-pipeline.md) — Asset cooking pipeline
- [ADR-0031](./adr/0031-pack-container-format.md) — Pack container format
- [ADR-0032](./adr/0032-scene-graph-format.md) — Runtime scene graph format
- [ADR-0040](./adr/0040-character-authoring-format.md) — Character authoring
  format & packing
- [ADR-0041](./adr/0041-scenario-composition-manifest.md) — Scenario composition
  manifest

### Client Runtime

- [ADR-0021](./adr/0021-client-runtime-decomposition.md) — Client runtime
  decomposition
- [ADR-0024](./adr/0024-client-world-phase-pipelines.md) — Client world phase
  pipelines

### Server Runtime

- [ADR-0022](./adr/0022-gameplay-scripting-language.md) — Gameplay scripting
  language: Lua
- [ADR-0023](./adr/0023-simulationworld-phase-pipeline.md) — SimulationWorld
  phase pipeline
- [ADR-0039](./adr/0039-lua-data-driven-configuration.md) — Data-driven
  configuration in Lua, shipped in the scenario's server pack
- [ADR-0043](./adr/0043-lobby-and-match-lifecycle.md) — Lobby and Match
  lifecycle: fixed player count, automatic Ready, no mid-match joins

### Infrastructure & CD

- [ADR-0026](./adr/0026-cd-strategy.md) — CD strategy: Flux for
  `main`/`develop`; no k3s deploy for ephemeral branches

## 10. Quality Requirements

See [REQUIREMENTS.md](./REQUIREMENTS.md) — Non-Functional Requirements (NFR-01
to NFR-07).

## 11. Risks and Technical Debt

- **PhysX cross-platform determinism gap:** client/server divergence is
  expected; mitigated by restore-and-replay reconciliation with the jump
  smoothed in presentation, but may produce visible corrections
  ("rubber-banding") if divergence grows too fast.
- **Scope ambition vs. solo-dev bandwidth:** ECS + custom physics/ballistics +
  client prediction + multithreading + a new networking library is a lot of new
  surface area to learn and integrate simultaneously for v1.
- **GameNetworkingSockets build complexity:** pulls in transitive dependencies
  (protobuf, OpenSSL) that add cross-platform build maintenance overhead.
- **No authentication of peers:** connections are encrypted, but neither side
  proves who it is, so a man-in-the-middle goes undetected. Acceptable only
  under the stated trusted-LAN assumption; certificates signed by a project
  certificate authority are needed before any non-trusted deployment.
- **Falcor dependency** (see ADR-0009): a fork/vendor of the source is
  recommended to insulate against upstream abandonment.
- **Cross-OS local development:** building and testing requires both a Windows
  and a Linux environment simultaneously — largely mitigated now that the
  self-hosted k3s cluster (§7) serves as a standing Linux environment for the
  server, rather than needing a VM/WSL2 set up per session.
- **C++23 module support (`import std;`)** is still immature on both MSVC and
  GCC/Clang — avoid depending on it; stick to headers.
- **OpenUSD tooling weight** (see ADR-0015): revisit if it becomes
  disproportionate to actual map count/complexity.
- **Signing key management:** losing or leaking the pack-signing private key
  would require re-keying and re-signing all shipped packs — back it up securely
  and keep it out of version control.
- **Full rebake on every cook** is acceptable at v1's asset scale; will need
  incremental invalidation (e.g., content-hash-based) if asset count grows
  significantly.
- **Lua sandbox correctness:** security relies on a carefully curated restricted
  environment; an incomplete sandbox (e.g., leaking `load`/`dofile`, or a C++
  binding that exposes filesystem/network access) would undermine the "no I/O
  inside worlds" guarantee. Audit the exposed API surface before shipping any
  script content.

## 12. Glossary

See [`CONTEXT.md`](../CONTEXT.md) at the repo root for the project's domain
vocabulary.
