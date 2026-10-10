# Match Capture and Reenactment: One Match's Client Actions, Played Again by augustac Against a Live Server

> Supersedes ADR-0048: the Match recording and its Replay go, and a Match
> capture is the one way the server keeps a Match. ADR-0051 replays a capture on
> the server for viewers; this ADR reenacts one through clients.

A crash or a load problem seen in a playtest is hard to bring back: the people
who played are gone, and nothing else plays the way they did. This ADR decides
what the server captures of a Match, how `augustac` plays one player of it again
against a real server (a Reenactment), how a capture is inspected, and what a
Reenactment can and cannot promise.

**A Match capture is one Match, from its Match start to its Match end.** Not the
server's run: the Lobby, the joins and Readies before the Match, and everything
between two Matches are left out, since a Reenactment brings its own Lobby
(below) and a Replay needs none (ADR-0051). A capture opens with a header: a
magic and the format's version, the engine version that made it, the server pack
the Match ran on and the client pack its players joined with (their BLAKE3
hashes), the tick rate and when the Match started, in UTC. Then one ordered
stream of events, each at its offset in ticks from the Match's first tick, so
the capture names no tick of the server that made it:

- **Join**, at offset 0, one per player in the Match start's order: the player's
  Session, its Character, by its name in the scenario's manifest (ADR-0042), and
  where the Match start spawned it, on the position grid. A player's place in
  this order is its number in the capture, from 1.
- **Commands**: each Command a player sent, at the offset of the tick the
  server's command queue handed it to SimulationWorld, its Seen time's tick
  turned into an offset the same way. Only what the client sent: the movement
  the queue holds or idles on a tick with nothing new (`server::CommandQueue`)
  is the server's own and is not captured, and a Command a Commands message
  repeats is captured once.
- **Leave**: a player whose connection ended mid-Match, at the offset of the
  tick the server took its body out.
- **Death**: who died, who killed them, and the offset of the tick.
- **Match end**: the last event, at the offset of the Match's last tick, with
  its winner or a Draw.

**Join, Commands and Leave are actions; Death and Match end are markers.** A
Captured player can do what a client does: join, send Commands, disconnect. It
cannot make anyone die or end the Match: the server decides both (ADR-0044,
ADR-0043), from Commands that, sent open-loop, need not hit what they hit in the
playtest. A dead player stays in the Match as a Spectator, so a Death is never a
Leave. The markers say how a run compares with the playtest; they do not drive
it.

**In the protocol's encoding, one record per event.** A record is one payload
the way a message is (ADR-0007, ADR-0038): a one-byte type and its fields, a
Command in the very bytes a Commands message carries it in. They are
`augusta_protocol`'s, next to the messages, but never messages: neither decoder
yields the other's. A file is the header, then the records in order, each after
its length in one byte, so none is longer than 255 bytes: the longest, the
header with its engine version and a Join with its Character's name, are about
110 and 85, and a Command about 20, so a wider length would only add zeros.

**The disk is written on a thread of the capture's own.** The Simulation thread
encodes each record as the tick produces it and hands it to the capture's writer
through a bounded queue, without waiting for the disk; the writer writes and
flushes each in turn, so a disk that stalls holds up only the writer, never a
tick (NFR-01). A record too long, one that finds the queue full, or a write that
fails stops the capture there, logged once, and the file keeps every record
before it. A server that stops abruptly leaves every whole record written by
then; a record cut short is dropped when read, and reported.

