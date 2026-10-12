# Replay: augustad Re-runs a Match Capture and Streams It to Replay Viewers

A Reenactment (ADR-0050) brings back a playtest's conditions through live
clients, but not what was seen: its outcome drifts from the first round fired,
and it needs one augustac per player. Watching a captured Match as it was
played, from any player's eyes, is a different job. This ADR decides that the
server re-runs a Match capture itself, on a fresh SimulationWorld, and streams
it to clients that only watch, from the same capture file a Reenactment uses.

**A replay server is augustad in a mode of its own.** `replay.captures` in
`augustad.yaml` (ADR-0034) names a directory of Match captures; set, augustad
runs no Lobby and no Match, refuses every Join and Reenact request with a new
Join refused reason, _replay server_, and serves only Replays. It loads its
server pack as any augustad does, and serves only the captures made on that
pack: a capture names the server pack its Match ran on (ADR-0050), and a Replay
needs that pack's Map, Characters and Game policy. It also serves only the
captures made at its own `simulation.tick_rate_hz` with its scenario's
Characters. It ticks every Replay at its own rate, and the Parameters it loads
are for that rate. Any other capture is left out of its Replay list, and a
Replay request naming one is refused as _unknown capture_. A live server and a
replay server can share a directory, the live one capturing into it: they are
two processes, so a Replay never shares a Simulation thread with a live Match.

**A Replay re-runs the capture, it does not play back an outcome.** A capture
holds what the players did, not what the World resolved, so the replay server
builds a fresh SimulationWorld from its content, as the live server does, and
hands it the capture tick by tick:

1. At the Match's first tick, a Match start with the capture's players, each at
   its captured spawn, with Game policy's `assign_spawns` not asked.
2. Before each tick, the bodies of the players whose Leave falls on it are taken
   out, as the live server took them out.
3. Each player's Commands go through a `server::CommandQueue` of its own, each
   queued just before the tick it was handed to the World on, so the queue holds
   and idles on the ticks with nothing new exactly as the live server's did: the
   capture leaves those out (ADR-0050) because the queue makes them again. Each
   Seen time's tick is moved onto the Replay's ticks by the same offset.
4. Every tick lasts one tick at the capture's tick rate, as on the live server.

The Replay ends at the World's Match end, or at the capture's if the World has
not ended it by then.

**A Replay checks itself against the capture's markers.** The World is handed
what the live World was handed, from the same content, and Game policy has no
source of randomness, so on the build that made the capture each Death and the
Match end come out as captured. The replay server compares them and logs the
first that differs: on that build it is a non-determinism to chase; on another
it can be floats rounding differently (ADR-0008, ADR-0045), and nothing holds a
Replay across builds to a tolerance or re-syncs it. It is a smoke check, not
ADR-0048's per-tick comparison.

**A Replay viewer asks with a Replay request, by a capture's name.** Two new
client-to-server messages take the Join request's place as a connection's first
message:

- **Replay list request**: the replay server answers with every capture it can
  replay, each by its file name with when it started, how long it lasted and its
  players' Characters, then closes the connection.
- **Replay request**: the engine version and client pack, checked as a Join's
  are (ADR-0043), and the name of a capture from that list. A name is matched
  against the directory's listing, never opened as a path, so a request cannot
  reach a file outside it; one not in the listing is refused with a new reason,
  _unknown capture_.

A Replay request accepted starts a Replay of its own for that viewer, so two
viewers of one capture each watch from the start at their own pace. The replay
server runs at most `replay.max_viewers` at once, and refuses the next with
_lobby full_, the reason a full Lobby gives. A viewer sends nothing once its
Replay starts: whatever it sends later, a request among them, is dropped and
counted as misbehaviour (ADR-0049), and its Replay goes on.

