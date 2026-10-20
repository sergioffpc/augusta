# Networking Protocol: Message Catalogue and Reliability Split

Extends [ADR-0007](0007-serialization-format.md) (Protocol Buffers) with the
shape of a message and which messages the transport delivers reliably. The
protocol is the shared `augusta_protocol` module: a pure codec with no socket,
clock or state, used by both client and server (ADR-0006).

**Its own types.** A message holds only types the protocol defines itself (its
body state, command, stance and parameters, as plain fields) and the math types
(`math::Vec3`); never another module's structs. So `augusta_protocol` depends on
nothing but `augusta_math`, which also holds the grids its numbers travel on,
`augusta_tick`, whose Tick and Sequence widths it encodes, and `augusta_limits`,
whose player and recoil bounds it encodes, without owning or re-exporting them;
a module changing its own structs never changes what travels. How many commands
a Commands message carries is the protocol's own bound
(`protocol::kMaxCommandsPerMessage`). `checks/protocol_boundary.cmake` fails if
the protocol includes or links anything else but the protobuf runtime, which it
links `PRIVATE` (ADR-0007), and if a header outside `src/modules/protocol`
includes a protobuf header or the generated one. A protocol type that mirrors
one of the engine's carries the suffix `Wire`: `BodyStateWire` for
`physics::BodyState`, and the Authoritative State message is
`AuthoritativeStateWire`, for the Link's `link::AuthoritativeState`. Every
protocol type carries the suffix, so none reads like an engine type. Every
conversion between the protocol's types and the engine's is the Link's
(`augusta_link`) and nowhere else. The Link has its own message vocabulary in
the engine's terms (`augusta/link_types.h`: a join, an Authoritative State, a
Shot, a tick's Commands, ...): `host::Host` and `harness::Session` hand it those
right before Encode and take them back right after Decode. The Harness's message
types are the Link's; the Host maps its own types and Replication's into them at
its edge (`augusta/link_mapping.h`), naming no protocol type. The Link owns the
transport end to end, too: the client's connection, the server's listening
socket and one channel per peer, and the replay server's endpoint of its own
(ADR-0051), so the Host and the Harness never link `augusta_networking`.
Admission works on the peer identity the Link hands the Host, and the Connection
health collector and the client's netcode stats read each peer's round trip,
loss and queue from the Link, in its own types. Its conversions are stateless
functions in an internal target of their own (`augusta_link_messages`), with no
transport beneath, which also keeps only the newest of a tick's unacknowledged
Commands within `protocol::kMaxCommandsPerMessage` in the tick's one Commands
message, so the Harness never sees the bound. The capture module
(`augusta/capture.h`), shared by both peers, writes and reads a Match capture's
records: its interface speaks the engine's types, and it converts them with
`augusta_link_messages` alone. Inside each peer, modules pass the engine's
types, never a `Wire` one: each peer names a player by its own Session ID type
(`harness::SessionId`, the Link's `link::SessionId`, for the harness's API and
ClientRuntime; `host::SessionId` for `host::Match` and `host::Host`), a body by
its own Entity ID type (`harness::EntityId`, the Link's `link::EntityId`, which
ClientRuntime converts at its own edge into `presentation::EntityId`, so
`presentation` does not depend on the harness; `host::EntityId`, which
`host::Host` hands SimulationWorld and `replication` as the
`simulation::EntityId` of the same number), a pack by `assets::PackHash`, and a
refusal by its own `JoinRefusal`; `checks/protocol_boundary.cmake` fails the
build's tests if one of their headers names the protocol, or a `presentation`
header the harness.

**Extended by ADR-0042**: Join request also carries the chosen character's name,
Join refused gains the _unknown character_ reason, and Join accepted names the
player's own character by the same name.

**Extended by ADR-0043**: Join refused gains _match in progress_ (and _match
full_ is renamed _lobby full_, same value), a reliable Lobby update replaces the
Roster in Join accepted, the client sends Ready naming the Lobby version it
loaded for, and reliable Match start and Match end messages bound each match.
The spawn position moves from Join accepted to Match start, which gives every
player's. Each Lobby entry and each player in Match start carries its
character's name.

