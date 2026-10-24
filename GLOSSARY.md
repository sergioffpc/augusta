# Augusta

A physics-driven, server-authoritative multiplayer FPS simulator engine.

## Language

### Simulation & Networking

**ECS (Entity Component System)**: The data-oriented architecture pattern the
simulation core is built on — entities are IDs, components are plain data,
systems operate on components. _Avoid_: Game object, actor

**Tick**: One discrete simulation step on the server, running at a fixed rate.
_Avoid_: Frame (a frame is a client-side render step, a separate concept from a
tick)

**Authoritative server**: The dedicated server, which holds the single source of
truth for all gameplay-affecting state. Nothing a client reports is treated as
fact until the server validates it. _Avoid_: Trusted server, master client

**Match**: One game played by the scenario's Player count, from Match start
until Game policy ends it. Its players are fixed when it starts: no one joins,
and a player who disconnects leaves it and its body is removed. When it ends,
everyone still connected returns to the Lobby (ADR-0043). _Avoid_: Game (too
broad: the product is a game), Round (a synonym; a Match is the one unit of
play, with no respawn until it ends)

**Lobby**: Where admitted players wait for the next Match, up to the Player
count. Players come and go freely; it closes when a Match starts and reopens
when it ends, and the next Match waits at least 5 seconds after that (ADR-0043).
_Avoid_: Waiting room, pre-game

**Player count**: How many players a Match needs to start, fixed by the
scenario's Parameters. _Avoid_: Max players (the protocol's upper bound on any
scenario's player count, not a scenario's own number)

**Ready**: A player whose client has loaded what it needs to draw everyone
currently in the Lobby. The client decides it on its own; the player never
presses anything. _Avoid_: Loaded, prepared

**Match start**: The moment the Lobby is full and every player is Ready, at
least 5 seconds after the previous Match ended: the server closes the Lobby and
tells every client who is in the Match, with each player's Character, the Entity
ID of the body it controls, and Spawn point. _Avoid_: Spawn (a player's body is
placed at Match start, but "spawn" names the placement, not the start of the
match)

**Match end**: The moment Game policy decides a Match is over (when at most one
Team has a player left alive: last Team standing), or its last player leaves.
The server takes every body and bullet out of the simulation and tells every
client still in the Match, with the Winner or a Draw; everyone returns to the
Lobby (ADR-0043). _Avoid_: Game over, round end

