# Networking Protocol: Message Catalogue and Reliability Split

Extends [ADR-0007](0007-serialization-format.md) (custom binary format) with the
shape of a message and which messages the transport delivers reliably. The
protocol is the shared `augusta_protocol` module: a pure codec with no socket,
clock or state, used by both client and server (ADR-0006).

**Its own types.** A message holds only types the protocol defines itself (its
body state, command, stance and parameters, as plain fields) and the math types
(`math::Vec3`); never another module's structs. So `augusta_protocol` depends on
nothing but `augusta_math`, and a module changing its own structs never changes
what travels. A protocol type that mirrors one of the engine's carries the
suffix `Wire`: `BodyStateWire` for `physics::BodyState`, and the Authoritative
State message is `AuthoritativeStateWire`, for the client's
`harness::AuthoritativeState`. The other messages have no engine counterpart and
keep the names of the table below. Each peer converts between the protocol's types and its own at its
edge and nowhere else: the server in `server::Host` and the client in
`harness::Session`. Inside each peer, modules pass the engine's types.

**Extended by ADR-0042**: Join request also carries the chosen character's path,
Join refused gains the *unknown character* reason, and Join accepted, each Roster
entry and each player in Authoritative State carry a character index.

**Extended by ADR-0043**: Join refused gains *match in progress*, a reliable Lobby
update replaces the Roster in Join accepted, the client sends Ready naming the
Lobby version it loaded for, and reliable Match start and Match end messages
bound each match. The per-player character index leaves Authoritative State.

**Wire shape.** One message is one transport payload: a one-byte `MessageType`
followed by that type's fields, fixed-width and little-endian; a string is a
one-byte length and its bytes. Every field takes the smallest type that holds
what it says: flags travel as bits of one byte, which a small enumeration shares
where it fits, and the bits no field uses are 0. Message types and refusal
reasons start at 1, so a zeroed byte is never one. There is no length prefix on
the payload itself, since the transport already frames messages.

**Quantized numbers.** A body's and a command's numbers travel as a whole count
of a grid's step, not as floats. The step is a power of two, so a count times
its step is an exact float, and a value read back encodes to the same bytes:

