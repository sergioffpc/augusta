# Requirements — FPS Simulator Engine

Scope: the engine described in [VISION.md](./VISION.md) — multiplayer-only,
server-authoritative, physics-based ballistics, 2–8 players per Match. The v1
milestone ([ROADMAP.md](./ROADMAP.md)) played them with one rifle on one test
map.

---

## Functional Requirements

### US-01: Connect to Dedicated Server

As a player, I want to connect to a dedicated server, so that I can join a
match.

```text
Given a running dedicated server reachable on the network
When I initiate a connection with the server's engine version, the client pack cooked
     with its server pack, and a Character its scenario offers
Then the server accepts the connection and assigns me a session ID

Given a client whose engine version is not the server's, or whose client pack was not
      cooked with the server's
When I request to join
Then the server refuses me, telling me why (version or pack mismatch), admits me to no
     Lobby and assigns me no session ID; the client reports the reason and exits
```

A join that fails more than one check is told the first, in the order engine
version, client pack, Character, match in progress, Lobby full (ADR-0038,
ADR-0043).

### US-02: Join a Match (2–8 Players)

As a player, I want to join a match with 2 to 8 players, so that I can play.

```text
Given a Lobby holding fewer than the scenario's Player count, and no match in progress
When I request to join
Then I am admitted to the Lobby, and play in the next match once the Lobby is full
```

### US-03: Spawn into a Match

As a player, I want to spawn at the start of a match, so that I can participate.

```text
Given a new match has started
When spawning is processed
Then I am placed at a valid spawn point with full health and default loadout
```

### US-04: Move Player Character (Walk / Run / Crouch / Prone)

As a player, I want to move using walk, run, crouch, and prone stances, so that
I can navigate and use cover realistically.

```text
Given I am alive and not incapacitated
When I input a movement or stance command
Then my position and stance update accordingly on both client and server
```

### US-05: Manage Stamina

As a player, I want my stamina to deplete when running and recover when resting,
so that sustained sprinting has a realistic limit.

```text
Given I am sprinting
When stamina reaches zero
Then I am forced to slow to a walk until stamina recovers above a defined threshold
```

### US-06: Aim Weapon (Hip-fire / ADS)

As a player, I want to aim down sights or fire from the hip, so that I can trade
accuracy for speed.

```text
Given I have a weapon equipped
When I toggle aim down sights (ADS)
Then my view zooms and the weapon's accuracy/recoil model changes accordingly
```

### US-07: Fire Rifle

As a player, I want to fire my rifle, so that I can engage other players.

```text
Given I have ammo loaded and am not reloading
When I trigger fire
Then a bullet is spawned server-side with the correct origin, direction, and initial velocity
```

### US-08: Reload Rifle

As a player, I want to reload my rifle, so that I can continue fighting after
depleting my magazine.

```text
Given my magazine is not full
When I trigger reload and the reload completes
Then my ammo count is restored to the rifle's magazine capacity
```

### US-09: Apply Weapon Recoil

As a player, I want the rifle to recoil when fired, so that sustained fire is
harder to control realistically.

```text
Given I fire multiple rounds in succession
When each round is fired
Then my aim point shifts according to the weapon's recoil pattern, cumulative per burst
```

### US-10: Simulate Bullet Ballistics (Server-Authoritative)

As the system, I want the server to simulate each bullet's physics-based
trajectory, so that ballistics are realistic and consistent for all players.

```text
Given a bullet is fired
When it travels through the world
Then the server computes its trajectory, including gravity-induced drop and travel time,
and replicates the relevant result to clients
```

### US-11: Detect Hit by Impact Location

As the system, I want to detect precisely where a bullet impacts a player, so
that damage can be determined accordingly.

```text
Given a bullet's simulated trajectory intersects a player's hitbox
When the intersection is resolved server-side
Then the specific body part hit (e.g., head, torso, limb) is recorded
```

### US-12: Apply Damage by Hit Location

As the system, I want damage to depend on hit location and ammo type, so that
combat outcomes are realistic.

```text
Given a hit is detected at a given body part
When damage is applied
Then the resulting damage matches the configured lethality for that body part and ammo type
```

### US-13: Player Death (No Respawn)

