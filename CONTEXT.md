# Augusta

A physics-driven, server-authoritative multiplayer FPS simulator engine.

## Language

### Simulation & Networking

**ECS (Entity Component System)**:
The data-oriented architecture pattern the simulation core is built on — entities are IDs, components are plain data, systems operate on components.
_Avoid_: Game object, actor

**Tick**:
One discrete simulation step on the server, running at a fixed rate.
_Avoid_: Frame (a frame is a client-side render step, a separate concept from a tick)

**Authoritative server**:
The dedicated server, which holds the single source of truth for all gameplay-affecting state. Nothing a client reports is treated as fact until the server validates it.
_Avoid_: Trusted server, master client

**Client-side prediction**:
The client simulating its own actions locally, immediately, before the server confirms them — used purely for responsiveness.
_Avoid_: Client simulation

**Reconciliation**:
The process of correcting a client's predicted state against the server's authoritative state via smooth snap/blend, never by exact replay or resimulation.
_Avoid_: Resync, rollback

**RTT (Round-Trip Time)**:
The measured network latency between a client and the server for a single request/response cycle.
_Avoid_: Ping, lag

### Architecture

**Mechanism**:
Engine-side C++ code that provides a capability without deciding when or how it's invoked for gameplay purposes (e.g. the ballistics/hit-detection pipeline, the ECS phase runner).
_Avoid_: Engine code, core logic

**Game policy**:
Gameplay-specific rules (round lifecycle, win conditions, spawn rules) that decide how mechanism is used, implemented as sandboxed Lua in SimulationWorld's Scripts/Behaviours phase, kept out of C++ so it can change without touching mechanism code.
_Avoid_: Game logic, gameplay code (too broad — conflates policy with mechanism)

**Data-driven configuration**:
Tunable values (e.g. weapon/ammo damage) read from data files rather than expressed as mechanism code or policy scripts — a third category alongside mechanism and policy.
_Avoid_: Config, settings (too generic — this specifically means gameplay-tunable values, not engine/app configuration)

**Map**:
The static space a match is played in - its collision, spawn points and hitboxes - authored in OpenUSD (ADR-0015) and cooked into the signed pack. The shared `map` module builds its collision into the physics of both the client's PredictionWorld and the server's SimulationWorld.
_Avoid_: Level, stage (a *scene graph* is how the pack stores the map, not the map itself)

**Harness**:
Where anything that plays connects to the server: the client's network connection and PredictionWorld without a window or GPU. The real client, an automated test and a future autonomous agent each plug into one, supplying the input for every tick.
_Avoid_: Client session, bot

### Combat

**Hitbox**:
The collision volume attached to a player, used server-side to resolve where a bullet impacts.
_Avoid_: Collider (a collider is the general physics term; a hitbox is specifically the damage-resolution volume)

**ADS (Aim Down Sights)**:
The player action of aiming through a weapon's sights, trading movement/hip-fire speed for accuracy.
_Avoid_: Zoom, scope (those describe an effect of ADS, not the action itself)