**Extended by ADR-0050 and ADR-0051**: a Match capture is written in this
encoding, as records of its own (a header and one per event) under
`augusta.CaptureRecord`, never messages, so it carries a command as the same
`augusta.Command` a Commands message does; Match start gains the Match's first
tick; and the Reenact request, Replay list request, Replay request and Replay
view join the catalogue, with the Join refused reasons _reenactments not
accepted_, _replay server_ and _unknown capture_.

**Extended by ADR-0074**: the engine version names the kind of build, so a
Development build and a Shipping build never join each other; and a Development
build's catalogue holds two messages more, below, that let the server's state
reach a client's Debug overlay.

**Wire shape.** One message is one transport payload: one `augusta.Message`,
whose `oneof body` names the message, laid out as ADR-0007 says. There is no
length prefix on the payload itself, since the transport already frames
messages. Every proto enumeration's 0 is unspecified, which no sender writes, so
an absent field is never a refusal reason or a Body part.

**Counters are wide enough never to wrap.** The server tick counts from 1 for
the life of the server process and starts over at neither a Match nor a
reconnect, so it is a 64-bit number wherever it appears (an Authoritative
State's tick, a Shot's, a Commands message's Seen tick): 32 bits would wrap
after about 828 days at 60 Hz. A command sequence, and the acknowledged sequence
that answers it, counts one connection's commands and starts over at 1 on the
next; it is 64-bit too, so no session, however long, can wrap it. Both are
`augusta_tick`'s (`tick::Tick`, `tick::Sequence`), which the codec and the
engine take alike, and a `uint64` on the wire. Neither ever wraps, so every
receiver orders them as plain numbers.

**Quantized numbers.** A body's and a command's numbers travel as a whole count
of a grid's step, not as floats. The step is a power of two, so a count times
its step is an exact float, and a value read back is the same value, which gives
the same count again:

| Number                                                                                      | Count    | Step                      | Range        |
| ------------------------------------------------------------------------------------------- | -------- | ------------------------- | ------------ |
| position (a body's, the spawn point, a Shot's origin), per axis                             | signed   | 1/1024 m                  | ±8192 m      |
| velocity, per axis                                                                          | signed   | 1/512 m/s                 | ±64 m/s      |
| movement direction, per axis                                                                | signed   | 1/16384                   | ±2           |
| yaw, pitch (a command's view, a body's facing, a Shot's direction, a rifle's Recoil offset) | signed   | 2⁻²¹ rad (about 0.5 µrad) | ±4 rad       |
| stamina                                                                                     | unsigned | 1/32768                   | 0 to 2       |
| a command's Seen time fraction                                                              | unsigned | 1/256                     | 0 to 255/256 |

The sender clamps: a value beyond its range travels as the bound, and a NaN
travels as 0. A count beyond its range is never sent, so `Decode` refuses one
(**Untrusted input**). The tick rate travels as whole Hz, and the parameters
stay 32-bit floats: they are sent once, and must arrive exactly. So do a rifle's
two times: its owner replays its commands from them (ADR-0004) with the function
the server stepped them with, and a rounded start would be a rifle the server
never had. A player's own health is a 32-bit float too, the Parameters' starting
health counted down by the damage the Parameters give, and arrives as the server
has it.

**Aim is not the network's to blur.** The server fires with the angle it was
sent, so the angle grid decides how far a shot lands from where the player
aimed: at most half a step, 2⁻²² rad, under 0.2 mm at 800 m. That leaves
precision to the weapon's own spread (a good rifle is about 0.3 mrad), which is
a design choice, and not to the codec. The finer grid costs about a byte more
per angle than 1/8192 rad would, under 1 KB/s more upload per client at 60 Hz
with 8 commands a message. The grids live in the shared `augusta_math` module
(`augusta/grid.h`), which the protocol's encoder and decoder and
`augusta_physics` both use, so neither depends on the other and the two cannot
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
own, next to its rifle, so a client knows how hurt its player is and when it is
dead; another player's health never travels, since it would tell an enemy how
close to death an opponent is. A dead player's body is simply absent from the
per-body list, and a dead recipient is told a health of 0.

**A Death is told to everyone, reliably.** A player whose health reaches zero
dies on that tick, for the rest of the Match (US-13). Every client in the Match,
the victim's included, is sent a Death: which body died, whose round killed it,
the Body part it struck and the direction it was fired in, on the angle grid.
Its loss would leave a client showing a player that is gone, so it is reliable.
The direction and the Body part are what a ragdoll starts from (ADR-0045), so
one can be added without changing the message. A client drops a Death that
arrives outside a Match or names a body not in the one in progress, as it drops
such an update.

**Bodies live on the grid.** `physics::World` rounds every body to these grids
in `CreateBody`, `Step`, `SetState` and `Restore`. So a server's body is exactly
what its clients are told, spawn point included, and a client's prediction
rounds the same way the server does, in replays too. One step (about 0.98 mm) is
within the 1 mm reconciliation tolerance (ADR-0004), so a client that predicts
correctly never corrects for rounding.

**Untrusted input.** `Decode` never throws: it returns
`std::expected<Message, DecodeError>` (ADR-0033), and it refuses a payload only
when it cannot be a valid message of this build. The error is:

- `kMalformed`: protobuf's parse fails (a payload cut short, a wrong wire type,
  a broken varint, a string that is not UTF-8);
