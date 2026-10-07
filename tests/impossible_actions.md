# Impossible actions (US-15, NFR-05)

NFR-05 asks that 100% of out-of-bounds client actions be stopped before they
affect the Authoritative State. This catalogue is that 100%: every impossible
action a client can attempt, how the server stops it, and the test that proves
it. Each test, in [session_test.cpp](session_test.cpp), drives a real
`server::Host` over loopback only through wire messages, and asserts only on
what clients are told (Authoritative State updates, Shots, Hit confirmations,
Deaths, replies, the Roster, the connection), never on the server's counters. A
corrected entry's test also compares what the impossible action gets with what
an honest client sending the honest equivalent gets in the same Match.

## Outcomes and protections

- **Rejected**: a value or message no honest client produces. The server's
  boundary (Input Validation: `protocol::Decode`, `CommandQueue`'s `Validate`,
  and `Host`'s checks of who may send what, and when) refuses it before it is
  queued. The Authoritative State and the acknowledged sequence stay exactly as
  if it had never been sent. A peer that keeps sending rejected data that no
  honest client sends is also disconnected (`server/misbehaviour.h`).
- **Corrected**: an action the game forbids (firing with an empty magazine,
  sprinting with no stamina). SimulationWorld simulates the intent and never
  what the client claims, so the forbidden outcome cannot happen. Each rule is
  enforced once, by the mechanism that owns it (WeaponHandling, Movement, Lag
  compensation), never by a second validation layer inside SimulationWorld.

Each entry's protection is one of:

- **Structural**: no field exists that could carry the claim, so there is
  nothing to check.
- **Check**: a field exists and code at the boundary refuses its impossible
  values.
- **Rule**: a field carries an intent (fire held, reload pressed, sprint, a
  view), and the mechanism that owns the rule decides what it does. The intent
  is never the outcome.

## When a client message gains a field

Structural protections are the fragile ones. They hold only while no client
message carries an outcome. A new field in `JoinRequestWire`, `CommandsWire`,
`CommandWire` or `ReadyWire` that carries one (a position, a hit, a damage, an
ammo count, a health, a kill) turns a structural entry into nothing at all. It
needs a check at the boundary, an entry below, and a test. Any other new field
still needs a line in the next table that says what bounds it.

What bounds every field a client sends today:

| Message                | Field                                                           | What bounds it                                                                                                                         |
| ---------------------- | --------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| any                    | message type                                                    | `Decode`: unknown types refused. `Host`: only JoinRequest, Commands and Ready are handled                                              |
| `JoinRequestWire`      | `engine_version`, `client_pack`, `character`                    | `Decode`: length limits. `Match::Join`: each must be the server's (or one of its scenario's characters)                                |
| `CommandsWire`         | `commands`                                                      | `Decode`: at most `kMaxCommandsPerMessage`                                                                                             |
| `CommandsWire`         | `seen_tick`, with each command's `seen_age` and `seen_fraction` | Corrected by Lag compensation's clamps (ADR-0044)                                                                                      |
| `SequencedCommandWire` | `sequence`                                                      | `Validate`: must be newer than the last taken in                                                                                       |
| `CommandWire`          | `direction`, `yaw`, `pitch`                                     | Whole grid counts on the wire (`augusta/grid.h`), so always finite. `Validate`: within what a client can produce                       |
| `CommandWire`          | `flags`, `desired_stance`                                       | `Decode`: the four flags and the stance fill one byte, and an unknown stance is refused. Each flag is an intent SimulationWorld judges |
| `ReadyWire`            | `version`                                                       | `Match::Ready`: only the current Roster version counts                                                                                 |

## Rejected