**Team**: One of the two sides every Match is played between, each named by its
scenario with its Uniform. Game policy puts each player on one at Match start
and it holds until Match end. _Avoid_: Side, squad, faction (a Team's name and
Uniform are its scenario's, not a fixed side)

**Uniform**: The Looks a Character wears for a Team, so a friend is told from an
enemy by what it wears, whichever Character either chose. _Avoid_: Skin, outfit,
team colour

**Winner**: The Team Game policy declares has won a Match, named in Match end.
Every player on it wins, alive or dead. _Avoid_: Victor, champion, winning
player

**Draw**: A Match end with no Winner: both Teams' last players died on the same
tick, a Match of one ended with its player's death, or its last player left.
_Avoid_: Tie, stalemate

**Session**: One player's presence on the server, from being admitted to the
Lobby until it disconnects, named by its Session ID. It spans the Lobby and
every Match the player plays in between. The client's own connection and
PredictionWorld pairing (augusta::harness::Session) is the client-side
implementation of one. _Avoid_: Connection (a session is the gameplay identity
kept for the life of the player's presence on the server; the transport
connection beneath it, augusta::networking's own peer handle, can in principle
outlive or be distinct from it)

**Link**: The channel through which one Session exchanges messages with the
other side: the game's state turned into messages and back, carried by the
transport connection beneath, which the Link alone owns. _Avoid_: Session (the
presence it carries), connection, netcode

**Session ID**: The Authoritative server's name for one connected player
(harness::SessionId on the client, host::SessionId and policy::SessionId on the
server, link::SessionId as the Link's own name for it, protocol::SessionIdWire
on the wire), assigned when it admits the join. Unique across every server and
every restart, so it names one Session wherever its lines end up. Distinct from
the transport's own handle for the connection, and not a credential — the server
tells senders apart by connection, not by this ID. _Avoid_: Player ID (a player
has no identity beyond its Session yet), connection ID

**Match ID**: The Authoritative server's name for one Match, assigned at its
Match start and unique across every server and every restart: what ties a
Match's lines, its Match capture and the Sessions that played it together.
_Avoid_: Match number, round ID

**Entity ID**: The Authoritative server's name for one dynamic body
(host::EntityId and simulation::EntityId on the server, harness::EntityId and
presentation::EntityId on the client, protocol::EntityIdWire on the wire): a
player's body today, later anything that moves. It names which body, not who
moves it: a player's body gets one at Match start, which pairs it with that
player's Session ID, and a body no player controls has one and no Session. Never
reused. _Avoid_: Player ID (a body is not a player), using a Session ID to name
a body

**Prop**: A dynamic body that affects gameplay (it blocks players and bullets,
can hurt a player, and is moved by players, bullets and explosions), simulated
only by the Authoritative server and named by an Entity ID. Placed in the Map or
created during a Match, like a thrown grenade (ADR-0045). _Avoid_: Object,
physics object, dynamic object, item

**Prop kind**: The authored definition a Prop is an instance of - its shape,
mass and Surface material - placed many times in a Map or created during a
Match. _Avoid_: Prefab, archetype, template, Prop (a Prop is one instance)

**Surface material**: What a surface is made of (concrete, wood, metal...),
named from the authored catalogue of them, which says what that surface does to
sound, to rounds that strike it, to bodies that touch it and how a strike on it
looks. One name for all of it, so concrete is concrete to every one of them.
_Avoid_: Physics material, ballistic material, surface type, material alone (a
surface's Look is unrelated)

**Look**: How a surface appears when drawn - its colour, how rough or metallic
it is, its bumps and its glow - authored apart from its Surface material:
neither decides the other, so a concrete-looking wall can stop rounds like wood.
_Avoid_: Material, render material, shader

**Cosmetic body**: A dynamic body that exists on one client only and never
affects gameplay, such as a ragdoll or debris. It is never sent and may differ
between clients (ADR-0045). _Avoid_: Prop (a Prop is gameplay), effect, particle
(particles are the renderer's, not physics)

**Breakable Prop kind**: A Prop kind authored already broken into pieces, so
that when one of its Props breaks during a Match it is replaced by those pieces:
the large ones Props, the small ones Cosmetic bodies. A wall that can be blown
open is a Breakable Prop placed in the Map; the Map itself never breaks.
_Avoid_: Destructible scenery, destructible Map, fracture

**Integrity**: How much more a Prop can take before it breaks or explodes, worn
down by the rounds that strike it, by Explosions and by hard knocks. Only Props
of a Breakable or explosive Prop kind have one. _Avoid_: Health (a player's),
hit points, durability

**Explosion**: A blast at one place and Tick, decided by the Authoritative
server, that throws and hurts what it reaches unsheltered by the Map, and that
every client sees and hears. Set off by a grenade, a launched round or an
explosive Prop. _Avoid_: Detonation, blast

**Fragment**: One of the pieces a fragmentation grenade's Explosion throws out,
flown like a round, which hurts the player it strikes in the Body part it
enters. _Avoid_: Shrapnel, frag (a Kill), splinter

**Dazzle**: How strongly a flashbang's Explosion blinds and deafens one player,
decided by the Authoritative server from where that player is and where it
looks, and felt as a white-out, muffled sound and ringing that fade. Never shown
on the HUD. _Avoid_: Blind, stun, flash

**Smoke screen**: A cloud from a smoke grenade that hides players, the same on
every client in where it is and how thick, growing and clearing over time.
Rounds pass through it. _Avoid_: Smoke alone (the smoke of an Explosion or an
impact is an Effect and may differ between clients), fog

**Loadout**: What one player carries in a Match - its weapons and how many
grenades and launched rounds of each kind - given by the Rules at Match start
and never refilled. _Avoid_: Inventory, kit, equipment

**Effect**: What a client draws for a moment of the game that affects nothing:
particles such as sparks, dust, fire or smoke, and optionally a decal and a
brief light, authored by name in one catalogue and drawn by each client on its
own, so it may differ between clients. _Avoid_: VFX, FX, particle system,
Cosmetic body (a Cosmetic body is physics)

**Client-side prediction**: The client simulating its own actions locally,
immediately, before the server confirms them — used purely for responsiveness.
_Avoid_: Client simulation

**Command**: One tick's local input (command::Command, in the shared core), with
the Seen time of the frame it was sampled on, a client sends the Authoritative
server under a growing sequence number, so the server can tell what it has
already seen and the client can tell what it has not yet acknowledged. What
Client-side prediction applies locally and Reconciliation replays. _Avoid_:
Input (Command is the sequenced payload sent to the server each tick;
augusta::input::Input is the per-frame local sampler that produces one)

**Play mode**: How one player plays a Match, Desktop (a screen, mouse and
keyboard or a gamepad) or VR (a PC VR headset with its tracked body), declared
when it joins and fixed for the Match. _Avoid_: Input mode, client mode, VR mode
(VR is one of the two Play modes)

**Tracked pose**: A VR player's body as its client reports it with each Command:
its Joints' rotations and its head's offset from its body, never its own limbs'
lengths, which are its Character's. _Avoid_: Mocap, body tracking data, skeleton
(a Skeleton is the Character's authored one)

**Reconciliation**: The process of correcting a client's predicted state against
the server's authoritative state: the client restores the server's state of its
body and its rifle and replays the commands the server has not yet acknowledged
from it, and presentation smooths the resulting jump. _Avoid_: Resync, rollback

**Authoritative State**: Everything the Authoritative server holds true as of
one of its ticks: every body's state, every player's rifle and health, and the
tick's combat events, such as its Shots and Deaths. No client is sent it whole:
each gets its own Authoritative State update of it. _Avoid_: Server state, world
state, snapshot

**Authoritative State update**: One server tick's Authoritative State as sent to
one client (protocol::AuthoritativeStateWire): every body as of that tick, by
its Entity ID, plus the recipient's own newest acknowledged Command sequence and
its own rifle. augusta::replication decides who gets what. _Avoid_: Snapshot,
state sync

**Spawn point**: A place in the Map where a player's feet are put at Match
start, authored as a scene node in the pack (ADR-0032). Which player gets which
is Game policy: a hook of the scenario's Rules assigns them once, at Match start
(ADR-0022, ADR-0023), and players take them in order, starting over after the
last, when it is undefined or its answer is refused. Every client is told where
each player spawned in Match start. _Avoid_: Spawn location, start position

**Roster**: Who is in the Lobby, each with its Character, as the server tells
every client in it whenever it changes. The version a client loaded for is what
its Ready names. _Avoid_: Player list

**Character**: What a player plays as, meaning its body's look and its collider,
chosen from the characters the scenario's manifest names. The player picks one
before joining, the server admits the join only if the scenario has it, and it
stays fixed for the whole Session, across every Match in it (ADR-0042). _Avoid_:
Skin, model, avatar (a character is not only appearance: its collider is
gameplay)

**Interpolation delay**: How far in the past, about 100 ms, a client shows other
players, so it always has two Authoritative State updates to interpolate
between. _Avoid_: Lerp delay, buffer time, lag

**Match capture**: What the players of one Match did, from its Match start to
its Match end, as the Authoritative server took it in: who joined it and where
they spawned, each Command a client sent and who left it, each at its tick from
the Match's first, with its Deaths and its Match end to compare against
(ADR-0050). Written per Match when the server's config asks for it, for a Replay
or a Reenactment. _Avoid_: Match recording, demo, playback

**Reenactment**: Captured players playing a Match capture again against a live
server, through the network, one client each: the playtest's input at the
playtest's pace, to bring back a crash or load problem; what the server resolves
from it may differ from the Match captured (ADR-0050). _Avoid_: Replay (the
server re-runs a Replay itself), rerun

**Replay**: A replay server handing a fresh SimulationWorld a Match capture,
tick by tick, and streaming what it resolves to a Replay viewer, who watches the
Match from any player's eyes; each Death and the Match end are checked against
the capture's (ADR-0051). _Avoid_: Playback, demo, rewind (Lag compensation
looks back in time), re-simulation (Reconciliation's replay of unacknowledged
commands is the client's own), Reenactment (that goes through live clients)

**Replay viewer**: A client watching a Replay: a Spectator from the first tick,
with no body and no Commands (ADR-0051). _Avoid_: Observer, ghost

**Seen time**: The moment of the server's timeline a client's frame shows the
other players at: the tick of an Authoritative State update and how far from it
to the next one, 0 to 1 (presentation::SeenTime). Every Command reports the Seen
time of the frame it was sampled on, and the server judges its rounds against
the other players as they were then (ADR-0044). _Avoid_: View, shown view, view
tick (a view is where a player looks: a yaw and a pitch)

**RTT (Round-Trip Time)**: The measured network latency between a client and the
server for a single request/response cycle. _Avoid_: Ping, lag

**Connection health**: How well a client's transport connection is delivering,
as the server's own transport measures it: its RTT, packet loss each way,
jitter, throughput and send queue. A property of the transport connection, not
of the Session above it: what happens to a Session (admitted, refused, left
mid-Match) is not part of it. _Avoid_: Connection quality (the transport's own
0..1 delivery rate, one input of it), network health, lag

### Architecture

**Mechanism**: Engine-side C++ code that provides a capability without deciding
when or how it's invoked for gameplay purposes (e.g. the
ballistics/hit-detection pipeline, the ECS phase runner). _Avoid_: Engine code,
core logic

**Game policy**: Gameplay-specific rules (Match lifecycle, win conditions, spawn
rules) that decide how mechanism is used, implemented as sandboxed Lua in
SimulationWorld's Scripts/Behaviours phase, kept out of C++ so it can change
without touching mechanism code. _Avoid_: Game logic, gameplay code (too broad —
conflates policy with mechanism)

**Glue**: What an executable adds to the modules it is built from, and the only
thing it adds: its entry point, which runs one Runtime behind the application
boundary, and uses only the application, that Runtime, crash, failure, logging
and version. It holds neither Mechanism nor Game policy; anything worth a test
of its own is not Glue and belongs to the Runtime or to a module. _Avoid_:
Application logic, server logic, client logic

**Runtime**: What runs a Harness or a Host live for one executable: the threads
and clock around it, the executable's own configuration and the wiring of the
modules it drives into one another. _Avoid_: Main loop, app

**Rules**: A scenario's Game policy as one Lua script: every hook of its game
mode, such as who spawns where and when the Match is won, chosen together
because they describe the same mode. The scenario's manifest names the script,
and it is packed with the scenario (ADR-0022, ADR-0041). A scenario may have
none. _Avoid_: Behaviours, objectives (the two scripts the rules replaced), game
mode script

**Policy action**: One decision of Game policy as the server receives it: a
hook's answer read into one of a closed set of C++ types and validated as it is
read (ADR-0022), such as a Match end. The server acts on policy actions; it
never reads a hook's answer itself. _Avoid_: Policy result, script output

**Tick result**: What one SimulationWorld tick returns (simulation::TickResult):
the tick's Authoritative State, its combat events included, and the Policy
actions taken on it (ADR-0023).

**Server view**: Everything the server has told one client, as of one moment
(harness::ServerView): published whole by the client's Network I/O thread for
every message that changes it and never changed after, so a reader on another
thread sees one moment (ADR-0005). It holds the newest Authoritative State
update and the recent ones before it. _Avoid_: Snapshot (an Authoritative State
update is not one), network state

**SimulationWorld**: The Authoritative server's one world: every player and
bullet in the Match, simulated once per tick, with Game policy run inside it.
What it resolves each tick is the Authoritative State. _Avoid_: Server world,
game world

**PredictionWorld**: The client's world for its own player only, simulated once
per tick against the Map's collision: where Client-side prediction runs and
Reconciliation corrects it. What it predicts each tick is the Prediction State.
_Avoid_: Client world, local simulation

**Prediction State**: What PredictionWorld predicted on one tick, handed whole
to PresentationWorld and never changed after (ADR-0005). _Avoid_: Predicted
snapshot, client state

**PresentationWorld**: The client's world run once per render frame: it turns
the Prediction State and every Authoritative State update since the last frame
into what the frame shows, showing the other players an Interpolation delay
behind and smoothing Reconciliation's jumps. _Avoid_: Render world, scene

**Data-driven configuration**: Tunable values (e.g. weapon/ammo damage, stamina
rules) written as a Lua table script rather than expressed as mechanism code or
policy scripts — a third category alongside mechanism and policy. A value may be
an expression of other values; the script is named by the scenario's manifest,
cooked into the scenario's server pack, evaluated once at server startup into a
plain immutable struct (**Parameters**) and sent to clients (ADR-0039). _Avoid_:
Config, settings (too generic — this specifically means simulation-tunable
values, not startup settings, which the YAML files of ADR-0034 hold in place of
command-line arguments)

**Parameters**: The result of loading the data-driven configuration script out
of the server pack: a plain immutable struct of the simulation's tunable values,
the same for the whole run. Whatever tunable value client and server must agree
on is a parameter: the server alone decides it and sends it to every client,
which predicts with the server's numbers and never its own. The tick rate is not
one: it is fixed for the life of the server process, so it is a startup setting
of the server that the server tells each client when it joins (ADR-0039).
_Avoid_: Tuning, settings; not a function's parameters

**Scenario**: One playable composition of authored content - a Map, the
Characters it offers, its sounds, its Parameters and its Rules - named by one
manifest and cooked into its own Packs (ADR-0041). An Authoritative server runs
one scenario. _Avoid_: Level, game mode (the Rules are its mode), map (a Map is
one part of it)

**Pack**: One scenario's cooked and signed content as a runtime loads it, cooked
as a pair: the client pack, what a client needs to draw and hear it, and the
server pack, what the server needs to simulate it, which names the client pack
cooked with it. Nothing in a pack is trusted until its signature verifies
(ADR-0030, ADR-0031). _Avoid_: Asset bundle, archive, package

**Map**: The static space a match is played in - its collision, spawn points,
hitboxes and Acoustic geometry - authored in OpenUSD (ADR-0015) and cooked into
the signed pack. Its collision is built into the physics of both the client's
PredictionWorld and the server's SimulationWorld. _Avoid_: Level, stage (a
_scene graph_ is how the pack stores the map, not the map itself)

**Harness**: Where anything that plays connects to the server: the client's
network connection and PredictionWorld without a window or GPU. The real client,
an automated test and an Agent each plug into one, supplying the input for every
tick: whatever plays live runs it in real time on the Prediction and Network I/O
threads the Harness owns (harness::Runner), and a test drives it by hand.
_Avoid_: Client session, bot

**Host**: Where every client connects on the server: its socket and
SimulationWorld without threads or a clock, the server's counterpart of the
Harness. The server's Runtime runs it in real time on its Network I/O and
Simulation threads, and a test drives it by hand, one network round and one tick
at a time. _Avoid_: Game server, server session

**Agent**: A player with no one at the keyboard, controlled by a Python script
through a Harness (ADR-0052): the script observes what the server tells the
Agent and its events, and chooses Intents; the Harness turns them into a Command
every tick. A load test and an AI playing the game are the same thing with
different scripts. _Avoid_: Bot (too vague: it names anything automated)

**Intent**: What an Agent is doing on one of its channels - movement, aim or
trigger - until its script replaces it, such as moving to a point, aiming at a
body or firing in Bursts. The Harness carries it out every tick, so aim follows
a moving target between the script's decisions; a Raw Intent holds one Command
on all three channels. An Intent that ends - arrived, blocked, target gone,
replaced - tells the script. _Avoid_: Command (what is sent to the server each
tick), Policy action (a decision of Game policy), order

**Captured player**: A client playing one player of a Match capture in a
Reenactment: it joins with a Reenact request naming that player's Character and
spawn, sends that player's Commands at their ticks, leaves when that player did
and shows the Match from that player's view (ADR-0050). _Avoid_: Replay viewer
(it watches, it does not play), bot, ghost

### Debugging

**Run**: One execution of a client, from its launch until its process ends. It
holds at most one Session, since a client never reconnects, and may end before
it is admitted. _Avoid_: Session (the presence on the server), launch

**Run ID**: The client's own name for one Run, taken at launch and unique across
every machine: it marks everything the Run leaves behind, and the server learns
it on the join, which ties it to the Session ID it admits. _Avoid_: Session ID,
process ID

**Crash dump**: What a crash leaves to be read in a debugger beside the build's
symbols: a client's minidump or core, or a GPU crash's Aftermath dump. _Avoid_:
Crash report (a crash leaves log lines too), core (one kind of it)

**Debug HUD**: Lines of text over the frame's corner showing the client's system
metrics. _Avoid_: Overlay, stats panel

**Debug overlay**: Shapes drawn into the 3D world to show state the game itself
does not show, such as collision shapes, Hitboxes or a Smoke screen's voxels.
_Avoid_: Debug view (it replaces the frame), gizmo, wireframe

**Debug view**: The frame replaced by one of the images it is made from, such as
albedo, normals or shadows. _Avoid_: Debug overlay (drawn over the world),
render mode

**Debug panel**: A window over the frame with numbers and controls about one
part of the game, such as the network or the physics. _Avoid_: Debug menu,
inspector, debug HUD (text lines only)

**Debug index**: The Debug panel listing the debug tools a client carries, each
switched on or off from it. _Avoid_: Debug menu, console

**Debug interaction**: The state in which the Debug panels take the mouse and
keyboard and the game gets neither. _Avoid_: Debug mode, edit mode

**Development build**: A build of the game with its debug tools in, whatever its
optimisation: what developers run, and what the `develop` Environment serves.
_Avoid_: Debug build (one kind of Development build, unoptimised)

**Shipping build**: The build of the game players get, with every debug tool
left out of it, not merely switched off: what a release publishes and what the
`staging` Environment serves. _Avoid_: Release build (Release is also how a
Development build is optimised), ship build, retail build

### Combat

**Surface**: Anything a round can strike: a collider of the Map, a Prop, or one
of a player's Hitboxes. Ballistics knows a Surface only by the id its caller
gave it. _Avoid_: Target; collider, when the Surface is a Hitbox

**Hitbox**: A volume of a player's body that stands for one body part (head,
torso or limb), used server-side to resolve where a bullet impacts. Authored
with the Character, one or more per body part (ADR-0040). _Avoid_: Collider (a
collider is the general physics term; a hitbox is specifically the
damage-resolution volume)

**Body part**: Where on a player's body a bullet struck: head, torso or limb.
Every Hitbox stands for one. It is what a hit resolves to, what decides the
hit's damage (the Parameters give the ammo's damage for each), and what a Hit
confirmation names. _Avoid_: Hit zone, hit location, hit region, body zone

**Hitbox pose**: The part of a player's skeletal pose that moves its Hitboxes:
locomotion, stance, aim and how the weapon is held. Client and server arrive at
the same one from the same state, so a shooter hits where it sees a body; what
the client draws on top of it (Cosmetic layers) never moves a Hitbox. _Avoid_:
Server pose, physics pose, hit pose

**Skeleton**: The jointed frame of a Character, authored with it and the same
for every player who plays that Character: what a Hitbox hangs from and what a
Hitbox pose moves. Each Character has its own; a Clip plays on several only when
their Skeletons share its Joints. _Avoid_: Rig (a rig is the authoring apparatus
around a skeleton, including what never ships), armature, bones (a Joint is the
unit, and "bone" names the gap between two)

**Joint**: One articulation of a Skeleton, named by its place in it, carrying
the Hitboxes authored under it. Every Hitbox belongs to exactly one; a Joint may
carry none. _Avoid_: Bone, node, transform (a Joint has a transform; it is not
one)

**Rest pose**: The Hitbox pose a Character is in when nothing animates it — the
pose it was authored in. What the engine poses a player by until a Character has
Clips, and what a Skeleton's Joint transforms describe as authored. _Avoid_:
Bind pose (that names the pose a mesh was skinned in, a client concern), T-pose,
default pose, idle (an idle is a Clip, not the absence of one)

**Cosmetic layer**: Something the client draws on top of a player's Hitbox pose
without moving a Hitbox, such as foot placement, a flinch or a reload. _Avoid_:
Client-side animation, visual pose

**Clip**: An authored animation of a Skeleton, used by a Character for one role,
such as walking forward crouched. _Avoid_: Animation (the module), sequence,
take

**Stride**: Where a player is in its cycle of two steps, advanced by the
distance its body moves and never by time. _Avoid_: Gait phase, cycle time

**Aim offset**: The poses that bend a player's body by the pitch of its view.
_Avoid_: Aim layer, look pose

**Shot**: One bullet fired, as the Authoritative server announces it to every
client: who fired it, at which tick, from where and in which direction. Clients
draw its trajectory; only the server decides what it hits (ADR-0044). _Avoid_:
Bullet (the bullet is what flies; the Shot is the firing that every client is
told of), fire event, gunshot

**Burst**: The rounds a rifle fires in one pull of its trigger: from the first
round fired with fire held until fire is released, the magazine is empty or a
reload starts. A round's place in it picks its kick from the Parameters' recoil
pattern, and the next Burst starts the pattern over. _Avoid_: Spray, volley,
salvo (a Burst is not a fire mode: the rifle is full-auto)

**Muzzle**: Where a rifle's rounds leave from, and the way its barrel points, as
its player holds it. _Avoid_: Eye (the Eye is where a player sees from, never
where a round leaves), shot origin

**Recoil offset**: How far a rifle points off its player's view, as a pitch and
a yaw: every round of a Burst adds its kick to it, scaled down in ADS, and it
shrinks back to zero while the trigger is not pulled. A Shot leaves along the
view turned by it. It is the rifle's, predicted and reconciled with it, and
never part of a Command's view. _Avoid_: Spread (recoil is a fixed pattern, not
random), kick (a kick is one round's addition to the offset), view punch, aim
punch

**Shooter's delay**: How far in the past the shooter saw other players when it
fired: from the Seen time its fire Command reports to the tick the server takes
that Command in, capped at 300 ms. Fixed when the Shot is fired, it holds for
the bullet's whole flight (ADR-0044). _Avoid_: Ping, lag, latency, rewind time
(the RTT is only part of it, next to the Interpolation delay)

**Hitbox history**: Every player's posed Joints on each of the Authoritative
server's recent ticks, as that tick's Authoritative State posed them: where its
root was (position, stance and facing), and apart from it each Joint that
carries a Hitbox or is the ancestor of one, relative to its parent. Enough ticks
to cover the Shooter's delay's cap, 18 at 60 Hz. What Lag compensation places a
player's Hitboxes by. _Avoid_: Rewind buffer, position history, snapshot history

**Lag compensation**: Judging a bullet against the hitboxes as they were the
Shooter's delay ago, so a shot that hit on the shooter's screen hits on the
server. _Avoid_: Rewind, backward reconciliation, favor-the-shooter
(Reconciliation is the client's own prediction correction, a different concept)

**Hit confirmation**: The Authoritative server telling a shooter that its Shot
hit a player: the target, the body part and the damage. The only source of the
shooter's hit marker; a client never predicts a hit. _Avoid_: Hit marker (the
marker is how the client shows a Hit confirmation), hit prediction, hit
registration

**Shot judgement**: How the Authoritative server judged one of a shooter's
rounds, told to that shooter in a Development build: the Hitboxes each tick of
its flight was tested against, as they were the Shooter's delay ago, and what
the round struck. _Avoid_: Impacts, hit report, Hit confirmation (only a hit,
and in every build)

**Death**: A player's health reaching zero in a Match, as the Authoritative
server resolves it: who died, who killed it and on which tick. The player stays
in the Match as a Spectator, and Game policy decides from the Deaths when the
Match ends. _Avoid_: Kill (the same event told from the killer's side), frag,
elimination

**ADS (Aim Down Sights)**: The player action of aiming through a weapon's
sights, trading movement/hip-fire speed for accuracy. _Avoid_: Zoom, scope
(those describe an effect of ADS, not the action itself)

**Spectator**: A dead player still in the Match: until the Match ends it watches
from a living teammate's eye (any living player's once its Team has none),
looking where that player looks and zoomed by its ADS, chosen in Session order
and moved on with fire, and its input moves, turns and fires nothing. It never
respawns. _Avoid_: Ghost, observer, dead cam

### Audio

**Acoustic geometry**: The surfaces sound bounces off, is blocked by and passes
through: shapes authored for that alone in a Map or a Prop, never drawn and
never collided with, each with one Surface material. _Avoid_: Audio mesh, sound
collision (the Map's collision is not what sound sees)

**Acoustic material**: What a Surface material does to sound - how much it
absorbs, scatters and lets through. Not named on its own: a surface names its
Surface material. _Avoid_: Audio material, surface type (a render material is
unrelated)

**Probe volume**: A region of a Map where sound is heard, filled with points
whose reflections and paths are baked ahead of play. _Avoid_: Reverb zone, audio
volume

**Foot contact**: A moment authored in a locomotion Clip when one foot lands,
heard as a footstep whenever a player's Stride passes it. _Avoid_: Footstep
event, foot plant (foot placement is a cosmetic layer, ADR-0061)

**Voice chat**: What the players of a team match say to each other through their
microphones: by radio to their whole Team wherever they are, or aloud to anyone
near enough to hear, enemies included. _Avoid_: Voice alone (a sound the mixer
plays), VoIP, comms

**Speech**: One player's Voice chat from pressing a push-to-talk to releasing
it. _Avoid_: Voice packet, transmission

### Deployment

**Node**: The physical machine the cluster runs on, and every Environment's
Authoritative servers with it: what is hardened, patched and rebuilt as one.
_Avoid_: Server (the Authoritative server is a workload on the Node, not the
Node), host, k3s server, box

**Environment**: One long-lived deployment that follows signed sources and runs
an Authoritative server per scenario it serves: `develop` (the chart CI signed
for the latest `develop` commit) and `staging` (the latest signed `v*` release
tag), both on the Node and kept apart from each other (ADR-0026, ADR-0056).
_Avoid_: Stage, namespace (the namespace is how an Environment is kept apart,
not the Environment), preview