- `kUnknownType`: no body this build knows (an empty payload, no `oneof` member
  set, a member this build lacks, or a Development build's message in a Shipping
  build);
- `kInvalidEnum`: an enumerated field outside its enumeration, its unspecified 0
  included;
- `kReservedBits`: a bitfield with a bit set outside its flags;
- `kOutOfRange`: a count outside its grid's range, or a number wider than its
  `*Wire` field, which no sender produces since senders clamp;
- `kFieldTooLong`: a string or list longer than the protocol's limits.

A message has one meaning, not one encoding (ADR-0007): fields this build does
not know are ignored and an absent one reads as its default, so a later build's
Join request still decodes, and is refused for its engine version. Every
receiver drops what fails to decode and logs it; a malformed message never
changes state. Whatever the error, the Link counts it the same, as a payload
that does not decode (**Failure paths**); the error only names the reason in
logs and tests.

**Outbound invariants.** `Encode` and `EncodeCaptureRecord` check their own side
as strictly, in every build, as they map each `*Wire` to its generated message:
they return `std::expected<BytesWire, EncodeError>` (ADR-0033), where the error
is `kFieldTooLong`, `kReservedBits` or `kInvalidEnum`, and give no payload at
all for a message or record a field of which the protocol cannot carry, so
nothing cut off or misframed is ever sent or captured. Such a field is the
sender's bug, never a peer's input: the Link, and the capture module for a
record, turns the error into a `kInvariantViolated` failure naming the message
or record type, and the runtime that sent it stops (ADR-0033).