| Impossible action                                                                                                                                              | Protection                                                      | Where                                         | Test                                                                                                                                                     |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A Command whose movement direction is longer than any input device produces                                                                                    | Check                                                           | `Validate` (`kMaxMovementMagnitude`)          | `ImpossibleCommandTest.AMovementLongerThanAnyInputDeviceProducesIsRejected`                                                                              |
| A Command whose pitch is past straight up or down                                                                                                              | Check                                                           | `Validate` (`kMaxPitch`)                      | `ImpossibleCommandTest.APitchPastStraightUpOrDownIsRejected`                                                                                             |
| A Command whose yaw is outside one turn                                                                                                                        | Check                                                           | `Validate` (`kMaxYaw`)                        | `ImpossibleCommandTest.AYawOutsideOneTurnIsRejected`                                                                                                     |
| Any of the above in a message that also holds good Commands: only the impossible ones are refused                                                              | Check                                                           | `CommandQueue::TryEnqueue`, per Command       | `ImpossibleCommandTest.ARejectedCommandLeavesTheGoodOnesOfItsMessageTakenIn`                                                                             |
| A Command whose sequence is not newer than the last taken in (a replay, or out of order)                                                                       | Check                                                           | `Validate` (`kStale`)                         | `ImpossibleCommandTest.ACommandWhoseSequenceIsNotNewerThanTheLastTakenInRepeatsNothing`                                                                  |
| Bytes that are no message                                                                                                                                      | Check                                                           | `protocol::Decode`, also fuzzed (ADR-0013)    | `ImpossibleCommandTest.BytesThatAreNoMessageChangeNothing`                                                                                               |
| A message type only the server sends (Authoritative State update, Shot, Hit confirmation, Death, Match start, Match end, Lobby, Join replies) sent by a client | Check                                                           | `Host::HandleMessage`                         | `ImpossibleCommandTest.AMessageOnlyTheServerSendsChangesNothingWhenAClientSendsIt`                                                                       |
| A Command message with more Commands than the protocol allows                                                                                                  | Check                                                           | `protocol::Decode` (`kMaxCommandsPerMessage`) | `ImpossibleCommandTest.ACommandMessageWithMoreCommandsThanTheProtocolAllowsIsRefusedWhole`                                                               |
| Commands from a peer that has not been admitted                                                                                                                | Check                                                           | `Host::HandleCommands`                        | `ImpossibleCommandTest.CommandsFromAPeerThatHasNotBeenAdmittedMoveNothing`                                                                               |
| A Ready naming a Roster version other than the current one                                                                                                     | Check                                                           | `Match::Ready`                                | `ImpossibleReadyTest.AReadyForAnyRosterButTheCurrentOneStartsNoMatch`                                                                                    |
| A second Join request from a player already admitted, as the same or another Character                                                                         | Check: it is answered with the admission the player already has | `Match::Join`                                 | `ImpossibleRejoinTest.ASecondJoinFromAnAdmittedPlayerChangesNeitherThePlayerCountNorItsCharacter`                                                        |
| A Join naming a Character the scenario lacks                                                                                                                   | Check                                                           | `Match::Join`                                 | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientThatPicksACharacterTheScenarioLacksIsRefusedForIt` |
| A Join from another engine version                                                                                                                             | Check                                                           | `Match::Join`                                 | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientWithAnotherEngineVersionIsRefusedForTheVersion`    |
| A Join with another client pack                                                                                                                                | Check                                                           | `Match::Join`                                 | `ImpossibleJoinTest.AJoinThatCanNeverPlayHereIsRefusedAndTheLobbyIsToldNothingOfIt`, `JoinTest.AClientWithAnotherClientPackIsRefusedForThePack`          |

## Structural

| Impossible action                  | Outcome                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Where                        | Test                                                                      |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------- |
| A Command carrying NaN or infinity | No such number can arrive. The wire carries every Command number as a whole grid count, so a NaN arrives as 0 and is taken in as that finite value. An infinity arrives as its grid's bound. In a yaw, a pitch or a movement on more than one axis, that bound is out of range and rejected. In one movement axis (about 2, which physics normalizes) or a Seen time fraction (255/256), it is a value a client can produce and is taken in. `Validate`'s non-finite check is a second line behind it | `augusta/grid.h`, `Validate` | `ImpossibleCommandTest.NoNumberOfACommandReachesTheServerAsNaNOrInfinity` |

## Corrected