As a player, I want to die permanently for the rest of the match when my health
reaches zero, so that matches carry real stakes.

```text
Given my health reaches zero
When death is processed
Then I enter spectator mode for the remainder of the match with no respawn
```

### US-14: Determine Match End / Win Condition

As the system, I want to detect when a match's win condition is met, so that the
match can conclude and a new one can begin.

```text
Given all players on one side are eliminated (or another defined win condition is met)
When the condition is evaluated server-side
Then the match ends, a winner is declared, everyone returns to the Lobby, and a new match
     starts after a defined delay
```

### US-15: Server-Side Validation of Client Input (Anti-Cheat Baseline)

As the system, I want the server to validate all client-reported actions, so
that clients cannot cheat by sending impossible data.

```text
Given a client reports an action (e.g., fire, move)
When the server validates it against physical and game constraints
Then invalid actions are rejected or corrected before affecting authoritative state
```

### US-16: Choose a Character

As a player, I want to choose the Character I play, so that I can pick among the
ones a scenario offers.

```text
Given augustac.yaml names a Character in player.character
When I join a server whose scenario offers that Character
Then I am admitted playing it, every client is told my Character in the Lobby's Roster
     and in Match start, and it stays my Character for the whole Session

Given augustac.yaml names a Character the server's scenario does not offer
When I request to join
Then the server refuses me as an unknown Character and I take no Lobby slot

Given augustac.yaml has no player.character, or an empty one
When the client starts
Then it refuses the config and does not connect
```

### US-17: See Other Players

As a player, I want to see the other players move smoothly, so that I can track
and engage them.

```text
Given I am in a Match with other players
When Authoritative State updates arrive, at irregular times
Then each other player is drawn as its Character's mesh, facing where it faces, at its
     position on the server's timeline one Interpolation delay (0.1 s) in the past,
     moving smoothly between the two surrounding updates

Given no newer Authoritative State has arrived
When frames keep rendering
Then each other player holds at its newest reported position, never extrapolated, and
     moves on without jumping backwards once updates resume
```

Stance is not drawn yet: it reaches the presentation, but the renderer draws a
Character as authored until animation poses it (ADR-0024).

### US-18: Hear Combat Audio Cues

As a player, I want to hear gunfire, hits, deaths and the Match's end, so that I
know what is happening around me without seeing it.

```text
Given I am in a Match
When I fire, another player's Shot is announced, the server confirms my hit,
     my health drops, a player dies, or the Match ends
Then I hear my own gunshot on the frame I fire, other gunshots and deaths from where
     they happened, and a hit marker, hit-taken or Match end stinger (won if I am the
     Winner, lost otherwise, a Draw included) as my own, each once
```

Spatialization itself (Steam Audio, ADR-0010, ADR-0028) is heard, not tested.

### US-19: See Combat Feedback

As a player, I want to see where shots go and when mine hit, so that I can
correct my aim and locate shooters.

```text
Given I am in a Match
When the server announces a Shot, or confirms one of mine hit
Then a tracer flies along the Shot's server-computed trajectory and leaves an impact
     where it meets the Map, the shooter's muzzle flashes (mine on the frame I fire),
     and on my Hit confirmation alone the hit marker shows for a moment
```

The aiming view (ADS zoom) is US-06's.

### US-20: Rebind Controls

As a player, I want to bind each control to a key of my choice, so that I can
play with the layout I am used to.

```text
Given an augustac.yaml whose input.keys section rebinds some controls
When the client starts
Then those controls answer to their new keys, their old keys do nothing, and every
     other control keeps its default

Given an input.keys entry that names an unknown control or key, binds a key another
      control has, binds Escape, or names a control twice
When the client starts
Then it refuses to start and names the offending entry

Given I am playing with the cursor captured
When I press Escape
Then the cursor is released and every held key let go, and keys and mouse do nothing
     until a mouse click, which recaptures the cursor without firing
```

### US-21: Record a Match and Replay It

As a developer, I want the server to record a Match and to replay that recording
on a fresh SimulationWorld, so that a bug seen in a playtest can be reproduced
and a non-determinism is caught.

