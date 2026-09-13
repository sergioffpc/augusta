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

### Combat

**Hitbox**:
The collision volume attached to a player, used server-side to resolve where a bullet impacts.
_Avoid_: Collider (a collider is the general physics term; a hitbox is specifically the damage-resolution volume)

**ADS (Aim Down Sights)**:
The player action of aiming through a weapon's sights, trading movement/hip-fire speed for accuracy.
_Avoid_: Zoom, scope (those describe an effect of ADS, not the action itself)