| Impossible action                                                                 | Protection                                                                                                                                                                                         | Where                                                                 | Test                                                                                                                                               |
| --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| Teleport: a client claims to be somewhere                                         | Structural: a Command carries a movement intent, never a position, and no client message carries one. The longest movement the boundary takes is normalized, so it moves no faster than a unit one | `CommandWire`, `physics::World`                                       | `CorrectedActionTest.AClientThatClaimsAPositionEndsWhereItsSprintTakesItAsAnHonestOneDoes`                                                         |
| Speed hack: more Commands than ticks                                              | Rule: one Command a tick; past the queue's cap the oldest is dropped                                                                                                                               | `CommandQueue` (`kMaxQueuedCommands`)                                 | `CorrectedActionTest.AClientSendingMoreCommandsThanTicksMovesNoFasterThanAnHonestOne`                                                              |
| Fire faster than the rifle's rate, by pressing fire anew or sending more Commands | Rule: the rifle keeps its own cooldown, and a released trigger carries none over                                                                                                                   | WeaponHandling (`weapon::Step`)                                       | `CorrectedActionTest.PressingFireAnewTwiceATickFiresNoFasterThanTheRiflesRate`                                                                     |
| Infinite ammo: fire held with an empty magazine                                   | Rule: the magazine is the server's, and no client message carries an ammo count                                                                                                                    | WeaponHandling (`weapon::Step`)                                       | `CorrectedActionTest.HoldingFireWithAnEmptyMagazineFiresNothingMore`                                                                               |
| Reload skip: fire during a reload                                                 | Rule: no round fires on a tick of a reload, the one it starts on included                                                                                                                          | WeaponHandling (`weapon::Step`)                                       | `CorrectedActionTest.FireDuringAReloadFiresNothingUntilItCompletes`                                                                                |
| Reload spam: reload on every tick                                                 | Rule: a reload is never started over or cut short; a press once a round has left the magazine starts another                                                                                       | WeaponHandling (`weapon::Step`)                                       | `CorrectedActionTest.ReloadOnEveryTickRefillsNoSoonerThanTheReloadTime`                                                                            |
| Sprint with no stamina                                                            | Rule: the stamina is the server's, and a Command carries only sprint held; an exhausted body is held to a walk until it recovers above the threshold                                               | Movement (`physics::World`)                                           | `CorrectedActionTest.SprintingWithNoStaminaIsHeldToAWalkUntilItRecoversAboveTheThreshold`                                                          |
| A Seen time older than the Shooter's delay's cap                                  | Rule: judged at the cap                                                                                                                                                                            | Lag compensation (`kMaxShootersDelay`)                                | `CorrectedAimTest.ASeenTimeOlderThanTheShootersDelaysCapIsJudgedAtTheCap`                                                                          |
| A Seen time newer than any Authoritative State update sent                        | Rule: judged at the newest sent, the last tick's                                                                                                                                                   | Lag compensation                                                      | `CorrectedAimTest.ASeenTimeNewerThanAnyUpdateSentIsJudgedAtTheNewestSent`                                                                          |
| A Seen time fraction outside 0 to 1                                               | Structural on the wire, which carries a fraction from 0 to 255/256 (`kFractionGrid`), so one past 1 arrives as 255/256 and one below 0 as 0. Rule behind it: held within 0 to 1                    | `augusta/grid.h`, Lag compensation                                    | `CorrectedAimTest.ASeenTimeFractionOutsideZeroToOneIsHeldWithinIt`                                                                                 |
| Commands from a dead player (a spectator)                                         | Rule: a dead player's body has left the simulation, so its Commands have nothing to move, turn or fire                                                                                             | SimulationWorld's Damage                                              | `CorrectedAimTest.ADeadPlayersCommandsMoveTurnAndFireNothing`                                                                                      |
| Commands from a player in the Lobby                                               | Check: dropped, and none taken in, so its Match starts at its spawn point with a full magazine and its Commands numbered from 1                                                                    | `Host::HandleCommands` (`Match::IsPlaying`)                           | `CorrectedLobbyTest.CommandsFromAPlayerInTheLobbyAffectNothing`                                                                                    |
| Impersonation: Commands sent as another player                                    | Structural: no client message carries a Session ID, so a Session ID is no credential. The server knows whose message it is by its connection alone                                                 | `Host` (`Match::SessionOf`)                                           | `CorrectedActionTest.CommandsNumberedAsAnotherPlayersMoveAndFireOnlyTheSendersOwnBody`                                                             |
| Reported outcomes: a client claims a hit, a damage, a health, a kill or a win     | Structural: no client message carries a hit, a damage, a health or a kill. Messages that do are the server's, and refused from a client                                                            | `JoinRequestWire`, `CommandsWire`, `ReadyWire`, `Host::HandleMessage` | `CorrectedAimTest.AClientThatClaimsAHitAKillAndAWinHurtsNoOne`, `ImpossibleCommandTest.AMessageOnlyTheServerSendsChangesNothingWhenAClientSendsIt` |

## Residual risks v1 accepts

Validation proves that no client can do what the game forbids. It cannot prove
that a client plays fairly with what the game allows:

- A client may always claim the Shooter's delay's cap: any Seen time up to
  `kMaxShootersDelay` old is one an honest client on a slow link could report,
  so a round is judged against the targets as they were then (ADR-0044).
- Every client is sent every body, so a wallhack sees them all. v1 runs on a
  trusted LAN.
- Aim assistance cannot be told from good aim: an aimbot's Commands are views
  and fire held, each within what a client can produce.
