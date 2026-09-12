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
  Steam Audio [Apache 2.0], Slang [Apache 2.0])
- **Language:** C++23 (avoid C++23 std modules/`import std` — still immature
  on MSVC and GCC/Clang)
- **Coding style:** Google C++ Style Guide
- **Testing:** GoogleTest (unit) + Google Benchmark (micro-benchmarks)
- **Content tooling:** OpenUSD (Tomorrow Open Source Technology License 1.0,
  Apache-derived) for offline map authoring/baking only — not linked into
  shipped client or server binaries
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
  against authoritative server snapshots (not exact replay — see ADR-04)
- Multithreaded from v1: dedicated Main/Render, Simulation, and Network I/O threads
- Custom lightweight binary protocol for game-state messages
- Mechanism vs. policy vs. data separation: engine mechanism (movement,
  physics, ballistics, hit detection) is C++; game policy (round
  lifecycle, win conditions, spawn rules) is encapsulated in sandboxed
  Lua scripts run in a dedicated Scripts/Behaviours phase; tunable
  balance values are data-driven configuration — a third category (see
  §8, ADR-22, ADR-23)
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
- ECS World (Flecs) — entities: players, bullets, round state
- Physics Layer — PhysX wrapper (collision, movement) + custom Ballistics module
- Networking Protocol — message definitions + custom binary serialization
- Match/Round State — round lifecycle, win conditions
- Level Data — lightweight custom runtime format, baked offline from
  OpenUSD source

**Client-only** (Windows-only)
- Input handling — reads device input, hands commands to PredictionWorld
- Networking — sends commands, receives authoritative server state
- ClientRuntime
  - PredictionWorld (ECS) — consumes commands + authoritative server
    state; runs client-side prediction and reconciliation (ADR-04); emits
    an immutable prediction state each simulation tick
  - PresentationWorld (ECS) — consumes the prediction state; interpolates/
    smooths for display; emits presentation state each render frame
- Renderer — NVIDIA Falcor (D3D12), shaders authored in Slang; consumes
  presentation state
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
| Reconciliation | Mechanism | Ingests any newly arrived authoritative state; applies smooth snap/blend correction (ADR-04) — no rollback/resimulate |
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
- Level baking tool — converts OpenUSD-authored maps into the engine's
  runtime level format, including Steam Audio baked reflection/occlusion data
- Asset cooker CLI — imports meshes via Assimp, optimizes via meshoptimizer,
  compresses textures via DirectXTex (BC7/BC5/BC4, DDS), and packages
  everything into signed, verified pack files (separate client and
  server packs)

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
   corrects (snap/blend) — no exact-replay assumption (see ADR-04)

**Scenario: Round End**
1. Server evaluates win condition each tick (e.g., one side eliminated)
2. On match, server ends round, declares winner, broadcasts result
3. Server schedules next round start after a fixed delay

## 7. Deployment View
v1 gameplay: a Linux dedicated server process and up to 8 Windows client
processes, on the same LAN/localhost.

