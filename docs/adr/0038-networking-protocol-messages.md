# Networking Protocol: Message Catalogue and Reliability Split

Extends [ADR-0007](0007-serialization-format.md) (custom binary format) with the
shape of a message and which messages the transport delivers reliably. The
protocol is the shared `augusta_protocol` module: a pure codec with no socket,
clock or state, used by both client and server (ADR-0006).

**Its own types.** A message holds only types the protocol defines itself (its
body state, command, stance and parameters, as plain fields) and the math types
(`math::Vec3`); never another module's structs. So `augusta_protocol` depends on
nothing but `augusta_math`, which also holds the grids its numbers travel on, and a module changing its own structs never changes
what travels. A protocol type that mirrors one of the engine's carries the
suffix `Wire`: `BodyStateWire` for `physics::BodyState`, and the Authoritative
State message is `AuthoritativeStateWire`, for the client's
`harness::AuthoritativeState`. Every protocol type carries the suffix, so none
reads like an engine type. Each peer converts between the protocol's types and its own at its
edge and nowhere else: the server in `server/wire.h` (used by `server::Host`
right after Decode and right before Encode) and the client in
`augusta/harness_wire.h` (used the same way by `harness::Session`). Inside each
peer, modules pass the engine's types, never a `Wire` one: each peer names a
player by its own Session ID type (`harness::SessionId` for the harness's API
and ClientRuntime; `server::SessionId` for `server::Match` and `server::Host`),
a body by its own Entity ID type (`harness::EntityId`, which ClientRuntime
converts at its own edge into `presentation::EntityId`, so `presentation` does
not depend on the harness; `server::EntityId`, which `server::Host` hands
SimulationWorld and `replication` as the `simulation::EntityId` of the same
number), a pack by `assets::PackHash`, and a
refusal by its own `JoinRefusal`; `tests/protocol_boundary.cmake` fails the build's tests if one of their
headers names the protocol, or a `presentation` header the harness.

**Extended by ADR-0042**: Join request also carries the chosen character's path,
Join refused gains the *unknown character* reason, and Join accepted, each Roster
entry and each player in Authoritative State carry a character index.

**Extended by ADR-0043**: Join refused gains *match in progress* (and *match
full* is renamed *lobby full*, same value), a reliable Lobby update replaces the
Roster in Join accepted, the client sends Ready naming the Lobby version it
loaded for, and reliable Match start and Match end messages bound each match.
The spawn position moves from Join accepted to Match start, which gives every
player's. The per-player character index leaves Authoritative State.

**Wire shape.** One message is one transport payload: a one-byte `MessageType`
followed by that type's fields, fixed-width and little-endian; a string is a
one-byte length and its bytes. Every field takes the smallest type that holds
what it says: flags travel as bits of one byte, which a small enumeration shares
where it fits, and the bits no field uses are 0. Message types and refusal
reasons start at 1, so a zeroed byte is never one. There is no length prefix on
the payload itself, since the transport already frames messages.