```text
Given augustad.yaml names a file under simulation.recording
When the server runs and I replay that recording with augusta-replay on the server
     pack it names
Then every tick is handed the input the server handed SimulationWorld, in the same
     order, and the replay either confirms every tick resolved the recorded outcome or
     names the first tick that differs and what differed
```

A recording that stops early, because its disk fell behind or a write failed,
keeps every whole tick before that point and never holds the tick up (ADR-0048).

### US-22: Compose and Tune a Scenario

As a content author, I want to compose a scenario from a Map, its Characters,
its cue sounds and its scripts, and set its Parameters in a Lua script, so that
I can make and tune a game mode without changing engine code.

```text
Given a scenario manifest naming its Map, Characters, a sound for every cue, a
      Parameters script and, optionally, Rules
When I cook it and start a server on its server pack
Then a manifest with an unknown or missing entry is refused when cooked; a Parameters
     script with an unknown, missing, mistyped or out-of-range value, or that reaches
     outside its sandbox, stops the server at startup naming the field; otherwise every
     client is admitted with the server's tick rate and Parameters and plays by them
```

The Parameters set the rifle, its ammo, starting health, stamina and the Player
count; the tick rate is the server's config, not a Parameter (ADR-0039,
ADR-0041).

---

## Non-Functional Requirements

Quality attribute scenarios (Source / Stimulus / Environment / Artifact /
Response / Measure).

### NFR-01: Server Tick Rate (Performance)

```text
Source:      Dedicated server
Stimulus:    8 concurrent players sending input
Environment: Normal production conditions
Artifact:    Game loop / simulation core
Response:    Server maintains a stable simulation rate
Measure:     ≥ 60 Hz tick rate sustained, no missed ticks
```

### NFR-02: Network Latency Tolerance

```text
Source:      Player client
Stimulus:    Network latency up to 100 ms
Environment: Typical broadband connection
Artifact:    State synchronization system
Response:    Bullet and player state remain visually consistent
Measure:     Desync correction completes within 150 ms in 95% of cases
```

### NFR-03: Ballistics Determinism

```text
Source:      Ballistics simulation
Stimulus:    Identical fire event (position, direction, velocity, ammo type)
Environment: Any supported platform
Artifact:    Ballistics module
Response:    Trajectory computation produces identical results
Measure:     The server's trajectory is reproducible across runs and platforms
             within a defined tolerance
```

The tolerance is the one the golden-trajectory test defines, on the Windows and
Linux runners both (ADR-0013). Only the server decides where a bullet goes and
what it hits (ADR-0024); a client computes the same trajectory with the same
math only to draw it (ADR-0044), so the same check covers that drawing.

### NFR-04: Platform Targeting

```text
Source:      Build system
Stimulus:    Compilation request
Environment: Client build targets Windows x64; server build targets
             Linux x86-64, with Windows x64 for development only
Artifact:    Engine codebase (shared core, client, server)
Response:    Shared core (ECS, physics, ballistics, networking) compiles
             cleanly for both target platforms without platform-specific
             branches; client-only code (rendering) is Windows-only;
             server-only code runs in production on Linux only; any other
             target is refused when the build is configured
Measure:     Successful client build + v1 milestone playthrough on
             Windows; successful server build + v1 milestone playthrough
             on Linux; CI builds the server on both its platforms
```

The server's Windows build is for development (ADR-0047): it runs a match beside
the client on one machine, but nothing is released or deployed from it, and a
crash there leaves only the logged stack, no core dump or split debug info.
`cmake/AugustaPlatform.cmake` holds this contract, and the configure stops on
any other OS, architecture or 32-bit toolchain rather than compiling for it.

### NFR-05: Server Authority / Cheat Resistance

```text
Source:      Malicious or buggy client
Stimulus:    Client sends an impossible action (e.g., teleport, infinite ammo)
Environment: Live match
Artifact:    Server-side validation layer
Response:    Server rejects or corrects the invalid action
Measure:     100% of out-of-bounds actions rejected before affecting authoritative state
```

### NFR-06: Scalability Baseline

```text
Source:      Match host
Stimulus:    Players joining a match
Environment: A scenario whose Player count is 2–8
Artifact:    Server session management
Response:    Server supports the target concurrent player count without degradation
Measure:     Stable operation with 2–8 concurrent players for at least one full match
```