Non-production development/test deployment: the server also runs on a
self-hosted, single-node k3s cluster (developer's own hardware), one
Kubernetes namespace per environment (`develop` persistent;
`feature/*`/`hotfix/*`/`release/*` ephemeral, torn down on branch
delete) — see ENGINEERING.md, Deployment & CD. LAN-only access; this
removes the need for a separate Linux VM/WSL2 just to run the server
locally, since k3s now hosts it.

Production deployment (`main`) is explicitly out of scope/undecided for
now.

## 8. Crosscutting Concepts
- **Units:** 1 engine unit = 1 meter (real-world scale, required for realistic
  ballistics)
- **Threading:** fixed dedicated threads, no generic job/task scheduler in
  v1. Client: 3 threads (Main/Render, Simulation [ECS + PhysX], Network
  I/O). Server: 2 threads (Simulation, Network I/O) — no render thread,
  since it's headless (see ADR-05).
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
  hashed with BLAKE3 and signed with Ed25519; the public key is embedded
  in each binary for load-time verification, the private key never leaves
  the developer's machine. A failed verification refuses to load and exits
  with an error. Assets are addressed by relative path within the pack.

## 9. Architecture Decisions (ADRs)

ADR numbers are stable identifiers assigned in decision order; the groupings
below are a reading aid only and do not affect numbering.

### Core Engine
- **ADR-01 — ECS library:** Flecs, for entity/component management shared
  across client and server.
- **ADR-02 — Physics:** PhysX for general collision/movement; ballistics
  implemented as a custom module (learning focus + determinism control).
- **ADR-03 — Networking transport:** GameNetworkingSockets (Valve) over UDP.
- **ADR-04 — Reconciliation model:** Approximate client-side prediction with
  smooth corrective reconciliation, not exact resimulation — driven by PhysX's
  documented lack of cross-platform determinism.
- **ADR-05 — Threading:** Multithreaded from v1, using fixed dedicated
  threads rather than a generic job system. Client: 3 threads (Main/Render,
  Simulation, Network I/O). Server: 2 threads (Simulation, Network I/O) —
  no render thread, since it's headless.
- **ADR-06 — Codebase structure:** Single shared codebase/core for client and
  dedicated server, to minimize simulation divergence.
- **ADR-07 — Serialization:** Custom binary format over a schema-compiler
  library, given the small and stable v1 message set.

### Tooling & Build
- **ADR-08 — Build tooling:** CMake + Ninja + sccache for cross-platform,
  fast, cached builds.
- **ADR-11 — Language standard:** C++23, avoiding std modules given current
  toolchain immaturity.
- **ADR-12 — Coding style:** Google C++ Style Guide.
- **ADR-13 — Testing/benchmarking:** GoogleTest + Google Benchmark.
- **ADR-25 — Dependency manager:** vcpkg (MIT, manifest mode via
  `vcpkg.json`), integrated in CI via `lukka/run-vcpkg`. Chosen over
  Conan for broader, more current coverage of this project's specific
  dependencies (PhysX 5.x, GameNetworkingSockets, meshoptimizer,
  DirectXTex — all stale or entirely absent on Conan Center) and
  simpler GitHub Actions integration. Also covers Steam Audio and
  OpenUSD via maintained vcpkg ports, replacing manual SDK download /
  build_usd.py. NVIDIA Falcor remains outside any package manager —
  vendored and built from source (it fetches its own sub-dependencies,
  e.g. Slang, via NVIDIA's internal Packman tool).

### Rendering & Audio
- **ADR-09 — Renderer:** NVIDIA Falcor (D3D12), Windows client only. The
  server is Linux-only and headless, fully decoupled from Falcor — its
  platform limitations (Linux/Vulkan experimental support) never come into
  play. Falcor is a research/prototyping framework, not built for shipping
  games, and has had no commits since Jan 2025 (~20 months stale as of
  writing).
- **ADR-10 — Audio:** Steam Audio (Apache 2.0), Windows client only.
- **ADR-14 — Shading language:** Slang (Apache 2.0). Already Falcor's default
  shader compiler/vendored dependency; formalized here as an explicit choice.
  Consideration: pin to Falcor's vendored Slang version, or take an
  independent/newer version if needed.

### Asset Pipeline
- **ADR-15 — Map/level authoring format:** OpenUSD, used purely as an
  offline authoring/interchange format. Maps are baked at build time into
  the engine's own lightweight runtime format; OpenUSD/Hydra and their
  toolchain are never linked into shipped binaries — consistent with the
  industry pattern (Remedy Northlight, Polyphony Digital) of
  USD-for-authoring → custom-runtime-format.
- **ADR-16 — Mesh import & optimization:** Assimp (multi-format import) +
  meshoptimizer (vertex cache optimization, simplification, quantization).
- **ADR-17 — Texture compression:** DirectXTex/texconv, baked to
  BC7/BC5/BC4 in DDS containers. KTX2 evaluated and rejected — no benefit
  without Basis Universal transcoding on a D3D12-only client.
- **ADR-18 — Runtime asset format:** a single signed, verified pack file,
  content-hashed with BLAKE3 and signed with Ed25519. Assets are addressed
  by relative path within the pack — no cross-pack GUID/manifest
  indirection layer at this scale (the pack's own internal table of
  contents, needed to resolve those relative paths, is an implementation
  detail, not a separate addressing system).
- **ADR-19 — Client/server pack split:** separate client pack (full:
  geometry, textures, meshes, audio) and server pack (stripped: collision
  geometry, spawn points, hitboxes), baked from the same source, to keep
  the headless Linux server lean.
- **ADR-20 — Audio asset format:** mono uncompressed PCM for v1 SFX,
  matching Steam Audio's per-source spatialization model.

### Client Runtime
- **ADR-21 — Client runtime decomposition:** ClientRuntime splits into two
  ECS worlds mapped onto the two client threads from ADR-05:
  PredictionWorld (Simulation thread, fixed tick — input + authoritative
  state in, immutable prediction state out) and PresentationWorld
  (Main/Render thread, per-frame — prediction state in, presentation state
  out to Renderer and Audio). Phase-level detail within each world is
  deferred to a future, more detailed diagram.
- **ADR-24 — PredictionWorld & PresentationWorld phase pipelines:**
  PredictionWorld: 5 phases (CommandIngestion, Reconciliation, Movement,
  WeaponHandling, Commit) — no Ballistics/HitDetection/Damage/
  Scripts-Behaviours; those remain exclusively server-side, consistent
  with the Fire Rifle scenario in §6 (client predicts only immediate
  feedback, never the bullet's outcome). PresentationWorld: 5 phases
  (Interpolation, Camera, Animation, AudioCues, Commit) —
  translates the fixed-tick Prediction State into smooth, frame-rate-
  independent visuals/audio. Neither client world contains a
  Scripts/Behaviours phase — game policy is exclusively server-authoritative.

### Server Runtime
- **ADR-22 — Gameplay scripting language:** Lua (MIT, lua.org reference
  implementation) embedded via sol2 (MIT, header-only C++ binding),
  running in a sandboxed environment (no io/os/package.loadlib) for the
  SimulationWorld's Scripts/Behaviours phase. Encapsulates game policy
  separately from mechanism code. LuaJIT considered and deferred: policy
  logic is low-frequency, not hot-path numeric work, so JIT performance is
  unnecessary, and LuaJIT's upstream is stalled (would mean depending on
  the OpenResty-maintained fork rather than lua.org directly). Python —
  already used for offline asset tooling via OpenUSD — is deliberately
  not reused here: it is not designed for embedding into a 60Hz real-time
  tick loop (CPython overhead, GIL).
- **ADR-23 — SimulationWorld phase pipeline:** eight ordered phases per
  tick — CommandIngestion, Movement, WeaponHandling, Ballistics,
  HitDetection, Damage, Scripts/Behaviours, Commit. Scripts/
  Behaviours runs last, after Damage resolves the tick's deaths, so it
  can evaluate win conditions and schedule round transitions/spawns for
  the next tick. Weapon/ammo damage values are data-driven configuration
  (not mechanism code or policy scripts) — a third category alongside
  mechanism and policy.

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
- **Falcor dependency** (see ADR-09): a fork/vendor of the source is
  recommended to insulate against upstream abandonment.
- **Cross-OS local development:** building and testing requires both a
  Windows and a Linux environment simultaneously — largely mitigated now
  that the self-hosted k3s cluster (§7) serves as a standing Linux
  environment for the server, rather than needing a VM/WSL2 set up per
  session.
- **C++23 module support (`import std;`)** is still immature on both MSVC
  and GCC/Clang — avoid depending on it; stick to headers.
- **OpenUSD tooling weight** (see ADR-15): revisit if it becomes
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
| Term | Meaning |
|---|---|
| ECS | Entity Component System — data-oriented architecture pattern |
| Tick | One discrete simulation step on the server |
| Authoritative server | Server holds the single source of truth for game state |
| Client-side prediction | Client simulates its own actions locally before server confirmation |
| Reconciliation | Process of correcting client-predicted state against server truth |
| Hitbox | Collision volume used to resolve bullet impacts against a player |
| ADS | Aim Down Sights |
| RTT | Round-Trip Time (network latency) |