| Number | Bytes per value | Step | Range |
| --- | --- | --- | --- |
| position (a body's, the spawn point), per axis | 3 (signed) | 1/1024 m | ±8192 m |
| velocity, per axis | 2 (signed) | 1/512 m/s | ±64 m/s |
| movement direction, per axis | 2 (signed) | 1/16384 | ±2 |
| yaw, pitch | 2 (signed) | 1/8192 rad | ±4 rad |
| stamina | 2 (unsigned) | 1/32768 | 0 to 2 |

A value beyond its range travels as the bound, and a NaN travels as 0. The tick
rate and the parameters stay 32-bit floats: they are sent once, and must arrive
exactly. So a body is 18 bytes and a command 11. The protocol exposes each grid
as a `Snap` function (`protocol::SnapPosition` and the rest), which gives what
`Decode` would give back.

**Bodies live on the grid.** `physics::World` rounds every body to these grids
in `CreateBody`, `Step`, `SetState` and `Restore`. So a server's body is exactly
what its clients are told, spawn point included, and a client's prediction rounds
the same way the server does, in replays too. One step (about 0.98 mm) is within
the 1 mm reconciliation tolerance (ADR-0004), so a client that predicts correctly
never corrects for rounding.

**Untrusted input.** `Decode` never throws: it returns
`std::expected<Message, DecodeError>` (ADR-0033) where the error is `kEmpty`,
`kUnknownType`, `kTruncated`, `kTrailingBytes`, `kInvalidEnum` or `kFieldTooLong`.
It checks every length against the bytes remaining before allocating, and rejects
a payload with bytes left over once its message is complete, so a message has
exactly one encoding. Every receiver drops what fails to decode and logs it; a
malformed message never changes state.

**Reliability split.** The sender names a `networking::Reliability` for every
send (ADR-0003's transport offers both). A message that must arrive, and whose
loss would leave the two sides disagreeing, is reliable; a message a newer one
supersedes is unreliable.

| Message | Direction | Reliability | Fields |
| --- | --- | --- | --- |
| Join request | client → server | reliable | engine version |
| Join accepted | server → client | reliable | session ID, the player's spawn position, the server's tick rate, the parameters to predict with, and the roster: every player already in the match (at most 8) with session ID and body |
| Join refused | server → client | reliable | reason: version mismatch, match full |
| Commands | client → server | unreliable | up to 8 commands, oldest first: sequence, movement direction, yaw, pitch, and one byte holding the sprint, ADS, fire and reload flags (bits 0-3) and the desired stance (bits 4-5) |
| Authoritative State | server → client | unreliable | server tick, the recipient's acknowledged command sequence, and per player (at most 8): session ID, position, velocity, stance, stamina |

Per-tick traffic is unreliable because a newer message supersedes an older one,
and it is made loss-tolerant without retransmission:

- **Commands are repeated until acknowledged.** Each command has a sequence number
  (from 1, one per client tick). Every Commands message carries all the commands
  the client has not yet seen acknowledged, capped at the newest 8, so one lost
  datagram does not drop input. The Authoritative State update carries, for its
  recipient, the highest sequence the server has processed; the client forgets
  commands up to it.
- **The server takes each command in once.** A sequence not newer than the last
  taken in from that client is dropped (routine, since commands repeat), and so is
  a command with a number beyond what a client produces (a pitch past straight up,
  say). The codec cannot carry a non-finite number, and it clamps to each grid's
  range. Judging what is left within those ranges is the server's sanity gate,
  kept apart so the anti-cheat baseline (US-15) grows in one place.
- **One command per tick.** The server consumes one queued command per tick per
  player. If none is queued it repeats the last movement for about 100 ms and then
  reduces the player to no movement; a repeated tick never repeats a one-shot
  action. The acknowledged sequence is the last command actually consumed.
- **State is newest-wins.** An update carries the server tick, and a client
  ignores one not newer than the update it holds. Every recipient is sent every
  player.

**Joining.** A connection is accepted at the transport unconditionally, because a
refusal is itself a message and needs a connection to travel on. The client's
first message is a Join request carrying its engine version; the server admits it
only on an exact match with its own and while the match holds fewer than 8
players, checking the version first so a client that can never play here is not
told "full". After a refusal the client closes the connection.

**What a join carries.** Join accepted tells the client everything it must know
before its first tick, so nothing is learned by guessing. The **spawn position**
is where the server put the player: the next spawn point of the map's pack in
order, starting over after the last, a mechanism until spawn rules become Game
policy. The client starts its prediction there, not at the origin. The **tick
rate** is the server's startup setting (ADR-0034, ADR-0039), fixed for the life of
the server process, so it is told here once and never again; the client ticks at
it and starts no tick before it has it, and drops a Join accepted whose rate is not
finite and above zero. The **parameters** are the server's data-driven
configuration (ADR-0039), the stamina rules among them, fixed for the run and
sent so the client predicts with the server's numbers and never with values of
its own; the two cannot drift. A client drops a Join accepted whose parameters
fail the range checks of ADR-0039, as it drops any message that does not decode.
The **roster** is who was already in the match and where, so a joining
client sees the world as it is and not an empty one; the joining player itself is
not in it. The server keeps each player's last reported body (a joiner is at its
spawn point until the first tick reports it), so back-to-back joins see each
other. From then on the Authoritative State lists everyone.

**Failure paths.** The server drops what does not decode, is not a client message
or fails the command gate (a number beyond what a client produces), and logs it as
`dropped_malformed`; a peer's garbage never reaches the world or another client.
A stale command is routine, since commands repeat, and only traced. A connection that ends frees its slot at once and
removes the player at the start of the next tick; the log tells `left` (the peer
closed it) from `timeout` (the transport gave up on it). The client has no
reconnecting and no connection screen: it ends the session with one of three
failures, refused (with the reason), server unreachable (the connection ended
before the server admitted it) or connection lost (after), reports it, and exits
non-zero.

**Session ID.** The server names each admitted player with a session ID it
generates itself: a counter that is never reused, deliberately unrelated to the
transport's connection handle, so identifying a player inside a message does not
depend on how the connection is represented. It identifies; it does not
authenticate. The server tells senders apart by connection, so a guessed ID grants
nothing, and it is kept out of logs per ADR-0029 regardless.

## Considered Options

- **Refuse at the transport instead of with a message**: rejected — the transport
  gives the client only "connection closed", so the player could not be told why
  (US-01 asks for the reason).
- **Version as a number or hash instead of the engine version string**: rejected
  for now — the string is what `augusta::EngineVersion()` already is, exact
  equality is the rule, and 32 bytes at join time cost nothing.
