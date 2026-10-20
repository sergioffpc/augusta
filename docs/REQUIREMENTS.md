# Requirements — FPS Simulator Engine

Scope: the engine described in [VISION.md](./VISION.md) — multiplayer-only,
server-authoritative, physics-based ballistics, 2–8 players per Match in two
Teams, played with one rifle on one test map.

Each requirement names the release that first met it by its version (v1, v2), or
the one it is planned for by its theme (Teams): a planned release takes its
version only when it is cut (ENGINEERING.md, Milestones).

---

## Functional Requirements

### US-01: Connect to Dedicated Server

Version: v1

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

Version: v1

As a player, I want to join a match with 2 to 8 players, so that I can play.

```text
Given a Lobby holding fewer than the scenario's Player count, and no match in progress
When I request to join
Then I am admitted to the Lobby, and play in the next match once the Lobby is full
```

### US-03: Spawn into a Match

Version: v1

As a player, I want to spawn at the start of a match, so that I can participate.

```text
Given a new match has started
When spawning is processed
Then I am placed at a valid spawn point with full health and default loadout
```

### US-04: Move Player Character (Walk / Run / Crouch / Prone)

Version: v1

As a player, I want to move using walk, run, crouch, and prone stances, so that
I can navigate and use cover realistically.

```text
Given I am alive and not incapacitated
When I input a movement or stance command
Then my position and stance update accordingly on both client and server
```

In VR (US-25), walking in place moves me and my tracked body's posture sets my
stance: I crouch by crouching and go prone by lying down.

### US-05: Manage Stamina

Version: v1

As a player, I want my stamina to deplete when running and recover when resting,
so that sustained sprinting has a realistic limit.

```text
Given I am sprinting
When stamina reaches zero
Then I am forced to slow to a walk until stamina recovers above a defined threshold
```

### US-06: Aim Weapon (Hip-fire / ADS)

Version: v1

As a player, I want to aim down sights or fire from the hip, so that I can trade
accuracy for speed.

```text
Given I have a weapon equipped
When I toggle aim down sights (ADS)
Then my view zooms and the weapon's accuracy/recoil model changes accordingly
```

In VR (US-25), I aim by bringing the rifle's sight to my eye: there is no ADS
button and no zoom, and the recoil model changes once the sight is on my eye.

### US-07: Fire Rifle

Version: v1

As a player, I want to fire my rifle, so that I can engage other players.

```text
Given I have ammo loaded and am not reloading
When I trigger fire
Then a bullet is spawned server-side with the correct origin, direction, and initial velocity
```

The origin is the rifle's Muzzle, never my eye, in both Play modes (ADR-0064):
on a desktop the round crosses my sight at the rifle's zero range, and in VR it
leaves the barrel I hold, along it.

### US-08: Reload Rifle

Version: v1

As a player, I want to reload my rifle, so that I can continue fighting after
depleting my magazine.

```text
Given my magazine is not full
When I trigger reload and the reload completes
Then my ammo count is restored to the rifle's magazine capacity
```

In VR (US-25), I trigger reload with a button, with the same rules and timing.

### US-09: Apply Weapon Recoil

Version: v1

As a player, I want the rifle to recoil when fired, so that sustained fire is
harder to control realistically.

```text
Given I fire multiple rounds in succession
When each round is fired
Then my aim point shifts according to the weapon's recoil pattern, cumulative per burst
```

In VR (US-25), the rifle kicks in my hands with the same pattern and I bring it
back myself.

### US-10: Simulate Bullet Ballistics (Server-Authoritative)

Version: v1

As the system, I want the server to simulate each bullet's physics-based
trajectory, so that ballistics are realistic and consistent for all players.

```text
Given a bullet is fired
When it travels through the world
Then the server computes its trajectory, including gravity-induced drop and travel time,
and replicates the relevant result to clients
```

A bullet goes through what it strikes, or ricochets off it at a grazing angle,
by the struck surface's material and thickness and the bullet's remaining energy
(ADR-0065): glass and thin wood let it through, a concrete wall stops it.