**Reliability split.** The Link sends each message with the reliability below,
as a `networking::Reliability` (ADR-0003's transport offers both). A message
that must arrive, and whose loss would leave the two sides disagreeing, is
reliable; a message a newer one supersedes is unreliable.

| Message             | Direction       | Reliability | Fields                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ------------------- | --------------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Join request        | client → server | reliable    | engine version, client pack hash, the chosen character's name (ADR-0042)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Join accepted       | server → client | reliable    | session ID, the server's tick rate, the parameters to predict with (the Player count, the stamina rules, the rifle with its recoil pattern of at most 64 kicks, its ammo with damage by body part, and the starting health), the player's own character's name (ADR-0042)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Join refused        | server → client | reliable    | reason: version mismatch, pack mismatch, unknown character, match in progress, lobby full                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Commands            | client → server | unreliable  | up to 8 commands, oldest first: sequence, movement direction, yaw, pitch, one bitfield holding the sprint, ADS, fire and reload flags (bits 0-3) and the desired stance (bits 4-5), and the command's Seen time (ADR-0044) as how many ticks before the message's Seen tick its own is (0 to 255) and its fraction; then the message's Seen tick, the newest server tick any of its commands was sampled against                                                                                                                                                                                                                                                                                                                                                           |
| Authoritative State | server → client | unreliable  | what every recipient shares: server tick, and per body (at most 8): entity ID, position, velocity, one bitfield holding the stance (bits 0-1) and the exhausted flag (bit 2), stamina, the yaw it faces; then what is the recipient's own: its acknowledged command sequence, how many of its commands the server still holds queued after the tick (0 to 255), its rifle as of the tick, to reconcile its predicted one against (ADR-0004): the rounds in its magazine (0 to 255), the time until its next round may fire and the time its reload still takes, each a 32-bit float, the rounds its Burst has fired (0 to 255), and its Recoil offset as a pitch and a yaw; then its health as a 32-bit float, 0 once it has died. A dead player's body is not in the list |
| Lobby               | server → client | reliable    | the Roster's version, and every player in the Lobby (at most 8, the recipient included) with session ID and character's name (ADR-0043)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| Ready               | client → server | reliable    | the Lobby version the client loaded for (ADR-0043)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Match start         | server → client | reliable    | every player in the Match (at most 8, the recipient included): session ID, the entity ID of its body, character's name, spawn position (ADR-0043)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| Match end           | server → client | reliable    | the Match is over and its players are back in the Lobby (ADR-0043), sent to every player still in it after the deaths of its last tick: the session ID of the winner Game policy declared (US-14), or 0 for a draw (session IDs start at 1)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| Shot                | server → client | reliable    | one round a player fired, sent to every player in the Match, the shooter included: the entity ID of the shooter's body, the server tick it was fired on, its origin, and its direction as a yaw and a pitch (ADR-0044)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Hit confirmation    | server → client | reliable    | one round the recipient fired that hit a player, sent to the shooter alone: the entity ID of the body hit, the body part (head, torso or limb), and the damage as a 32-bit float, as the Parameters give it (ADR-0044)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Death               | server → client | reliable    | one player in the Match died, sent to every player in it, the victim included: the entity ID of the victim's body, the entity ID of the killer's body (whose round killed it, which never lacks one: only rounds kill), the body part the killing round struck, and the direction it was fired in as a yaw and a pitch (US-13, ADR-0045)                                                                                                                                                                                                                                                                                                                                                                                                                                   |

Per-tick traffic is unreliable because a newer message supersedes an older one,
and it is made loss-tolerant without retransmission:

- **Commands are repeated until acknowledged.** Each command has a sequence
  number (from 1, one per client tick). Every Commands message carries all the
  commands the client has not yet seen acknowledged, capped at the newest 8, so
  one lost datagram does not drop input. The Authoritative State update carries,
  for its recipient, the highest sequence the server has processed; the client
  forgets commands up to it.
- **A command's Seen time tick is an age.** Each command names the server tick
  of the update its player was being shown (ADR-0044). The commands of one
  message were sampled a tick apart, so their ticks are a few ticks apart too:
  the message carries the newest of them once, in full, and each command how
  many ticks before it its own is, up to 255. A Seen time more than 255 ticks
  before the message's travels as 255, at least a second old at any tick rate
  and so already past the 300 ms a shot is judged within. The fraction's last
  step is 255/256: a Seen time a whole tick on names the next tick instead.
- **The server takes each command in once.** A sequence not newer than the last
  taken in from that client is dropped (routine, since commands repeat), and so
  is a command with a number beyond what a client produces (a pitch past
  straight up, say). The codec cannot carry a non-finite number, and it clamps
  to each grid's range. Judging what is left within those ranges is the server's
  sanity gate, kept apart so the anti-cheat baseline (US-15) grows in one place.
- **One command per tick.** The server consumes one queued command per tick per
  player. If none is queued it repeats the last movement for 100 ms (a duration,
  converted to ticks at the server's tick rate and rounded up, as the Match
  pause is) and then reduces the player to no movement; a repeated tick never
  repeats a one-shot action. The acknowledged sequence is the last command
  actually consumed. A queue holds at most 16 commands; when a client runs
  further ahead the oldest go, and the server's heartbeat counts each as
  `overflow=`.
- **Clients pace their ticks to the server's.** Nothing else keeps a client's
  rate of commands equal to the server's rate of consuming them: a client whose
  clock runs slightly fast fills its queue (adding input latency, then drops),
  one slightly slow empties it (and the server holds its last movement, which
  the client then corrects). So each Authoritative State update tells its
  recipient how many of its commands are still queued, and the client lengthens
  its next tick when that is above 1.5 and shortens it when below, by 4% per
  command and never more than 5% either way, which keeps one or two queued
  against a clock up to about 2% off. While more than 3 are queued, a backlog a
  gap in its commands leaves once they arrive, it may lengthen its tick by up to
  25%, so 7 queued drain in about 0.75 s rather than 1.8 s; steady, jittery and
  lossy links pace as before (`prototypes/prediction-reconciliation.html`). Only
  when the client's ticks happen changes: each still simulates the nominal tick
  of the rate told in Join accepted (ADR-0039), and the server still consumes
  one command per tick.
- **State is newest-wins.** An update carries the server tick, and a client
  ignores one not newer than the update it holds. Every recipient is sent every
  player.
- **A tick's bodies are encoded once.** Every recipient's update holds the same
  tick and bodies, so the server encodes one and `Readdress` makes each other
  recipient's from it: it keeps the shared fields' bytes and writes the
  recipient's own fields after them, under an envelope header of its own, which
  is byte for byte what `Encode` makes of that recipient's update, since
  protobuf writes known fields in field-number order. It holds only because
  every field the recipients share numbers below every field that is a
  recipient's own, a rule the `.proto` states beside the message.

**Messages of a Development build.** Every body the server holds already reaches
every client in the Authoritative State, so a Debug overlay draws the server's
bodies and Props from the Server view, and needs nothing new. What never left
the server is how it judged a round: the Hitboxes it tested, placed by the
Hitbox history at the Shooter's delay, and what the round struck. Two messages
carry that, and exist only in a Development build (ADR-0074): the one schema
describes them in every build (ADR-0007), but their `*Wire` types, mapping and
Link conversions are compiled only with `AUGUSTA_DEBUG_TOOLS`, so a Shipping
build decodes neither, refusing each as an unknown type, and since the two kinds
of build never join each other (**Joining**), neither ever reaches one.

| Message         | Direction       | Reliability | Fields                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| --------------- | --------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Debug subscribe | client → server | reliable    | one bitfield of flags, one per stream of the server's state the client wants from now on (bit 0: Shot judgements); a cleared bit stops its stream                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| Shot judgement  | server → client | reliable    | one round the recipient fired, sent once its flight ends: the server tick of its Shot; the Shooter's delay as whole ticks (0 to 255) and a fraction; one bitfield holding what the round struck (none, the Map, a Prop or a Hitbox, bits 0-1), the Body part of a Hitbox (bits 2-3) and whether entries were left out (bit 4); the point it struck and the entity ID of the Prop or body (0 otherwise); then at most 16 entries, oldest first: the tick's distance from the Shot's (0 to 65535), then the players whose bounds that tick's segment crossed, each by entity ID, position, its stance and the yaw it faced, as the Hitbox history held it |

- **A stream flows only when both sides ask for it.** Each bit of Debug
  subscribe has its own key in the `debug` section of `augustad.yaml` (ADR-0074;
  bit 0's is `debug.shot_judgements`), off by default. The server sends a stream
  to a peer only while the key is on and the peer's last Debug subscribe sets
  its bit; a bit whose key is off is ignored, with one `INFO`, and never counts
  toward a misbehaving disconnect. The client sends Debug subscribe when a Debug
  overlay that draws a stream opens or closes, so a server pays for a stream
  only while someone is looking at it.
- **A Shot judgement carries what the client cannot work out.** The client
  already flies the round itself, from its Shot and the Parameters' ballistics
  (ADR-0044), so it draws each tick's segment without being sent it. Only the
  ticks whose segment crossed some player's bounds travel, each with the pose of
  every player crossed (position, stance and facing): the client places the
  Hitboxes from that pose and the Character, which the pack hash makes the
  server's own. A round flies at most 5 seconds, so the entries stop at 16 and
  the flag says some were left out. A round that passes one or two players is
  tens of bytes, about 1 KB/s at an automatic rifle's rate, sent only to its
  shooter.

**Joining.** A connection is accepted at the transport unconditionally, because
a refusal is itself a message and needs a connection to travel on. The client's
first message is a Join request carrying its engine version and the hash of the
client pack it loaded (the 32-byte BLAKE3 hash its trailer signs, ADR-0031); the
server admits it only on an exact match of the version with its own, and of the
hash with the one its server pack carries for the client pack cooked with it;
ADR-0042 and ADR-0043 add the character and the Lobby's checks, and ADR-0043
gives the order of them all. Both packs being signed by the same key proves only
that each is genuine, not that they are the same cook: a client on another cook
could hold different collision or characters and mispredict every tick. A
Development build's engine version ends in `+dev` (ADR-0074), so a Development
client and a Shipping server, or the other way round, refuse each other as a
version mismatch: the messages of a Development build never reach a peer that
cannot decode them. After a refusal the client closes the connection.

**What a join carries.** Join accepted tells the client everything it must know
before its first tick, so nothing is learned by guessing. Who else is playing,
and where each player's body starts, is not in it: the Lobby update tells the
first and Match start the second (ADR-0043). The client starts its prediction at
the Spawn point Match start gives it, not at the origin. The **tick rate** is
the server's startup setting (ADR-0034, ADR-0039), fixed for the life of the
server process, so it is told here once and never again; the client ticks at it
and starts no tick before it has it, and drops a Join accepted whose rate is 0
(a whole number of Hz, 1 to 255, as `augustad.yaml` takes it). The
**parameters** are the server's data-driven configuration (ADR-0039), the
stamina rules, the rifle, its ammo and the starting health among them, fixed for
the run and sent so the client predicts with the server's numbers and never with
values of its own; the two cannot drift. A client drops a Join accepted whose
parameters fail the range checks of ADR-0039, as it drops any message that does
not decode.

**Failure paths.** The server drops what does not decode, is not a client
message or fails the command gate (a number beyond what a client produces), and
logs it as `dropped_malformed`; a peer's garbage never reaches the world or
another client. A peer that keeps doing so is disconnected: the server counts,
per peer, what does not decode, what is not a client message, commands outside
what a client produces and commands before joining, and once 16 fall within 5
seconds it closes the connection, an ordinary departure (the Roster changes in
the Lobby; in a Match the body leaves, and the Match goes on or ends with its
last player). An honest client sends none of these, whatever its latency and
loss. Routine rejections never count: a stale command, commands outside a Match,
a Ready for an old Roster and a refused Join. The window and threshold are
boundary constants, like the command queue's cap, neither Parameters (ADR-0039)
nor settings (ADR-0034). Every connection is accepted, since a refusal needs one
to travel on, so a peer not admitted to the Lobby within 30 seconds of
connecting is disconnected the same way, or idle connections would pile up. An
honest client asks to join as soon as it is connected, so it is admitted or
refused within a round trip; a refused one has its refusal long before the
deadline ends the connection. The deadline is a boundary constant too. No
message announces either disconnect; each is one `WARN`,
`event=misbehaving_disconnected`, with the peer, its session if it has one and
the reason (`not admitted in time` for the deadline), and the heartbeat counts
them as `misbehaving=`. A stale command is routine, since commands repeat, and
only traced. A connection that ends frees its slot at once and removes the
player at the start of the next tick; the log tells `left` (the peer closed it)
from `timeout` (the transport gave up on it) and `misbehaving` (the server
closed it). The client has no reconnecting and no connection screen: it ends the
session with one of three failures, refused (with the reason), server
unreachable (the connection ended before the server admitted it) or connection
lost (after), reports it, and exits non-zero.

**Session ID.** The server names each admitted player with a session ID it
generates itself: a counter that is never reused, deliberately unrelated to the
transport's connection handle, so identifying a player inside a message does not
depend on how the connection is represented. It identifies; it does not
authenticate. The server tells senders apart by connection, so a guessed ID
grants nothing. Not being a credential, it may be logged (`session=`), and is:
without it the log cannot tell one player's lines from another's to reconstruct
a Match. ADR-0029's rule stands for what is a credential - no session/auth token
or ticket is ever logged.

**Entity ID.** A body is named by an entity ID, not by the session of the player
who moves it: the two answer different questions (which body is this; whose
commands move it), and a body no player moves (a crate, a door) has no session
at all. The server makes one for each player's body at Match start from a
counter of its own, never reused across matches, and Match start pairs each
player's session with it; the Authoritative State then names every body by its
entity ID alone. A client finds its own body through that pairing, and
presentation draws every body by it. It is a 32-bit number, in each Match start
entry and in place of the session ID in each Authoritative State body.

## Considered Options

- **Refuse at the transport instead of with a message**: rejected — the
  transport gives the client only "connection closed", so the player could not
  be told why (US-01 asks for the reason).
- **Angles as int16 counts of 1/8192 rad**: rejected — a shot could land about 5
  cm off at 800 m, an error the network adds and no weapon has.
- **The client aiming with the rounded angle**: rejected — a scoped view would
  move in visible steps of about 0.1 mrad.
- **Leaving the angle error for the weapon's spread to hide**: rejected — spread
  is a per-weapon design choice, and the network's error would add to it on
  every weapon alike.
- **Version as a number or hash instead of the engine version string**: rejected
  for now — the string is what `augusta::EngineVersion()` already is, exact
  equality is the rule, and 32 bytes at join time cost nothing.
