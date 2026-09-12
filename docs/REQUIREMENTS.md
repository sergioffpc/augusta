# Requirements — FPS Simulator Engine (v1)

Scope: v1 milestone as defined in [VISION.md](./VISION.md) — multiplayer-only,
server-authoritative, physics-based ballistics, 1 rifle, 1 test map, 2–8 players.

---

## Functional Requirements

### US-01: Connect to Dedicated Server
As a player, I want to connect to a dedicated server, so that I can join a match.

```
Given a running dedicated server reachable on the network
When I initiate a connection with a valid client version
Then the server accepts the connection and assigns me a session ID
```

### US-02: Join a Match (2–8 Players)
As a player, I want to join an active match with 2 to 8 players, so that I can play a round.

```
Given a match with fewer than 8 connected players
When I request to join
Then I am added to the match and notified of the current match state
```

### US-03: Spawn into a Round
As a player, I want to spawn at the start of a round, so that I can participate.

```
Given a new round has started
When spawning is processed
Then I am placed at a valid spawn point with full health and default loadout
```

### US-04: Move Player Character (Walk / Run / Crouch / Prone)
As a player, I want to move using walk, run, crouch, and prone stances, so that I can
navigate and use cover realistically.

```
Given I am alive and not incapacitated
When I input a movement or stance command
Then my position and stance update accordingly on both client and server
```

### US-05: Manage Stamina
As a player, I want my stamina to deplete when running and recover when resting, so that
sustained sprinting has a realistic limit.

```
Given I am sprinting
When stamina reaches zero
Then I am forced to slow to a walk until stamina recovers above a defined threshold
```

### US-06: Aim Weapon (Hip-fire / ADS)
As a player, I want to aim down sights or fire from the hip, so that I can trade accuracy
for speed.

```
Given I have a weapon equipped
When I toggle aim down sights (ADS)
Then my view zooms and the weapon's accuracy/recoil model changes accordingly
```

### US-07: Fire Rifle
As a player, I want to fire my rifle, so that I can engage other players.

```
Given I have ammo loaded and am not reloading
When I trigger fire
Then a bullet is spawned server-side with the correct origin, direction, and initial velocity
```

### US-08: Reload Rifle
As a player, I want to reload my rifle, so that I can continue fighting after depleting
my magazine.

```
Given my magazine is not full
When I trigger reload and the reload completes
Then my ammo count is restored to the rifle's magazine capacity
```

### US-09: Apply Weapon Recoil
As a player, I want the rifle to recoil when fired, so that sustained fire is harder to
control realistically.

```
Given I fire multiple rounds in succession
When each round is fired
Then my aim point shifts according to the weapon's recoil pattern, cumulative per burst
```

### US-10: Simulate Bullet Ballistics (Server-Authoritative)
As the system, I want the server to simulate each bullet's physics-based trajectory, so
that ballistics are realistic and consistent for all players.

```
Given a bullet is fired
When it travels through the world
Then the server computes its trajectory, including gravity-induced drop and travel time,
and replicates the relevant result to clients
```

### US-11: Detect Hit by Impact Location
As the system, I want to detect precisely where a bullet impacts a player, so that damage
can be determined accordingly.

```
Given a bullet's simulated trajectory intersects a player's hitbox
When the intersection is resolved server-side
Then the specific body part hit (e.g., head, torso, limb) is recorded
```

### US-12: Apply Damage by Hit Location
As the system, I want damage to depend on hit location and ammo type, so that combat
outcomes are realistic.

```
Given a hit is detected at a given body part
When damage is applied
Then the resulting damage matches the configured lethality for that body part and ammo type
```

### US-13: Player Death (No Respawn)
As a player, I want to die permanently for the rest of the round when my health reaches
zero, so that rounds carry real stakes.

```
Given my health reaches zero
When death is processed
Then I enter spectator mode for the remainder of the round with no respawn
```

### US-14: Determine Round End / Win Condition
As the system, I want to detect when a round's win condition is met, so that the round
can conclude and a new one can begin.

```
Given all players on one side are eliminated (or another defined win condition is met)
When the condition is evaluated server-side
Then the round ends, a winner is declared, and a new round starts after a defined delay
```

### US-15: Server-Side Validation of Client Input (Anti-Cheat Baseline)
As the system, I want the server to validate all client-reported actions, so that clients
cannot cheat by sending impossible data.

```
Given a client reports an action (e.g., fire, move)
When the server validates it against physical and game constraints
Then invalid actions are rejected or corrected before affecting authoritative state
```

---

## Non-Functional Requirements

Quality attribute scenarios (Source / Stimulus / Environment / Artifact / Response / Measure).

### NFR-01: Server Tick Rate (Performance)
```
Source:      Dedicated server
Stimulus:    8 concurrent players sending input
Environment: Normal production conditions
Artifact:    Game loop / simulation core
Response:    Server maintains a stable simulation rate
Measure:     ≥ 60 Hz tick rate sustained, no missed ticks
```

### NFR-02: Network Latency Tolerance
```
Source:      Player client
Stimulus:    Network latency up to 100 ms
Environment: Typical broadband connection
Artifact:    State synchronization system
Response:    Bullet and player state remain visually consistent
Measure:     Desync correction completes within 150 ms in 95% of cases
```

### NFR-03: Ballistics Determinism
```
Source:      Ballistics simulation
Stimulus:    Identical fire event (position, direction, velocity, ammo type)
Environment: Any supported platform
Artifact:    Ballistics module
Response:    Trajectory computation produces identical results
Measure:     Client and server compute matching trajectories within a defined tolerance
```

### NFR-04: Platform Targeting
```
Source:      Build system
Stimulus:    Compilation request
Environment: Client build targets Windows; server build targets Linux
Artifact:    Engine codebase (shared core, client, server)
Response:    Shared core (ECS, physics, ballistics, networking) compiles
             cleanly for both target platforms without platform-specific
             branches; client-only code (rendering) is Windows-only;
             server-only code is Linux-only
Measure:     Successful client build + v1 milestone playthrough on
             Windows; successful server build + v1 milestone playthrough
             on Linux
```

### NFR-05: Server Authority / Cheat Resistance
```
Source:      Malicious or buggy client
Stimulus:    Client sends an impossible action (e.g., teleport, infinite ammo)
Environment: Live match
Artifact:    Server-side validation layer
Response:    Server rejects or corrects the invalid action
Measure:     100% of out-of-bounds actions rejected before affecting authoritative state
```

### NFR-06: v1 Scalability Baseline
```
Source:      Match host
Stimulus:    Players joining a match
Environment: v1 milestone
Artifact:    Server session management
Response:    Server supports the target concurrent player count without degradation
Measure:     Stable operation with 2–8 concurrent players for at least one full round
```