### US-11: Detect Hit by Impact Location

Version: v1

As the system, I want to detect precisely where a bullet impacts a player, so
that damage can be determined accordingly.

```text
Given a bullet's simulated trajectory intersects a player's hitbox
When the intersection is resolved server-side
Then the specific body part hit (e.g., head, torso, limb) is recorded
```

A bullet goes on through a player it hits, with the energy that player's body
leaves it, and can hit the player behind (ADR-0065).

### US-12: Apply Damage by Hit Location

Version: v1

As the system, I want damage to depend on hit location and ammo type, so that
combat outcomes are realistic.

```text
Given a hit is detected at a given body part
When damage is applied
Then the resulting damage matches the configured lethality for that body part and ammo type
```

A bullet that went through something first does less damage, in proportion to
the energy it has left (ADR-0065).

### US-13: Player Death (No Respawn)

Version: v1

As a player, I want to die permanently for the rest of the match when my health
reaches zero, so that matches carry real stakes.

```text
Given my health reaches zero
When death is processed
Then I enter spectator mode for the remainder of the match with no respawn
```

In VR (US-25), my view fades out and reopens as a Spectator's; it never follows
my own body down.

### US-14: Determine Match End / Win Condition

Version: v1

As the system, I want to detect when a match's win condition is met, so that the
match can conclude and a new one can begin.

```text
Given every player of one Team is dead (or another defined win condition is met)
When the condition is evaluated server-side
Then the match ends, the winning Team is declared, everyone returns to the Lobby, and a
     new match starts after a defined delay
```

v1 met it with every player its own side, last player standing; Teams make the
sides two (ADR-0068).

### US-15: Server-Side Validation of Client Input (Anti-Cheat Baseline)

Version: v1

As the system, I want the server to validate all client-reported actions, so
that clients cannot cheat by sending impossible data.

```text
Given a client reports an action (e.g., fire, move)
When the server validates it against physical and game constraints
Then invalid actions are rejected or corrected before affecting authoritative state
```

### US-16: Choose a Character

Version: v1

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

Version: v1

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

Version: v1

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

Version: v1

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

Version: v1

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

### US-21: Capture a Match, Then Replay or Reenact It

Version: v3

As a developer, I want the server to capture a Match, to watch it again from any
player's eyes and to play it again against a live server, so that a crash, a
load problem or a moment seen in a playtest can be brought back.

```text
Given augustad.yaml names a directory under simulation.capture
When a Match is played to its Match end
Then the directory holds one Match capture of it, which augusta-inspect reads as
     its packs, its length, and each player's Character, spawn, Commands, Leave
     and Death

Given a replay server whose replay.captures holds that capture, on its server pack
When I run augustac --replay with the capture's name
Then I watch the Match from its first player's eyes, with that player's pitch and
     ADS, move on to the next player with fire, and the replay server logs the
     first Death or Match end that differs from the capture's, if any

Given a server with simulation.reenactments on, on the capture's packs
When I run augustac --reenact with the capture, once per player, all at once
Then each player spawns where the capture has it and its Commands reach the
     server on their ticks, each Seen time keeping its captured delay
```

A capture that stops early, because its disk fell behind or a write failed,
keeps every whole record before that point and never holds the tick up
(ADR-0050, ADR-0051).

### US-22: Compose and Tune a Scenario

Version: v1

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

### US-23: Script Players with Agents

Version: v3

As a developer, I want to control players from a Python script, so that a load
test, an end-to-end test or an AI can play the game with no one at the keyboard.

```text
Given a running server, its client pack, and a Python script that imports augusta_agent
When the script connects the scenario's Player count of Agents and sets their Intents
Then each Agent joins, is Ready in the Lobby and plays the Match: every tick it moves,
     aims at and fires on what its Intents name, and the script is told each Shot,
     Hit confirmation, Death, Match start and Match end, and each Intent that ends

Given an Agent aiming at another player, whose script takes seconds to decide
When the target moves meanwhile
Then the Agent's aim keeps following it, and the Agent sends a Command every tick

Given a server that refuses an Agent or ends its connection
When the script awaits anything of that Agent
Then it raises AgentFailed, saying why as augustac would
```