**Optional or strict (#398).** `simulation.capture_mode` says what losing a
record costs the run, ADR-0033's dispositions. A record too long, one that finds
the queue full or one past the retention budget is a loss as much as a failed
create, write or flush, since the capture misses it all the same. `optional`,
the default, is the debugging aid above: the run's first loss is logged once at
`ERR` as `event=capture_degraded`, a failure of the `subsystem` named for what
went wrong (`capture_write_failed` for a create or write,
`capture_flush_failed`, `capture_queue_full` for a disk that fell behind,
`capture_record_too_long`, `capture_retention_budget`), the captures are
`degraded` from then on, and the next Match is still captured afresh; the
Match's authority never changes. A playtest whose point is the capture runs
`strict`: any loss is `strict_capture_failed`, a `runtime` failure, nothing more
is captured, and the Simulation thread stops the runtime on it before its next
tick, the supervisor writing the one `ERR` line. A capture that loses nothing is
written the same in either mode, and neither mode makes a tick wait for the
disk: a stalled disk fails a strict run only once its queue is full.

**The captures' health is one-way: `enabled`, then `degraded`, then `stopped`.**
It is the `augustad_capture_health` metric (ADR-0049), and each change of it a
log line (`capture_enabled ... mode=`, `capture_degraded`,
`capture_disabled ... lost=`). `stopped` means the run's captures produce no
more records: a strict capture lost one, or the Simulation thread has run its
last tick, however its loop ended, and waited for the writer to write what was
queued. Both happen before the runtime returns, while its metrics endpoint still
serves, so a scrape between then and the process's exit reads `stopped`; a
degraded run's loss stays in its `capture_disabled` line as `lost=true`.

**augustad captures only when asked.** `simulation.capture` in `augustad.yaml`
(ADR-0034) names a directory; without it nothing is captured. Each Match is a
file of its own, named by when it started and its number in the server's run, so
a server that runs many Matches keeps every one.

**A server keeps its capture directory within a count and a size, oldest Match
first (#461).** `simulation.capture_retention` sets `max_files`, `max_mib` or
both; without it nothing is deleted, and `0` or a negative value fails the
config. Only the directory's own regular files named as captures and starting
with the magic count, oldest by the Match start in the name, never the mtime;
the Match in progress is never one. At each Match start, before its file is
created, the oldest are deleted until fewer than `max_files` remain. `max_mib`
is a hard cap, the Match in progress included: before each record the writer
deletes the oldest completed capture while the directory's total plus that
record would pass it, and stops the capture with `retention_budget` when none is
left, the file keeping every record before it. All of it runs on the writer
thread, from one scan per Match start plus the bytes it writes and frees, never
a rescan per record. A deletion that fails is logged once per Match start and
stops nothing: a filesystem sized to the cap is the backstop (#460). Each
server's budget is its own; the servers sharing a filesystem must sum to fit it.

**Match start names the Match's first tick.** A Captured player must turn the
capture's offsets back into ticks of the server it plays against, and
`MatchStartWire` gains the Match's first tick for that. It travels reliably, as
Match start does; taking it from the first Authoritative State instead would put
every offset off by however many States were lost before the first arrived.

**augustac reenacts one player of a capture:
`--reenact <capture> --player <n>`.** The command line otherwise only asks for
`--config`, `--help` and `--version` (ADR-0034); a capture is chosen for one
run, not kept as a setting, so it joins them. The rest comes from
`augustac.yaml` as on any run: the server to connect to and the client pack. The
client becomes the Captured player numbered `n`:

1. It refuses to start if the capture does not load, has no player `n`, or names
   another client pack than the one it loaded. It asks to join with a Reenact
   request (below) instead of a Join request, with player `n`'s Character, not
   the config's, and its spawn, and logs it if the server's Player count is not
   the capture's number of players, then plays on.
2. It reports Ready as any client does, once it has loaded the Lobby.
3. From Match start it sends player `n`'s Commands paced to the server's ticks,
   so the queue hands each to the World on the Match's first tick plus its
   offset, its Seen time's tick moved by the same amount: the Seen time keeps
   the delay it had, and Lag compensation judges its rounds as far back as in
   the playtest. The keyboard and mouse move nothing; the view is the captured
   player's, so whoever runs it sees the Match as that player played it. It
   disconnects at its Leave's offset.
4. The run ends at the capture's Match end or the server's, whichever comes
   first. It logs both, and each Death of the capture beside the one the server
   told, if any.

**A whole Reenactment is one augustac per player, all joining at once.** Each
Captured player is independent of the others: it knows only its own player of
the capture, talks only to the server, and neither waits for nor checks on
another. Nothing orders them either: the Match starts only when the Lobby holds
the Player count and every one of them is Ready (ADR-0043), so a run starts the
processes together and the server waits for the last. The order they are
admitted in is whatever it happens to be, and no spawn depends on it: each
spawns where its Reenact request says.

**A Captured player joins with a Reenact request, which names its spawn.** Every
Command after Match start moves a body from where it stands, so a player that
spawns anywhere but where it did in the playtest makes every one of its Commands
walk somewhere else, into other walls. Game policy's `assign_spawns` hands out
Spawn points in the order of the Match start, which lists players by Session,
which the server numbers as it admits them: independent processes joining at
once cannot keep that order. So the Reenact request, a new client-to-server
message, takes the Join request's place as a connection's first message for a
Captured player: the same engine version, client pack and Character, checked in
the same order with the same refusals (ADR-0043), and the spawn from the
capture. At Match start the server places each player admitted through a Reenact
request at its spawn, and asks `assign_spawns` only for the others. The Join
request is untouched: a person's client never names a spawn.

**augustad takes Reenact requests only when asked.** A spawn a client names is
taken on trust, so `simulation.reenactments` in `augustad.yaml` (ADR-0034), off
by default, turns them on; with it off, a Reenact request is refused with a new
Join refused reason, _reenactments not accepted_. It is a debugging setting,
never on for a server open to players, as capturing is.

A Reenactment need not have every player captured: the other places can be
anyone's, a person's included, joining with a Join request as always.

**augusta-inspect reads a capture.** Given a file that opens with a capture's
magic instead of a pack's, `augusta-inspect` prints the header (format version,
engine version, server and client packs, tick rate, when the Match started), how
long the Match lasted in ticks and seconds, and one line per player: its number
for `--player`, Session, Character, spawn, how many Commands it sent, and when
it left or died and who killed it; then the Match end's winner or a Draw, and
whether the file ends partway through a record. It decodes every record but a
Command's contents, which it only counts, so a capture's size does not slow it.

**A Reenactment reproduces conditions, not outcomes.** Every client action
reaches the server in the same order, on or within a tick or two of the same
tick: the command queue hands one Command a tick, so a Command that arrives a
little early or late still goes to the World in its turn. What the server
resolves from them can differ from the playtest from the first round fired, and
nothing checks it. Seeing the Match as it was played is a Replay's job
(ADR-0051).

## Consequences

- **The determinism check of ADR-0048 goes.** Its Replay compared every tick's
  outcome on a fresh SimulationWorld, and the golden match held every build to
  it (ADR-0013, NFR-09). A Reenactment compares nothing but Deaths and the Match
  end, which an open-loop run cannot be held to; ADR-0051's Replay compares the
  same two, on the server, without a network in between. US-21 and NFR-09 are
  rewritten or dropped with these ADRs, and the Match recording's code,
  `augusta-replay` and the golden match are removed.
- **Captured players run only where augustac does**, on Windows with a GPU, one
  window each: a Reenactment is for bringing back a playtest's problem, not for
  load tests or CI, which stay with augusta-swarm's Scripted players (ADR-0013).
- **A capture is valid only with its packs and protocol.** A change to a
  Command's encoding, to the Map or to the scenario's Characters leaves older
  captures unplayable: a spawn is a position, not a Spawn point by name, and a
  changed Map can put it inside a wall.
- **A crash in decoding is out of reach.** A capture holds Commands the server
  took in, in its own encoding, never the bytes that reached it; malformed input
  is the protocol fuzzers' to find (ADR-0013).
- **The protocol changes.** `MatchStartWire` gains a field and the Reenact
  request is a new message, so a client of an older engine version is refused at
  Join, as for any protocol change.
- **With reenactments on, a client places its own body.** Anyone who can reach
  such a server can spawn wherever they like, which is why it is off by default
  and only for a server run to bring back a playtest.

## Considered Options

- **Keeping ADR-0048's Match recording and Replay beside the capture**:
  rejected - two ways of keeping a Match and two file formats, with a golden
  match to rewrite on every change to the simulation; one capture serves both a
  Reenactment and ADR-0051's Replay.
- **Recording the network traffic with its wall-clock arrival times**:
  rejected - the tick a Command goes to the World is decided by the server's
  queue and pacing, not by when its datagram arrives, so time offsets would add
  the run machine's jitter to every Command's tick; and the traffic repeats
  unacknowledged Commands and carries Lobby messages a capture leaves out.
- **Capturing the whole server run, Lobby included**: rejected - a Reenactment
  brings its own Lobby, and a problem in a Match needs that Match, not the hours
  of Lobby around it.
- **Absolute ticks, rebased when played**: rejected - the capture would name
  ticks of a server that is gone; offsets from the Match's first tick make every
  tick, Seen times included, one addition on the server played against.
- **Numbering ticks from 0 in every Match, in the engine**: rejected - the tick
  is the server's monotonic clock: unreliable States are ordered by it across a
  Match end, and the Hitbox history and command queues are keyed by it. A
  per-Match tick is already one subtraction from the Match's first tick.
- **Capturing on each client**: rejected - a playtest would leave one file per
  machine to gather, each with send times instead of the ticks the server used.
- **Sending the captured bytes untouched**: rejected - a Seen time names a tick
  of the server that made the capture, which the server played against holds to
  the newest State it sent or to `kMaxShootersDelay`, so every round would be
  judged at one extreme of Lag compensation.
- **Captured players in augusta-swarm, headless**: rejected - augustac shows the
  Match as the captured player saw it, which is half of what bringing back a
  playtest's problem is for; augusta-swarm stays the Scripted players'.
- **One augustac playing every player of a capture**: rejected - only one of
  them could be seen, and one process would no longer stand for one client.
- **Ordering the processes' joins, so `assign_spawns` places each where it
  did**: rejected - it needs one process to wait for another's admission,
  through a channel between them or a delay per `--player` that a slow machine
  outruns, and it holds only if Game policy assigns the same spawns to the same
  order every time.
- **The server reading the capture and placing each player from it**: rejected -
  the capture would have to be on the server's machine as well as every
  client's, and the server would need to be told which captured player each
  connection is; the Reenact request carries the one thing the server needs.
- **A spawn field on the Join request**: rejected - every person's Join would
  carry a field only a Captured player fills, and the server would check on
  every Join what a separate message lets it refuse by its type.
- **Each length in 4 bytes, up to 64 KiB, as the Match recording's**: rejected
  (#463) - no record comes near 256 bytes, so three of every record's four
  length bytes were zeros, about a tenth of a capture made mostly of Commands. A
  record type that ever needs more is a new format version, which the header
  carries.
- **Each length as a varint**: rejected - one byte for every record a capture
  holds, as a plain byte is, for a decoder of its own and a longer record no
  capture has.
