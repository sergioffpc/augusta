# Match Recording and Replay: SimulationWorld's Input and Outcome per Tick, in the Protocol's Encoding

A bug seen in a playtest is hard to reproduce by playing again, and a
non-determinism in SimulationWorld's phase pipeline (ADR-0023) shows only as a
match that cannot be played the same way twice. This ADR decides what the server
records of a match, where and when, how a recording is replayed, and what makes
a replay's outcome "the same".

**A recording is SimulationWorld's input and outcome, tick by tick.** Not the
network traffic: what reaches SimulationWorld is what the server decided to hand
it after its queues, gates and Match lifecycle, and replaying the World alone
needs neither a socket nor a clock. A recording opens with a header naming the
server pack its content came from (its BLAKE3 hash), the engine version that
recorded it and the tick rate. Then one record per tick, every tick the World
ran from its first, in order, so a record's tick is its place:

- its input, in the order the server hands it over: whether the Match in the
  World was ended before the tick, the bodies taken out of it, the players of a
  Match started before it (body, session and Character by path), every player's
  command for the tick, and the tick's duration;
- its outcome, as far as its players are told: where each player of a Match
  start spawned, every body as of the tick with its rifle and health, the Shots
  fired, the hits (shooter, target, Body part, damage, health left), the Deaths,
  and Game policy's Match end with its winner or a Draw.

The Map's impacts and the bullets still in flight are left out: no client is
told them, and a bullet that flies differently shows in the hits it makes.

**The protocol's encoding, as records of their own.** A record is one payload
the way a message is (ADR-0007, ADR-0038): a one-byte type (header or tick) and
its fields, with the same grids, the same flag packing, the same widest-first
structs and the same untrusted-input decoding, and a command in the very bytes a
Commands message carries it in, its Seen time's tick written in full beside it.
They are `augusta_protocol`'s, next to the messages, but never messages: neither
decoder yields the other's. A file is the records in order, each after its
length in 4 little-endian bytes, since the protocol's own payloads leave framing
to the transport. No record is longer than 64 KiB, many times what a tick of 8
players makes: a reader refuses a longer length before allocating for it, and a
writer that would write one stops recording there instead, so a file is always
readable up to its last record. Every number a record holds is on its grid or
travels as its bits, so a tick's outcome reads back exactly what the World
resolved; only a Spawn point, which need not be on the grid, is recorded where
the body placed there is.

**augustad records only when asked.** `simulation.recording` in `augustad.yaml`
(ADR-0034) names the file; without it nothing is recorded. The file is replaced
when the server starts and holds the whole run, Lobby ticks included, since the
World's tick count and every Seen time depend on them. Each tick's record is
written and flushed as soon as the recording's writer reaches it, so a server
that stops abruptly leaves behind every whole tick written by then; a record cut
short is dropped when read, and reported. It costs about 35 KB a second with 8
players at 60 Hz, which is why it is a debugging setting and not a default.

**The disk is written on a thread of the recording's own.** The Simulation
thread encodes a tick's record right after the tick, which costs only the
record, at most 64 KiB, and hands it to the recording's writer through a queue
of at most 256 records (about 4 seconds at 60 Hz, 16 MiB at worst), without
waiting for the disk; the writer writes and flushes each record in turn. A disk
that stalls a write then holds up only the writer, never a tick (NFR-01). A
record that finds the queue full means the disk is not keeping up: the recording
stops there, logged once, and the file keeps every tick before it, as it does
for a record too long to write, because dropping a tick and going on would leave
ticks that are no longer their places. When the server stops, the writer writes
what is still queued before it goes, so a stalled disk holds up the server's
shutdown, not its ticks.

**A replay hands a fresh World the same and checks each tick.** The replay loads
the content of the pack the header names (refusing another pack), builds the
World as the server does, and for every record hands it the recorded input in
the recorded order, then compares the tick's outcome, its number included, with
the recorded one. It stops at the first tick that differs and reports which part
of the outcome did. A pack whose Map the World refuses stops it before the first
tick, as it would have stopped the server. The `augusta-replay` tool does this
for a file augustad wrote; a test does it for a recording it made. The replay is
a tool, not runtime: it lives in `tools/replay` with its tests, the golden match
among them, and `augustad` only writes recordings.