The Harness carries out Intents every tick; a script is never called on a tick,
so a slow one delays its own decisions, never a tick. `load_test.py` is the load
test (ADR-0013, ADR-0052).

### US-24: Join a Server with Room

Version: Scalable (planned)

As a player, I want my client to join whichever server of the scenario has room,
so that I can play when one server's Lobby is full or its Match is under way.

```text
Given augustac.yaml lists several servers of one scenario under network.servers
When a server refuses me with lobby full or match in progress
Then the client tries the next in turn and joins the first that admits me; if
     none does, it tells me every server is full or playing
```

Agents find a server the same way. Any other refusal - version, pack, Character

- stops at the server that gave it, since every server of the scenario would
  give it too (NFR-13).

### US-25: Play in VR

Version: VR (planned)

As a player, I want to play a Match on a PC VR headset, with my head, hands and
body tracked, so that I hold and aim my rifle and take cover with my own body,
in the same Matches as players on a desktop.

```text
Given a PC VR headset with an OpenXR runtime and cameras that see my body
When I join a Match in the VR Play mode
Then I see from my tracked head, my rifle is where my hands hold it, I am hit
     on my tracked body bounded to my Character's, and the other players see it
```

My camera images never leave my PC, and the Roster shows each player's Play mode
(ADR-0063).

### US-26: Play on a Team

Version: Teams (planned)

As a player, I want to fight on one of two Teams, each in its own Uniform, so
that I fight beside teammates I can tell from the enemy.

```text
Given a Match starts
When Game policy puts each player on a Team
Then I spawn at my Team's Spawn points wearing my Team's Uniform on the Character I
     chose, my rounds hurt teammates as they hurt enemies, once dead I watch only my
     living teammates, and my Team wins when the other has no one left alive
```

The free-for-all is gone: every Match is a team match (ADR-0068).

### US-27: Talk to my Team

Version: Teams (planned)

As a player, I want to talk to my Team by radio and to anyone near me aloud, so
that we coordinate, at the risk of being overheard.

```text
Given I am alive in a Match with a microphone
When I hold the radio push-to-talk and speak
Then my living teammates hear me by radio wherever they are, and anyone within range
     hears me aloud from where I stand, enemies included

Given I am dead
When I speak
Then only my dead teammates hear me, and I still hear my Team's radio
```

Speech is never captured, and there is none in the Lobby (ADR-0069).

---

## Non-Functional Requirements

Quality attribute scenarios (Source / Stimulus / Environment / Artifact /
Response / Measure).

### NFR-01: Server Tick Rate (Performance)

Version: v1

```text
Source:      Dedicated server
Stimulus:    8 concurrent players sending input
Environment: Normal production conditions
Artifact:    Game loop / simulation core
Response:    Server maintains a stable simulation rate
Measure:     ≥ 60 Hz tick rate sustained, no missed ticks
```

### NFR-02: Network Latency Tolerance

Version: v1

```text
Source:      Player client
Stimulus:    Network latency up to 100 ms
Environment: Typical broadband connection
Artifact:    State synchronization system
Response:    Bullet and player state remain visually consistent
Measure:     Desync correction completes within 150 ms in 95% of cases
```

### NFR-03: Ballistics Determinism

Version: v1

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

Version: v1

```text
Source:      Build system
Stimulus:    Compilation request
Environment: Client build targets Windows x64 and Linux x86-64;
             server build targets Linux x86-64 and Linux arm64, with
             Windows x64 for development only
Artifact:    Engine codebase (shared core, client, server)
Response:    Shared core (ECS, physics, ballistics, networking) compiles
             cleanly for every target platform without platform-specific
             branches; client-only code (rendering) builds for
             Windows and Linux x86-64; server-only code runs in
             production on Linux only; any other server target is
             refused when the build is configured
Measure:     Successful client build + Match playthrough on
             Windows; successful server build + Match playthrough
             on Linux; CI builds the server on Linux x86-64 and
             Windows x64, and each release on both Linux architectures;
             the nightly builds and tests the client on Linux x86-64
```