### NFR-07: Server Observability

```text
Source:      Developer watching a deployed server
Stimulus:    A playtest or load test running against develop or staging
Environment: k3s cluster with the monitoring stack (ADR-0049)
Artifact:    Dedicated server's metrics and liveness endpoint
Response:    Grafana shows the server's tick, Lobby and Match, Sessions and
             every connected client's Connection health; a server whose
             tick loop hangs is restarted
Measure:     Every metric in ADR-0049's catalogue present within one scrape
             interval (15 s) of the event; a tick loop stalled for 5 s
             fails the liveness probe
```

### NFR-08: Asset Integrity

```text
Source:      Anyone who can change a pack file on disk or on the node
Stimulus:    A client or server pack that is corrupted, truncated, tampered with,
             unsigned or signed by another key; or a client whose pack was not
             cooked with the server's
Environment: Client or server startup; a client joining
Artifact:    Pack loading and Join admission
Response:    The process refuses the pack and exits before anything else starts;
             the server refuses the Join as a pack mismatch
Measure:     No byte of a pack is trusted before its BLAKE3 hash and Ed25519
             signature verify against the key the process was given; every
             refused pack names why
```

ADR-0018 and ADR-0031 define the hash, the signature and the header's client
pack hash; ADR-0019 and ADR-0038 the Join's pack mismatch.

### NFR-09: Simulation Reproducibility

```text
Source:      Developer or CI replaying a Match recording
Stimulus:    Every tick's recorded input handed to a fresh SimulationWorld
Environment: The server pack the recording names; the build that recorded it,
             or any other supported build
Artifact:    SimulationWorld (phase pipeline and Game policy)
Response:    Every tick resolves the recorded outcome
Measure:     On the recording build, every value of every tick exactly equal;
             on another build, body positions and Shot origins within one
             position-grid step (1/1024 m) and velocities within two steps over
             a tick, every other value equal, each tick starting from the
             recorded bodies; the golden match replays on every CI runner
```

NFR-03 holds one bullet's trajectory to its tolerance; this holds the whole
simulation to the same one (ADR-0004, ADR-0048).

### NFR-10: Crash Diagnosability

```text
Source:      A defect in the dedicated server
Stimulus:    A fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) in a
             published Linux build
Environment: k3s node whose kernel.core_pattern is systemd-coredump's
Artifact:    The server's crash handler, its image and the build's debug info
Response:    The server logs the signal and the crashing thread's symbolized
             stack, then dies of the signal, and the kernel keeps its core
Measure:     Every such crash leaves a crash line and one line per frame in
             ADR-0029's format, a core, and debug info whose build ID matches
             the binary, kept as long as the image
```

A stack overflow, or a crash in another thread or in the report itself, leaves
no logged stack, and the development-only Windows build writes no core
(ADR-0047).

### NFR-11: Deployability

```text
Source:      Developer merging to develop or main
Stimulus:    A merged change to the server, its chart or an environment's values
Environment: k3s with Flux; GitHub-hosted CI, no runner in the cluster
Artifact:    Server image, Helm chart and HelmReleases
Response:    Flux deploys the commit's own image with no manual step; reverting
             the commit restores the previous image and pack
Measure:     Every deployed pod runs the image of its chart's commit; a
             rollback is one Git revert (no deploy-time target is decided)
```

CI lints and renders the chart and builds the image on every pull request that
touches them; no stage runs after a deploy (ADR-0013, ADR-0026).

### NFR-12: Robustness Against Malformed Input

```text
Source:      A peer, or a file from outside (pack, Match recording)
Stimulus:    Arbitrary bytes: empty, truncated, oversized, out of range or with
             bytes left over
Environment: Client or server at any time; the fuzzer under ASan
Artifact:    Protocol decoding, pack loading and Match recording reading
Response:    The input is refused with a typed error, nothing is allocated from
             a length that was not checked, and the process keeps running
Measure:     No crash, hang (5 s per input) or sanitizer report across every
             fuzz run (about 60 s per target per pull request, 30 min nightly);
             every payload decoding accepts re-encodes to the same bytes
```

The fuzz targets' seeds and regressions replay under ctest on every preset
(ADR-0013); what refused input does to authoritative state is NFR-05's.
