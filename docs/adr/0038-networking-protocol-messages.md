# Networking Protocol: Message Catalogue and Reliability Split

Extends [ADR-0007](0007-serialization-format.md) (custom binary format) with the
shape of a message and which messages the transport delivers reliably. The
protocol is the shared `augusta_protocol` module: a pure codec with no socket,
clock or state, used by both client and server (ADR-0006).

**Wire shape.** One message is one transport payload: a one-byte `MessageType`
followed by that type's fields, fixed-width and little-endian; a string is a
one-byte length and its bytes. Enumerated fields start at 1, so a zeroed byte is
never a valid value. There is no length prefix on the payload itself, since the
transport already frames messages.

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
| Join accepted | server → client | reliable | session ID, the player's spawn position, the stamina rules to predict with, and the roster: every player already in the match (at most 8) with session ID and body |
| Join refused | server → client | reliable | reason: version mismatch, match full |
| Commands | client → server | unreliable | up to 8 commands, oldest first: sequence, movement direction, sprint, desired stance, yaw, pitch, ADS, fire, reload |
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
  a command with a non-finite or out-of-range number. The codec still decodes
  such numbers faithfully; judging them is the server's sanity gate, kept apart so
  the anti-cheat baseline (US-15) grows in one place.
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
policy. The client starts its prediction there, not at the origin. The **stamina
rules** are the server's configuration, sent so the client predicts its forced
walk with the server's numbers and never with values of its own; the two cannot
drift. The **roster** is who was already in the match and where, so a joining
client sees the world as it is and not an empty one; the joining player itself is
not in it. The server keeps each player's last reported body (a joiner is at its
spawn point until the first tick reports it), so back-to-back joins see each
other. From then on the Authoritative State lists everyone.

**Failure paths.** The server drops what does not decode, is not a client message
or fails the command gate (a non-finite or out-of-range number), and logs it as
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