**"The same" is exactly equal on the build that recorded it, a grid step across
builds.** On the build and platform that made a recording, every value of every
tick must be equal, as numbers compare (every value is a count of a grid's step
or a float of simple arithmetic, and none is a NaN): the World keeps bodies on
the protocol's grids, simulates no rigid bodies yet (ADR-0045) and runs Game
policy with no source of randomness, so it has no excuse to differ, and a
tolerance there would hide the non-determinism a replay exists to catch. Across
builds it cannot be held to that: PhysX is not deterministic across Windows and
Linux (ADR-0045), nor are two compilers' floats (ADR-0008), and a body that
rounds onto the grid one way on one build can round the other way on another. So
a replay across builds lets a body's position and a Shot's origin be off by one
step of the position grid (about 1 mm, the reconciliation tolerance of ADR-0004
and NFR-03's), and a body's velocity, derived from two positions a tick apart,
by what two such steps make over the tick; every other value must still be
equal. Each tick of such a replay then starts from the recorded bodies, not the
replayed ones: the World puts every body where the recording has it (position,
velocity, stance, stamina, keeping its fall), so a step one build rounds
differently is never carried into the next tick, where movement and collisions
could grow it, and every tick is judged against the same start the recording
had. Only the Hitbox history keeps the replayed poses, a step at most off each,
which moves a hitbox by a millimetre for the Shooter's delay and does not grow.
The tool asks for the strict comparison unless told the recording comes from
another build.

**A recorded match is a golden test.** The repository holds one recording, a
scripted duel on the example scenario's golden server pack (ADR-0013), and the
test suite replays it across builds on every runner, as it does the golden
ballistic trajectories (NFR-03). A deliberate change to the simulation rewrites
it with one build target, and the diff is reviewed like code. Any other
recording, from a playtest, becomes a test the same way.

## Consequences

- **A recording is valid only with its pack.** The header names the pack's hash,
  not its content: a replay needs the very pack, and a recording made before a
  scenario change cannot be replayed after it.
- **A change to SimulationWorld's input or outcome changes the records.** A new
  phase input (Props' impulses, ADR-0045) or a new kind of outcome joins the
  tick's record; old recordings then fail to decode or replay, and the golden
  match is rewritten.
- **Props will strain the comparison across builds.** Once the Dynamics phase
  simulates them (ADR-0045), a Prop can drift more than a grid step between
  builds; their place in the outcome and their tolerance are decided when they
  land.

## Considered Options

- **Recording the network traffic**: rejected - a replay would need the server's
  queues, gates and Match lifecycle in the loop with their clock, to reach the
  same World input the recording can hold directly.
- **A digest of each tick's outcome instead of the outcome**: rejected - it can
  only be compared exactly, so a recording would replay on no build but its own,
  and a divergence would say nothing about what differs.
- **A tolerance on every float**: rejected - outside positions, every value the
  outcome holds is discrete, on a grid fed by commands, or simple arithmetic
  IEEE-754 does the same everywhere; loosening them would hide real
  non-determinism.
- **A schema library for the file (Protobuf, FlatBuffers)**: rejected, as for
  the messages (ADR-0007): the records are few and the protocol's codec already
  encodes every value they hold.
- **Comparing across builds without re-syncing**: rejected - a step off on one
  tick moves where the next tick starts, and over a match the difference grows
  past any fixed tolerance, so the golden match would fail on the other platform
  as soon as its bodies moved.
- **Writing each record on the Simulation thread**: rejected - a write is
  usually microseconds into the operating system's file cache, but nothing
  bounds it: a disk that stalls holds up the tick, and the debugging session
  that turned recording on then debugs a server missing its deadlines.
- **Blocking the Simulation thread when the queue is full**: rejected - it
  brings the stall back, only later.
- **Recording by default**: rejected - it costs disk on every run for a file
  only a debugging session reads.