The server's Windows build is for development (ADR-0047): it runs a match beside
the client on one machine, but nothing is released or deployed from it, and a
crash there leaves only the logged stack, no core dump or split debug info.

Linux arm64 is a production platform, built only for what ships: each release
builds and tests `augustad-linux-arm64` beside `augustad-linux-x64`, and
`main`'s server image, the one a release runs, is built for both architectures.
Pull requests and `develop` build Linux x86-64 alone, so an arm64-only break
shows at the merge into `main` or the release's tag, not before. Each is built
natively, on an arm64 runner; the dev container on an arm64 host (an Apple
silicon Mac, `scripts/dev-container.sh`) builds it natively too.

`cmake/AugustaPlatform.cmake` holds this contract, and the configure stops on
any other OS, architecture or 32-bit toolchain rather than compiling for it.

### NFR-05: Server Authority / Cheat Resistance

Version: v1

```text
Source:      Malicious or buggy client
Stimulus:    Client sends an impossible action (e.g., teleport, infinite ammo)
Environment: Live match
Artifact:    Server-side validation layer
Response:    Server rejects or corrects the invalid action
Measure:     100% of out-of-bounds actions rejected before affecting authoritative state
```

### NFR-06: Match Capacity

Version: v1

```text
Source:      Match host
Stimulus:    Players joining a match
Environment: A scenario whose Player count is 2–8
Artifact:    Server session management
Response:    Server supports the target concurrent player count without degradation
Measure:     Stable operation with 2–8 concurrent players for at least one full match
```

### NFR-07: Server Observability

Version: v2

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

Version: v1

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

### NFR-09: Replay Consistency

Version: v3

```text
Source:      Developer replaying a Match capture
Stimulus:    The capture's players, spawns, Leaves and Commands handed to a fresh
             SimulationWorld on a replay server, tick by tick
Environment: The server pack the capture names; the build that captured it
Artifact:    SimulationWorld (phase pipeline and Game policy)
Response:    Every Death and the Match end come out as captured
Measure:     Each Death's victim, killer and tick and the Match end's tick and
             winner equal to the capture's; the first that differs logged
```

Only the Deaths and the Match end are compared, and only on the build that
captured the Match: a Replay on another build may drift as floats round
differently, and nothing re-syncs it (ADR-0051).

### NFR-10: Crash Diagnosability

Version: v2

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

Version: v1

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

Version: v1

```text
Source:      A peer, or a file from outside (pack, Match capture)
Stimulus:    Arbitrary bytes: empty, truncated, oversized, out of range or with
             bytes left over
Environment: Client or server at any time; the fuzzer under ASan
Artifact:    Protocol decoding, pack loading and Match capture reading
Response:    The input is refused with a typed error, nothing is allocated from
             a length that was not checked, and the process keeps running
Measure:     No crash, hang (5 s per input) or sanitizer report across every
             fuzz run (about 60 s per target per pull request, 30 min nightly);
             every payload decoding accepts re-encodes to the same bytes
```

The fuzz targets' seeds and regressions replay under ctest on every preset
(ADR-0013); what refused input does to authoritative state is NFR-05's.

### NFR-13: Horizontal Scalability

Version: Scalable (planned)

```text
Source:      Players of one scenario, more than one Match's Player count of them
Stimulus:    Another Match's worth of players while every server of the scenario
             has a full Lobby or a Match in progress
Environment: k3s with Flux; several servers of the same scenario on the cluster
Artifact:    Server deployment (Helm chart and HelmReleases) and how a client
             finds a server with room
Response:    Another server of the scenario runs beside the others, with its own
             Lobby and Match, with no code change, and degrades none of them
Measure:     At least 4 servers of one scenario run 8-player Matches at once,
             each holding NFR-01 (60 Hz, no missed ticks); adding or removing a
             server is one Git change (NFR-11)
```

