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

**Match**:
One game played by the scenario's Player count, from Match start until Game policy ends it. Its players are fixed when it starts: no one joins, and a player who disconnects leaves it and its body is removed. When it ends, everyone still connected returns to the Lobby (ADR-0043).
_Avoid_: Game (too broad: the product is a game), Round (a synonym; a Match is the one unit of play, with no respawn until it ends)

**Lobby**:
Where admitted players wait for the next Match, up to the Player count. Players come and go freely; it closes when a Match starts and reopens when it ends, and the next Match waits at least 5 seconds after that (ADR-0043).
_Avoid_: Waiting room, pre-game

**Player count**:
How many players a Match needs to start, fixed by the scenario's Parameters.
_Avoid_: Max players (the protocol's upper bound on any scenario's player count, not a scenario's own number)

**Ready**:
A player whose client has loaded what it needs to draw everyone currently in the Lobby. The client decides it on its own; the player never presses anything.
_Avoid_: Loaded, prepared

**Match start**:
The moment the Lobby is full and every player is Ready, at least 5 seconds after the previous Match ended: the server closes the Lobby and tells every client who is in the Match, with each player's Character, the Entity ID of the body it controls, and Spawn point.
_Avoid_: Spawn (a player's body is placed at Match start, but "spawn" names the placement, not the start of the match)

**Match end**:
The moment Game policy decides a Match is over (in v1, when at most one player is left alive: last player standing), or its last player leaves. The server takes every body and bullet out of the simulation and tells every client still in the Match, with the Winner or a Draw; everyone returns to the Lobby (ADR-0043).
_Avoid_: Game over, round end

**Winner**:
The player Game policy declares has won a Match, alive in it when declared, named by its Session ID in Match end.
_Avoid_: Victor, champion

**Draw**:
A Match end with no Winner: its last players died on the same tick, or a Match of one ended with its player's death.
_Avoid_: Tie, stalemate

**Session**:
One player's presence on the server, from being admitted to the Lobby until it disconnects, named by its Session ID. It spans the Lobby and every Match the player plays in between. The client's own connection and PredictionWorld pairing (augusta::harness::Session) is the client-side implementation of one.
_Avoid_: Connection (a session is the gameplay identity kept for the life of the player's presence on the server; the transport connection beneath it, augusta::networking's own peer handle, can in principle outlive or be distinct from it)

**Session ID**:
The Authoritative server's name for one connected player (harness::SessionId on the client, server::SessionId and simulation::SessionId on the server, protocol::SessionIdWire on the wire), assigned when it admits the join. Distinct from the transport's own handle for the connection, and not a credential — the server tells senders apart by connection, not by this ID.
_Avoid_: Player ID, connection ID

**Entity ID**:
The Authoritative server's name for one dynamic body (server::EntityId and simulation::EntityId on the server, harness::EntityId and presentation::EntityId on the client, protocol::EntityIdWire on the wire): a player's body today, later anything that moves. It names which body, not who moves it: a player's body gets one at Match start, which pairs it with that player's Session ID, and a body no player controls has one and no Session. Never reused.
_Avoid_: Player ID (a body is not a player), using a Session ID to name a body

**Prop**:
A dynamic body that affects gameplay (it blocks players and bullets, can hurt a player, and is moved by players, bullets and explosions), simulated only by the Authoritative server and named by an Entity ID. Placed in the Map or created during a Match, like a thrown grenade (ADR-0045).
_Avoid_: Object, physics object, dynamic object, item

**Cosmetic body**:
A dynamic body that exists on one client only and never affects gameplay, such as a ragdoll or debris. It is never sent and may differ between clients (ADR-0045).
_Avoid_: Prop (a Prop is gameplay), effect, particle (particles are the renderer's, not physics)

**Client-side prediction**:
The client simulating its own actions locally, immediately, before the server confirms them — used purely for responsiveness.
_Avoid_: Client simulation

**Command**:
One tick's local input (command::Command, in the shared core), with the Seen time of the frame it was sampled on, a client sends the Authoritative server under a growing sequence number, so the server can tell what it has already seen and the client can tell what it has not yet acknowledged. What Client-side prediction applies locally and Reconciliation replays.
_Avoid_: Input (Command is the sequenced payload sent to the server each tick; augusta::input::Input is the per-frame local sampler that produces one)

**Reconciliation**:
The process of correcting a client's predicted state against the server's authoritative state: the client restores the server's state of its body and its rifle and replays the commands the server has not yet acknowledged from it, and presentation smooths the resulting jump.
_Avoid_: Resync, rollback

**Authoritative State update**:
One server tick's Authoritative State as sent to one client (protocol::AuthoritativeStateWire): every body as of that tick, by its Entity ID, plus the recipient's own newest acknowledged Command sequence and its own rifle. augusta::replication decides who gets what.
_Avoid_: Snapshot, state sync

**Spawn point**:
A place in the Map where a player's feet are put at Match start, authored as a scene node in the pack (ADR-0032). Which player gets which is Game policy: the scenario's `assign_spawns` hook assigns them once, at Match start (ADR-0022, ADR-0023), and players take them in order, starting over after the last, when it is undefined or its answer is refused. Every client is told where each player spawned in Match start.
_Avoid_: Spawn location, start position

**Roster**:
Who is in the Lobby, each with its Character, as the server tells every client in it whenever it changes. The version a client loaded for is what its Ready names.
_Avoid_: Player list

**Character**:
What a player plays as, meaning its body's look and its collider, chosen from the characters the scenario's manifest names. The player picks one before joining, the server admits the join only if the scenario has it, and it stays fixed for the whole Session, across every Match in it (ADR-0042).
_Avoid_: Skin, model, avatar (a character is not only appearance: its collider is gameplay)

**Interpolation delay**:
How far in the past, about 100 ms, a client shows other players, so it always has two Authoritative State updates to interpolate between.
_Avoid_: Lerp delay, buffer time, lag

**Match recording**:
What the Authoritative server handed SimulationWorld on each tick of its run and what the tick resolved, written to a file when its config asks for one, so a Replay can re-run it (ADR-0048). Every tick from the server's start, Lobby ticks included, not one Match alone.
_Avoid_: Demo, replay file (a Replay is what is done with a recording)

**Replay**:
Handing a fresh SimulationWorld, built from the same server pack, every tick's recorded input and checking each tick resolves the recorded outcome: exactly on the build that made the recording, within a grid step of position on any other, each tick starting from the recorded bodies (ADR-0048).
_Avoid_: Playback, rewind (Lag compensation looks back in time; a Replay re-runs a whole recording), re-simulation (Reconciliation's replay of unacknowledged commands is the client's own)

**Seen time**:
The moment of the server's timeline a client's frame shows the other players at: the tick of an Authoritative State update and how far from it to the next one, 0 to 1 (presentation::SeenTime). Every Command reports the Seen time of the frame it was sampled on, and the server judges its rounds against the other players as they were then (ADR-0044).
_Avoid_: View, shown view, view tick (a view is where a player looks: a yaw and a pitch)

**RTT (Round-Trip Time)**:
The measured network latency between a client and the server for a single request/response cycle.
_Avoid_: Ping, lag

**Connection health**:
How well a client's transport connection is delivering, as the server's own transport measures it: its RTT, packet loss each way, jitter, throughput and send queue. A property of the transport connection, not of the Session above it: what happens to a Session (admitted, refused, left mid-Match) is not part of it.
_Avoid_: Connection quality (the transport's own 0..1 delivery rate, one input of it), network health, lag

### Architecture

**Mechanism**:
Engine-side C++ code that provides a capability without deciding when or how it's invoked for gameplay purposes (e.g. the ballistics/hit-detection pipeline, the ECS phase runner).
_Avoid_: Engine code, core logic

**Game policy**:
Gameplay-specific rules (Match lifecycle, win conditions, spawn rules) that decide how mechanism is used, implemented as sandboxed Lua in SimulationWorld's Scripts/Behaviours phase, kept out of C++ so it can change without touching mechanism code.
_Avoid_: Game logic, gameplay code (too broad — conflates policy with mechanism)

**Policy action**:
One decision of Game policy as the server receives it: a hook's answer read into one of a closed set of C++ types and validated as it is read (ADR-0022), such as a Match end. The server acts on policy actions; it never reads a hook's answer itself.
_Avoid_: Policy result, script output

**Tick result**:
What one SimulationWorld tick returns (simulation::TickResult): the tick's Authoritative State, its combat events included, and the Policy actions taken on it (ADR-0023).

**Server view**:
Everything the server has told one client, as of one moment (harness::ServerView): published whole by the client's Network I/O thread for every message that changes it and never changed after, so a reader on another thread sees one moment (ADR-0005).
_Avoid_: Snapshot (an Authoritative State update is not one), network state

**Data-driven configuration**:
Tunable values (e.g. weapon/ammo damage, stamina rules) written as a Lua table script rather than expressed as mechanism code or policy scripts — a third category alongside mechanism and policy. A value may be an expression of other values; the script is authored in the scenario's folder next to its stage, cooked into the scenario's server pack, evaluated once at server startup into a plain immutable struct (**Parameters**) and sent to clients (ADR-0039).
_Avoid_: Config, settings (too generic — this specifically means simulation-tunable values, not startup settings, which the YAML files of ADR-0034 hold in place of command-line arguments)

**Parameters**:
The result of loading the data-driven configuration script (`parameters.lua`) out of the server pack: a plain immutable struct of the simulation's tunable values, the same for the whole run. Whatever tunable value client and server must agree on is a parameter: the server alone decides it and sends it to every client, which predicts with the server's numbers and never its own. The tick rate is not one: it is fixed for the life of the server process, so it is a startup setting in `augustad.yaml` that the server tells each client when it joins (ADR-0039).
_Avoid_: Tuning, settings; not a function's parameters

**Map**:
The static space a match is played in - its collision, spawn points and hitboxes - authored in OpenUSD (ADR-0015) and cooked into the signed pack. The shared `map` module builds its collision into the physics of both the client's PredictionWorld and the server's SimulationWorld.
_Avoid_: Level, stage (a *scene graph* is how the pack stores the map, not the map itself)

**Harness**:
Where anything that plays connects to the server: the client's network connection and PredictionWorld without a window or GPU. The real client, an automated test and a future autonomous agent each plug into one, supplying the input for every tick: whatever plays live runs it in real time on the Prediction and Network I/O threads the Harness owns (harness::Runner), and a test drives it by hand.
_Avoid_: Client session, bot

**Scripted player**:
What plays in a person's place through a Harness, deciding each tick's Command from the Server view, its own prediction and its seed: it wanders within a few metres of where the Match spawned it, aims from where its prediction puts it at the nearest living other player, fires in Bursts and reloads. augusta-loadtest runs a scenario's Player count of them against a server, for load and end-to-end tests.
_Avoid_: Bot (too vague: it names anything automated), AI player (it follows a fixed script, it does not plan)

### Combat

**Hitbox**:
A volume of a player's body that stands for one body part (head, torso or limb), used server-side to resolve where a bullet impacts. Authored with the Character, one or more per body part (ADR-0040).
_Avoid_: Collider (a collider is the general physics term; a hitbox is specifically the damage-resolution volume)

**Body part**:
Where on a player's body a bullet struck: head, torso or limb. Every Hitbox stands for one. It is what a hit resolves to, what decides the hit's damage (the Parameters give the ammo's damage for each), and what a Hit confirmation names.
_Avoid_: Hit zone, hit location, hit region, body zone

**Shot**:
One bullet fired, as the Authoritative server announces it to every client: who fired it, at which tick, from where and in which direction. Clients draw its trajectory; only the server decides what it hits (ADR-0044).
_Avoid_: Bullet (the bullet is what flies; the Shot is the firing that every client is told of), fire event, gunshot

**Burst**:
The rounds a rifle fires in one pull of its trigger: from the first round fired with fire held until fire is released, the magazine is empty or a reload starts. A round's place in it picks its kick from the Parameters' recoil pattern, and the next Burst starts the pattern over.
_Avoid_: Spray, volley, salvo (a Burst is not a fire mode: the rifle is full-auto)

**Recoil offset**:
How far a rifle points off its player's view, as a pitch and a yaw: every round of a Burst adds its kick to it, scaled down in ADS, and it shrinks back to zero while the trigger is not pulled. A Shot leaves along the view turned by it. It is the rifle's, predicted and reconciled with it, and never part of a Command's view.
_Avoid_: Spread (recoil is a fixed pattern, not random), kick (a kick is one round's addition to the offset), view punch, aim punch

**Shooter's delay**:
How far in the past the shooter saw other players when it fired: from the Seen time its fire Command reports to the tick the server takes that Command in, capped at 250 ms. Fixed when the Shot is fired, it holds for the bullet's whole flight (ADR-0044).
_Avoid_: Ping, lag, latency, rewind time (the RTT is only part of it, next to the Interpolation delay)

**Hitbox history**:
Every player's pose (position, stance and facing) on each of the Authoritative server's recent ticks, as that tick's Authoritative State reported it: enough ticks to cover the Shooter's delay's cap, 15 at 60 Hz. What Lag compensation places a player's Hitboxes by.
_Avoid_: Rewind buffer, position history, snapshot history

**Lag compensation**:
Judging a bullet against the hitboxes as they were the Shooter's delay ago, so a shot that hit on the shooter's screen hits on the server.
_Avoid_: Rewind, backward reconciliation, favor-the-shooter (Reconciliation is the client's own prediction correction, a different concept)

**Hit confirmation**:
The Authoritative server telling a shooter that its Shot hit a player: the target, the body part and the damage. The only source of the shooter's hit marker; a client never predicts a hit.
_Avoid_: Hit marker (the marker is how the client shows a Hit confirmation), hit prediction, hit registration

**ADS (Aim Down Sights)**:
The player action of aiming through a weapon's sights, trading movement/hip-fire speed for accuracy.
_Avoid_: Zoom, scope (those describe an effect of ADS, not the action itself)

**Spectator**:
A dead player still in the Match: until the Match ends it watches from a living player's eye, chosen in Session order and moved on with fire, and its input moves, turns and fires nothing. It never respawns.
_Avoid_: Ghost, observer, dead cam
