# Impossible actions (US-15, NFR-05)

NFR-05 asks that 100% of out-of-bounds client actions be stopped before they
affect the Authoritative State. This catalogue is that 100%: every impossible
action a client can attempt, how the server stops it, and the test that proves
it. Each test, in [session_test.cpp](session_test.cpp), drives a real
`server::Host` over loopback only through wire messages, and asserts only on
what clients are told (Authoritative State updates, Shots, replies, the Roster,
the connection), never on the server's counters.

## Outcomes and protections

- **Rejected**: a value or message no honest client produces. The server's
  boundary (Input Validation: `protocol::Decode`, `CommandQueue`'s `Validate`,
  and `Host`'s checks of who may send what, and when) refuses it before it is
  queued. The Authoritative State and the acknowledged sequence stay exactly as
  if it had never been sent. A peer that keeps sending rejected data that no
  honest client sends is also disconnected (`server/misbehaviour.h`).
- **Corrected**: an action the game forbids (firing with an empty magazine,
  sprinting with no stamina). SimulationWorld simulates the intent and never
  what the client claims, so the forbidden outcome cannot happen.

Each entry's protection is one of:

- **Structural**: no field exists that could carry the claim, so there is
  nothing to check.
- **Check**: a field exists and code at the boundary refuses its impossible
  values.

## When a client message gains a field

Structural protections are the fragile ones. They hold only while no client
message carries an outcome. A new field in `JoinRequestWire`, `CommandsWire`,
`CommandWire` or `ReadyWire` that carries one (a position, a hit, a damage, an
ammo count, a health, a kill) turns a structural entry into nothing at all. It
needs a check at the boundary, an entry below, and a test. Any other new field
still needs a line in the next table that says what bounds it.

What bounds every field a client sends today:

| Message | Field | What bounds it |
| --- | --- | --- |
| any | message type | `Decode`: unknown types refused. `Host`: only JoinRequest, Commands and Ready are handled |
| `JoinRequestWire` | `engine_version`, `client_pack`, `character` | `Decode`: length limits. `Match::Join`: each must be the server's (or one of its scenario's characters) |
| `CommandsWire` | `commands` | `Decode`: at most `kMaxCommandsPerMessage` |
| `CommandsWire` | `view_tick`, with each command's `view_age` and `view_fraction` | Corrected by Lag compensation's clamps (ADR-0044) |
| `SequencedCommandWire` | `sequence` | `Validate`: must be newer than the last taken in |
| `CommandWire` | `direction`, `yaw`, `pitch` | Whole grid counts on the wire (`augusta/grid.h`), so always finite. `Validate`: within what a client can produce |
| `CommandWire` | `flags`, `desired_stance` | `Decode`: the four flags and the stance fill one byte, and an unknown stance is refused. Each flag is an intent SimulationWorld judges |
| `ReadyWire` | `version` | `Match::Ready`: only the current Roster version counts |

## Rejected

| Impossible action | Protection | Where | Test |
| --- | --- | --- | --- |
| A Command whose movement direction is longer than any input device produces | Check | `Validate` (`kMaxMovementMagnitude`) | `ImpossibleCommandTest.AMovementLongerThanAnyInputDeviceProducesIsRejected` |
| A Command whose pitch is past straight up or down | Check | `Validate` (`kMaxPitch`) | `ImpossibleCommandTest.APitchPastStraightUpOrDownIsRejected` |
| A Command whose yaw is outside one turn | Check | `Validate` (`kMaxYaw`) | `ImpossibleCommandTest.AYawOutsideOneTurnIsRejected` |
| Any of the above in a message that also holds good Commands: only the impossible ones are refused | Check | `CommandQueue::TryEnqueue`, per Command | `ImpossibleCommandTest.ARejectedCommandLeavesTheGoodOnesOfItsMessageTakenIn` |
| A Command whose sequence is not newer than the last taken in (a replay, or out of order) | Check | `Validate` (`kStale`) | `ImpossibleCommandTest.ACommandWhoseSequenceIsNotNewerThanTheLastTakenInRepeatsNothing` |
| Bytes that are no message | Check | `protocol::Decode`, also fuzzed (ADR-0013) | `ImpossibleCommandTest.BytesThatAreNoMessageChangeNothing` |
| A message type only the server sends (Authoritative State update, Shot, Hit confirmation, Death, Match start, Match end, Lobby, Join replies) sent by a client | Check | `Host::HandleMessage` | `ImpossibleCommandTest.AMessageOnlyTheServerSendsChangesNothingWhenAClientSendsIt` |
| A Command message with more Commands than the protocol allows | Check | `protocol::Decode` (`kMaxCommandsPerMessage`) | `ImpossibleCommandTest.ACommandMessageWithMoreCommandsThanTheProtocolAllowsIsRefusedWhole` |
| Commands from a peer that has not been admitted | Check | `Host::HandleCommands` | `ImpossibleCommandTest.CommandsFromAPeerThatHasNotBeenAdmittedMoveNothing` |
| A Ready naming a Roster version other than the current one | Check | `Match::Ready` | `ImpossibleReadyTest.AReadyForAnyRosterButTheCurrentOneStartsNoMatch` |
| A second Join request from a player already admitted, as the same or another Character | Check: it is answered with the admission the player already has | `Match::Join` | `ImpossibleRejoinTest.ASecondJoinFromAnAdmittedPlayerChangesNeitherThePlayerCountNorItsCharacter` |
| A Join naming a Character the scenario lacks | Check | `Match::Join` | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientThatPicksACharacterTheScenarioLacksIsRefusedForIt` |
| A Join from another engine version | Check | `Match::Join` | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientWithAnotherEngineVersionIsRefusedForTheVersion` |
| A Join with another client pack | Check | `Match::Join` | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientWithAnotherClientPackIsRefusedForThePack` |

## Structural

| Impossible action | Outcome | Where | Test |
| --- | --- | --- | --- |
| A Command carrying NaN or infinity | No such number can arrive. The wire carries every Command number as a whole grid count, so a NaN arrives as 0 and is taken in as that finite value. An infinity arrives as its grid's bound. In a yaw, a pitch or a movement on more than one axis, that bound is out of range and rejected. In one movement axis (about 2, which physics normalizes) or a view fraction (255/256), it is a value a client can produce and is taken in. `Validate`'s non-finite check is a second line behind it | `augusta/grid.h`, `Validate` | `ImpossibleCommandTest.NoNumberOfACommandReachesTheServerAsNaNOrInfinity` |