Planned for a later release, and not met by v1 or v2: they run one server per
scenario per environment (ADR-0026), whose Deployment keeps `replicas: 1`
because each replica would be another Lobby and Match behind the same address.
NFR-06 is one server's capacity for one Match; this requirement is how many
Matches run at once, one server each.

### NFR-14: Client-side Observability Under Load

Version: Scalable (planned)

```text
Source:      Developer running a load test
Stimulus:    A load test of Agents against develop or staging
Environment: k3s cluster with the monitoring stack (ADR-0049)
Artifact:    The load test Job and its Agents' metrics endpoint
Response:    Grafana shows each Agent's Reconciliation corrections,
             interpolation running dry, fire and Connection health beside the
             servers' metrics
Measure:     Starting or stopping a load test of up to 32 Agents is one Git
             change (NFR-11); every Agent metric is present within one scrape
             interval (15 s)
```

NFR-07 watches the server; this requirement watches what its clients see of it,
which is what NFR-13's "degrades none of them" is measured by.

### NFR-15: Cluster Hardening (Security)

Version: v3

```text
Source:      A malicious game client, a compromised LAN device, a revision or
             image that was not ours, or a stolen operator credential
Stimulus:    An exploit of augustad, a connection to the Node, a push or image
             reaching GitHub or GHCR, or a stolen kubeconfig or SSH key
Environment: The Node (k3s with Flux), LAN-only, both Environments running
Artifact:    The Node's OS, kernel and k3s, admission policies, Flux sources,
             the augustad chart and operator credentials
Response:    The cluster runs only signed revisions and signed or
             digest-pinned images, admits no pod past its namespace's Pod
             Security level, allows no flow it did not name, holds no Secret
             but its alerts' bot token, never in Git, and a stolen
             credential cannot change workloads or the Node; a process that
             escapes augustad's container finds kernel addresses hidden, no
             ptrace, no kexec, no unprivileged user namespace and no ICMP
             redirect
Measure:     kube-bench reports no CIS k3s failure outside the recorded
             exceptions; every Kyverno policy enforced, CI refusing each
             policy's negative fixture; the Node runbook's negative checks all
             refused; every hardening kernel parameter reads back its value on
             every interface after a reboot; a LAN port scan shows only the
             game ports and Grafana
```

Planned for v3, and not met by v1 or v2: they run k3s on its defaults, with no
admission policy, signature check or NetworkPolicy (ADR-0057, ADR-0056). The
Node stays LAN-only; exposing a game port range to the internet is outside this
requirement, but it is one firewall rule once this one is met.

### NFR-16: VR Frame Rate

Version: VR (planned)

```text
Source:      A VR player
Stimulus:    Playing a full Match on the reference hardware (NFR-17's GPU)
Environment: A Match of 8 players, Desktop and VR mixed
Artifact:    The client's VR rendering
Response:    The client renders both eyes at the headset's native refresh
             rate
Measure:     No dropped frames at the headset's refresh rate (90 Hz on the
             reference headset) for the whole Match
```

A frame the headset waits for is reprojected by its runtime, which makes it
uncomfortable to play, not slower: a dropped frame counts as a failure, whatever
the runtime makes of it (ADR-0063).

### NFR-17: Desktop Frame Rate

Version: The Drawn Body (planned)

```text
Source:      A Desktop player
Stimulus:    Playing a full Match on the reference hardware, an NVIDIA
             GeForce RTX 5070 Ti with 16 GB
Environment: A Match of 8 players at 1440p, the upscaler on its default
             preset
Artifact:    The client's Desktop rendering
Response:    The client renders every frame with all of its lighting
Measure:     144 frames a second or more for the whole Match
```

Lighting has no quality setting to trade for frame rate (ADR-0067): the frame
rate is met with every player's shadows, reflections and indirect light as
everyone else sees them.