**Counters are wide enough never to wrap.** The server tick counts from 1 for
the life of the server process and starts over at neither a Match nor a
reconnect, so it travels in 8 bytes wherever it appears (an Authoritative
State's tick, a Shot's, a Commands message's view tick): 4 would wrap after
about 828 days at 60 Hz. A command sequence, and the acknowledged sequence that
answers it, counts one connection's commands and starts over at 1 on the next,
so 4 bytes outlast any session. Neither ever wraps, so every receiver orders
them as plain numbers.

**Quantized numbers.** A body's and a command's numbers travel as a whole count
of a grid's step, not as floats. The step is a power of two, so a count times
its step is an exact float, and a value read back encodes to the same bytes:

| Number | Bytes per value | Step | Range |
| --- | --- | --- | --- |
| position (a body's, the spawn point, a Shot's origin), per axis | 3 (signed) | 1/1024 m | ±8192 m |
| velocity, per axis | 2 (signed) | 1/512 m/s | ±64 m/s |
| movement direction, per axis | 2 (signed) | 1/16384 | ±2 |
| yaw, pitch (a command's view, a body's facing, a Shot's direction, a rifle's Recoil offset) | 3 (signed) | 2⁻²¹ rad (about 0.5 µrad) | ±4 rad |
| stamina | 2 (unsigned) | 1/32768 | 0 to 2 |
| a command's view fraction | 1 (unsigned) | 1/256 | 0 to 255/256 |

A value beyond its range travels as the bound, and a NaN travels as 0. The tick
rate travels as one byte of whole Hz, and the parameters stay 32-bit floats:
they are sent once, and must arrive exactly. So do a rifle's two times: its
owner replays its commands from them (ADR-0004) with the function the server
stepped them with, and a rounded start would be a rifle the server never had.
So a body is 18 bytes (25 in an update, with its entity ID and its yaw), a
rifle 16 and a command 15, a whole Shot message is 28, a whole Hit
confirmation 10 and a whole Death 16. A player's own health is a 32-bit float
too, the Parameters' starting health counted down by the damage the
Parameters give, and arrives as the server has it.

**Aim is not the network's to blur.** The server fires with the angle it was
sent, so the angle grid decides how far a shot lands from where the player
aimed: at most half a step, 2⁻²² rad, under 0.2 mm at 800 m. That leaves
precision to the weapon's own spread (a good rifle is about 0.3 mrad), which is
a design choice, and not to the codec. The two extra bytes per angle cost 4 per
command, about 1 KB/s more upload per client at 60 Hz with 8 commands a message. The grids live in the shared
`augusta_math` module (`augusta/grid.h`), which the protocol's encoder and
decoder and `augusta_physics` both use, so neither depends on the other and the two cannot
diverge. Each grid has a `Snap` function (`math::SnapPosition` and the rest),
which gives what `Decode` would give back.

**A Shot is fired on the grid.** SimulationWorld rounds a Shot's origin and
direction to these grids before it fires the bullet, so the round the server
flies and judges is exactly the one every client is told of and draws
(ADR-0044).

**Recoil lives on the grid, and off the Command.** The rifle's rules
(`weapon::Step`) round a rifle's Recoil offset to the angle grid on every tick,
on the server and in the owner's prediction alike, so the offset an update
carries is exactly the one the server had and the replay starts from it. The
offset is the rifle's alone: a Command's yaw and pitch are the player's view,
never the view with recoil on it, and the server adds its own offset to them
when it fires the Shot.

**A body faces where its player looks.** A body's yaw is the yaw of the last
Command the server took in from its player, on the angle grid: the server turns
the body's hitboxes by it (ADR-0040), and every client is told it, to turn the
body it draws.

**A player learns its own health, and no one else's.** The server alone holds
every player's health. Each Authoritative State update tells its recipient its
own, next to its rifle, so a client knows how hurt its player is and when it
is dead; another player's health never travels, since it would tell an enemy
how close to death an opponent is. A dead player's body is simply absent from
the per-body list, and a dead recipient is told a health of 0.

**A Death is told to everyone, reliably.** A player whose health reaches zero
dies on that tick, for the rest of the Match (US-13). Every client in the
Match, the victim's included, is sent a Death: which body died, whose round
killed it, the Body part it struck and the direction it was fired in, on the
angle grid. Its loss would leave a client showing a player that is gone, so it
is reliable. The direction and the Body part are what a ragdoll starts from
(ADR-0045), so one can be added without changing the message. A client drops a
Death that arrives outside a Match or names a body not in the one in
progress, as it drops such an update.

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
| Join request | client → server | reliable | engine version, client pack hash, the chosen character's path (ADR-0042) |
| Join accepted | server → client | reliable | session ID, the server's tick rate, the parameters to predict with (the Player count, the stamina rules, the rifle with its recoil pattern of at most 64 kicks, its ammo with damage by body part, and the starting health), the player's own character index |
| Join refused | server → client | reliable | reason: version mismatch, pack mismatch, unknown character, match in progress, lobby full |
| Commands | client → server | unreliable | up to 8 commands, oldest first: sequence, movement direction, yaw, pitch, one byte holding the sprint, ADS, fire and reload flags (bits 0-3) and the desired stance (bits 4-5), and the view the command was sampled against (ADR-0044) as one byte for how many ticks before the message's view tick its own is and one for its fraction; then the message's view tick, the newest server tick any of its commands was sampled against |
| Authoritative State | server → client | unreliable | server tick, the recipient's acknowledged command sequence, per body (at most 8): entity ID, position, velocity, one byte holding the stance (bits 0-1) and the exhausted flag (bit 2), stamina, the yaw it faces; then one byte: how many of the recipient's commands the server still holds queued after the tick; then the recipient's own rifle as of the tick, to reconcile its predicted one against (ADR-0004): one byte for the rounds in its magazine, the time until its next round may fire and the time its reload still takes, each a 32-bit float, one byte for the rounds its Burst has fired, and its Recoil offset as a pitch and a yaw; then the recipient's own health as a 32-bit float, 0 once it has died. A dead player's body is not in the list |
| Lobby | server → client | reliable | the Roster's version, and every player in the Lobby (at most 8, the recipient included) with session ID and character index (ADR-0043) |
| Ready | client → server | reliable | the Lobby version the client loaded for (ADR-0043) |
| Match start | server → client | reliable | every player in the Match (at most 8, the recipient included): session ID, the entity ID of its body, character index, spawn position (ADR-0043) |
| Match end | server → client | reliable | the Match is over and its players are back in the Lobby (ADR-0043), sent to every player still in it after the deaths of its last tick: the session ID of the winner Game policy declared (US-14), or 0 for a draw (session IDs start at 1) |
| Shot | server → client | reliable | one round a player fired, sent to every player in the Match, the shooter included: the entity ID of the shooter's body, the server tick it was fired on, its origin, and its direction as a yaw and a pitch (ADR-0044) |
| Hit confirmation | server → client | reliable | one round the recipient fired that hit a player, sent to the shooter alone: the entity ID of the body hit, one byte for the body part (head, torso or limb, from 1), and the damage as a 32-bit float, as the Parameters give it (ADR-0044) |
| Death | server → client | reliable | one player in the Match died, sent to every player in it, the victim included: the entity ID of the victim's body, the entity ID of the killer's body (whose round killed it, which never lacks one: in v1 only rounds kill), one byte for the body part the killing round struck (from 1), and the direction it was fired in as a yaw and a pitch (US-13, ADR-0045) |

Per-tick traffic is unreliable because a newer message supersedes an older one,
and it is made loss-tolerant without retransmission:

- **Commands are repeated until acknowledged.** Each command has a sequence number
  (from 1, one per client tick). Every Commands message carries all the commands
  the client has not yet seen acknowledged, capped at the newest 8, so one lost
  datagram does not drop input. The Authoritative State update carries, for its
  recipient, the highest sequence the server has processed; the client forgets
  commands up to it.
- **A command's view tick is an age.** Each command names the server tick of
  the update its player was being shown (ADR-0044). The commands of one message
  were sampled a tick apart, so their view ticks are a few ticks apart too: the
  message carries the newest of them once, in full, and each command how many
  ticks before it its own is, in a byte. A view more than 255 ticks before the
  message's travels as 255, at least a second old at any tick rate and so
  already past the 250 ms a shot is judged within. The fraction's last step is
  255/256: a view a whole tick on names the next tick instead.
- **The server takes each command in once.** A sequence not newer than the last
  taken in from that client is dropped (routine, since commands repeat), and so is
  a command with a number beyond what a client produces (a pitch past straight up,
  say). The codec cannot carry a non-finite number, and it clamps to each grid's
  range. Judging what is left within those ranges is the server's sanity gate,
  kept apart so the anti-cheat baseline (US-15) grows in one place.
- **One command per tick.** The server consumes one queued command per tick per
  player. If none is queued it repeats the last movement for 100 ms (a duration,
  converted to ticks at the server's tick rate and rounded up, as the Match pause
  is) and then reduces the player to no movement; a repeated tick never repeats a
  one-shot action. The acknowledged sequence is the last command actually
  consumed. A queue holds at most 16 commands; when a client runs further ahead
  the oldest go, and the server's heartbeat counts each as `overflow=`.
- **Clients pace their ticks to the server's.** Nothing else keeps a client's
  rate of commands equal to the server's rate of consuming them: a client whose
  clock runs slightly fast fills its queue (adding input latency, then drops),
  one slightly slow empties it (and the server holds its last movement, which
  the client then corrects). So each Authoritative State update tells its
  recipient how many of its commands are still queued, and the client lengthens
  its next tick when that is above 1.5 and shortens it when below, by 4% per
  command and never more than 5% either way, which keeps one or two queued
  against a clock up to about 2% off. Only when the client's ticks happen
  changes: each still simulates the nominal tick of the rate told in Join
  accepted (ADR-0039), and the server still consumes one command per tick.
- **State is newest-wins.** An update carries the server tick, and a client
  ignores one not newer than the update it holds. Every recipient is sent every
  player.

**Joining.** A connection is accepted at the transport unconditionally, because a
refusal is itself a message and needs a connection to travel on. The client's
first message is a Join request carrying its engine version and the hash of the
client pack it loaded (the 32-byte BLAKE3 hash its trailer signs, ADR-0031); the
server admits it only on an exact match of the version with its own, and of the
hash with the one its server pack carries for the client pack cooked with it;
ADR-0042 and ADR-0043 add the character and the Lobby's checks, and ADR-0043
gives the order of them all. Both packs being signed by the same key proves only
that each is genuine, not that they are the same cook: a client on another cook
could hold different collision or characters and mispredict every tick. After a
refusal the client closes the connection.

**What a join carries.** Join accepted tells the client everything it must know
before its first tick, so nothing is learned by guessing. Who else is playing,
and where each player's body starts, is not in it: the Lobby update tells the
first and Match start the second (ADR-0043). The client starts its prediction at
the Spawn point Match start gives it, not at the origin. The **tick
rate** is the server's startup setting (ADR-0034, ADR-0039), fixed for the life of
the server process, so it is told here once and never again; the client ticks at
it and starts no tick before it has it, and drops a Join accepted whose rate is
0 (a whole number of Hz, 1 to 255, as `augustad.yaml` takes it). The **parameters** are the server's data-driven
configuration (ADR-0039), the stamina rules, the rifle, its ammo and the
starting health among them, fixed for the run and
sent so the client predicts with the server's numbers and never with values of
its own; the two cannot drift. A client drops a Join accepted whose parameters
fail the range checks of ADR-0039, as it drops any message that does not decode.

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
nothing. Not being a credential, it may be logged (`session=`), and is: without it
the log cannot tell one player's lines from another's to reconstruct a Match.
ADR-0029's rule stands for what is a credential - no session/auth token or ticket
is ever logged.

**Entity ID.** A body is named by an entity ID, not by the session of the player
who moves it: the two answer different questions (which body is this; whose
commands move it), and a body no player moves (a crate, a door) has no session
at all. The server makes one for each player's body at Match start from a
counter of its own, never reused across matches, and Match start pairs each
player's session with it; the Authoritative State then names every body by its
entity ID alone. A client finds its own body through that pairing, and
presentation draws every body by it. On the wire it is 4 bytes, in each Match
start entry and in place of the session ID in each Authoritative State body.

## Considered Options

- **Refuse at the transport instead of with a message**: rejected — the transport
  gives the client only "connection closed", so the player could not be told why
  (US-01 asks for the reason).
- **Angles as int16 counts of 1/8192 rad**: rejected — a shot could land about
  5 cm off at 800 m, an error the network adds and no weapon has.
- **The client aiming with the rounded angle**: rejected — a scoped view would
  move in visible steps of about 0.1 mrad.
- **Leaving the angle error for the weapon's spread to hide**: rejected — spread
  is a per-weapon design choice, and the network's error would add to it on
  every weapon alike.
- **Version as a number or hash instead of the engine version string**: rejected
  for now — the string is what `augusta::EngineVersion()` already is, exact
  equality is the rule, and 32 bytes at join time cost nothing.
