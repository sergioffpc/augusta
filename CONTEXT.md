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
The process of correcting a client's predicted state against the server's authoritative state: the client restores the server's state and replays the commands the server has not yet acknowledged from it, and presentation smooths the resulting jump.
_Avoid_: Resync, rollback

**Spawn point**:
A place in the Map where a player's feet are put when it joins, authored as a scene node in the pack (ADR-0032). The server takes them in order, starting over after the last, and tells the client which one it got; which player gets which is Game policy once round rules exist.
_Avoid_: Spawn location, start position

**Roster**:
Who is already in the match, each with its last known body, as told to a client when the server admits it. After that the Authoritative State lists everyone every tick.
_Avoid_: Player list, lobby

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
Tunable values (e.g. weapon/ammo damage, stamina rules) written as a Lua table script rather than expressed as mechanism code or policy scripts — a third category alongside mechanism and policy. A value may be an expression of other values; the script is evaluated at load into a plain immutable struct (**Parameters**), reloaded live by the server and sent to clients (ADR-0039).
_Avoid_: Config, settings (too generic — this specifically means simulation-tunable values, not startup settings, which the YAML files of ADR-0034 hold in place of command-line arguments)

**Parameters**:
The result of loading the data-driven configuration script (`parameters.lua`): a plain immutable struct of the simulation's tunable values, numbered by a generation that grows with each reload. Whatever tunable value client and server must agree on is a parameter: the server alone decides it and sends it to every client, which predicts with the server's numbers and never its own. The tick rate is not one: it is fixed for the life of the server process, so it is a startup setting in `augustad.yaml` that the server tells each client when it joins (ADR-0039).
_Avoid_: Tuning, settings; not a function's parameters

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