**Each viewer costs the Simulation thread one Match's tick.** The replay server
ticks every Replay it runs on its one Simulation thread, each a SimulationWorld
of its own plus what its viewer is sent. So `replay.max_viewers` Replays share
NFR-01's tick budget, 16.7 ms at 60 Hz. The benchmark of a full Match's Replay
(`BM_ReplayViewerTick`, `tests/replay_benchmark.cpp`) puts one viewer's tick at
about a live tick's World tick (`BM_SimulationTick`): about 27 µs in the dev
container. At that, the 255 viewers the setting allows fit in under half the
budget. A slower machine or a heavier scenario must cap `replay.max_viewers` to
fit. `augustad_replay_tick_duration_seconds` times each Replay's tick,
`augustad_replays` counts them, and late and overrun ticks show in the Tick
family as they do on a live server. The replay server lists its captures and
builds each Replay's World on the Network I/O thread, which keeps what it read
of each capture, so a list request costs a look at the directory rather than a
read of every file. A tick takes its lock only to find its Replays and to close
the ones that ended, never while stepping them.

**A Replay viewer is a Spectator from the first tick.** The viewer is sent what
a Match's Spectator is: the tick rate and Parameters, in a Join accepted whose
session is 0, which no player has, since Session IDs start at 1 (ADR-0038). That
is the value a Match end gives for a Draw, and a viewer plays no one, so neither
can name it as a winner. Then the Match start with its first tick, every
Authoritative State, Shot and Death, and the Match end, after which the replay
server closes the connection. It has no body, sends no Commands and predicts
nothing; it draws every body by interpolating Authoritative States, as any
client draws the other players, so it is as smooth as the other players always
are. It watches the capture's first player and moves on with fire, as a
Spectator does.

**A Replay sends what the watched player saw.** A Spectator's camera looks level
from the hip, since a remote player's pitch and ADS are not replicated. A Replay
viewer is there to see what the player saw, so each tick the replay server also
sends a Replay view: every player's pitch and whether it held ADS, from the
Command the World ran that tick. It travels unreliably, as the Authoritative
State does, and only to Replay viewers: live Authoritative States do not grow.

**augustac watches a Replay: `--replays` and `--replay <capture>`.** As for a
Reenactment, a capture is chosen for one run, so it joins the command line
(ADR-0034). `augustac --replays` asks the server `augustac.yaml` names for its
list, prints it and exits; `augustac --replay <capture>` watches that capture.
The keyboard and mouse move nothing but which player is watched.

**A live server tells a client asking for Replays that it is none.** A Replay
list request or a Replay request sent to a live server is refused with another
new reason, _not a replay server_, so `augustac --replays` or `--replay` pointed
at the wrong server says so at once instead of waiting.

## Consequences

- **A Replay reproduces a crash in the simulation, not in the network.** The
  World is handed the capture in process, so a crash in SimulationWorld or Game
  policy comes back, every time, with no clients to start; one in the server's
  sessions, queues or sockets needs a Reenactment.
- **A Replay plays forward only.** Seeking means re-running from the first tick,
  and pausing or changing speed means the replay server ticking a run at another
  rate; none is decided here.
- **The protocol grows by four messages and three refusal reasons**: the Replay
  list request, the Replay list, the Replay request and the Replay view, with
  _replay server_, _unknown capture_ and _not a replay server_. A client of an
  older engine version is refused, as for any protocol change.
- **A replay server serves only its own pack's captures.** A capture made before
  a change to the scenario needs a replay server started on the pack it was made
  on.

## Considered Options

- **Re-running the capture on the client**: rejected - the client would need the
  server pack and Game policy, which never leave the server.
- **Capturing every Authoritative State and streaming it back**: rejected - a
  capture many times larger for a playback that reproduces nothing; re-running
  costs a World per viewer and brings back simulation crashes.
- **Replays on the live server, beside its Match**: rejected - the server runs
  one World and one Lobby; a second World on its Simulation thread would share
  NFR-01's tick budget with a Match people are playing.
- **The client naming a capture by its path**: rejected - a path from the
  network can name any file the server can read; a name matched against the
  directory's listing cannot.
- **One Replay of a capture shared by every viewer**: rejected - viewers who
  connect late would join it partway, with no way back to the start.
- **Replicating pitch and ADS in every Authoritative State**: rejected - every
  live client would pay for what only a Replay viewer uses.
