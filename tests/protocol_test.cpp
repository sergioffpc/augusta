#include "augusta/protocol.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/command.h"
#include "augusta/grid.h"
#include "augusta/math.h"
#include "augusta/tick.h"

// The codec is pure: every case here is bytes in, message or error out.
namespace {

using augusta::math::SnapAngle;
using augusta::math::SnapDirection;
using augusta::math::SnapFraction;
using augusta::math::SnapPosition;
using augusta::math::SnapStamina;
using augusta::math::SnapVelocity;
using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DeathWire;
using augusta::protocol::Decode;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::HitConfirmationWire;
using augusta::protocol::JoinAcceptedWire;
using augusta::protocol::JoinRefusalWire;
using augusta::protocol::JoinRefusedWire;
using augusta::protocol::JoinRequestWire;
using augusta::protocol::kMaxCommandsPerMessage;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::kMaxPlayers;
using augusta::protocol::LobbyWire;
using augusta::protocol::MatchEndWire;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::MatchStartWire;
using augusta::protocol::MessageTypeWire;
using augusta::protocol::MessageWire;
using augusta::protocol::PackHashWire;
using augusta::protocol::ReadyWire;
using augusta::protocol::RosterEntryWire;
using augusta::protocol::SequencedCommandWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::ShotWire;
using augusta::protocol::WeaponStateWire;

BytesWire BytesOf(std::initializer_list<std::uint8_t> values) {
  BytesWire bytes;
  for (const std::uint8_t value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

constexpr auto kJoinRequestType = static_cast<std::uint8_t>(MessageTypeWire::kJoinRequest);
constexpr auto kJoinAcceptedType = static_cast<std::uint8_t>(MessageTypeWire::kJoinAccepted);
constexpr auto kJoinRefusedType = static_cast<std::uint8_t>(MessageTypeWire::kJoinRefused);
constexpr auto kCommandsType = static_cast<std::uint8_t>(MessageTypeWire::kCommands);
constexpr auto kAuthoritativeStateType = static_cast<std::uint8_t>(MessageTypeWire::kAuthoritativeState);
constexpr auto kLobbyType = static_cast<std::uint8_t>(MessageTypeWire::kLobby);
constexpr auto kReadyType = static_cast<std::uint8_t>(MessageTypeWire::kReady);
constexpr auto kMatchStartType = static_cast<std::uint8_t>(MessageTypeWire::kMatchStart);
constexpr auto kMatchEndType = static_cast<std::uint8_t>(MessageTypeWire::kMatchEnd);
constexpr auto kShotType = static_cast<std::uint8_t>(MessageTypeWire::kShot);
constexpr auto kHitConfirmationType = static_cast<std::uint8_t>(MessageTypeWire::kHitConfirmation);
constexpr auto kDeathType = static_cast<std::uint8_t>(MessageTypeWire::kDeath);
// How many bytes a tick takes on the wire: as many as tick::Tick has.
constexpr int kTickBytes = static_cast<int>(sizeof(augusta::tick::Tick));
constexpr int kSequenceBytes = static_cast<int>(sizeof(augusta::command::Sequence));

// Every refusal the protocol has.
constexpr std::array<JoinRefusalWire, 5> kEveryRefusal = {
    JoinRefusalWire::kVersionMismatch, JoinRefusalWire::kLobbyFull, JoinRefusalWire::kUnknownCharacter,
    JoinRefusalWire::kMatchInProgress, JoinRefusalWire::kPackMismatch};

// A client pack hash of 1, 2, 3 ... 32, so its bytes are told apart on the wire.
PackHashWire CountingPackHash() {
  PackHashWire hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(i + 1);
  }
  return hash;
}

// payload followed by the bytes of hash.
BytesWire WithPackHash(BytesWire payload, const PackHashWire& hash) {
  payload.insert(payload.end(), hash.begin(), hash.end());
  return payload;
}

MessageWire RoundTrip(const MessageWire& message) {
  const auto decoded = Decode(Encode(message));
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(MessageWire{});
}

TEST(ProtocolTest, JoinRequestRoundTrips) {
  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = ""});

  ASSERT_TRUE(std::holds_alternative<JoinRequestWire>(decoded));
  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, "0.1.0");
}

TEST(ProtocolTest, JoinRequestWithTheLongestVersionRoundTrips) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = longest, .character = ""});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, longest);
}

TEST(ProtocolTest, JoinRequestCarriesTheClientPackHash) {
  const auto decoded =
      RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .client_pack = CountingPackHash(), .character = ""});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).client_pack, CountingPackHash());
}

TEST(ProtocolTest, JoinRequestCarriesTheChosenCharacter) {
  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = "soldier"});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).character, "soldier");
}

TEST(ProtocolTest, JoinRequestWithTheLongestCharacterRoundTrips) {
  const std::string longest(augusta::protocol::kMaxCharacterNameLength, 'c');

  const auto decoded = RoundTrip(JoinRequestWire{.engine_version = "0.1.0", .character = longest});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).character, longest);
}

TEST(ProtocolTest, ACharacterLongerThanAllowedIsTooLong) {
  BytesWire payload = WithPackHash(BytesOf({kJoinRequestType, 0}), PackHashWire{});
  payload.push_back(static_cast<std::byte>(augusta::protocol::kMaxCharacterNameLength + 1));
  payload.resize(payload.size() + augusta::protocol::kMaxCharacterNameLength + 1, static_cast<std::byte>('c'));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, JoinRequestWithAnEmptyVersionRoundTrips) {
  const auto decoded = RoundTrip(JoinRequestWire{});

  EXPECT_EQ(std::get<JoinRequestWire>(decoded).engine_version, "");
}

// actual is what Decode gave back for expected: its numbers on their grids.
void ExpectSnappedBody(const BodyStateWire& actual, const BodyStateWire& expected) {
  EXPECT_EQ(actual.position, SnapPosition(expected.position));
  EXPECT_EQ(actual.velocity, SnapVelocity(expected.velocity));
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, SnapStamina(expected.stamina));
  EXPECT_EQ(actual.flags, expected.flags);
}

EntityStateWire BodyAt(std::uint32_t entity, float x) {
  EntityStateWire body{.entity = static_cast<EntityIdWire>(entity)};
  body.body.position = Vec3(x, 1.0F, -2.5F);
  body.body.velocity = Vec3(0.5F, 0.0F, 3.0F);
  body.body.stance = augusta::protocol::StanceWire::kCrouching;
  body.body.stamina = 0.75F;
  return body;
}

TEST(ProtocolTest, JoinAcceptedRoundTrips) {
  JoinAcceptedWire sent{
      .session = static_cast<SessionIdWire>(0xA1B2C3D4U),
      .tick_rate_hz = 30,
      .parameters = {.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F},
                     .rifle = {.rounds_per_minute = 600.0F,
                               .muzzle_velocity = 800.0F,
                               .reload_seconds = 2.5F,
                               .recoil_recovery_per_second = 0.2F,
                               .ads_recoil_scale = 0.5F,
                               .ads_field_of_view = 0.7F,
                               .recoil_pattern = {{.pitch = 0.01F, .yaw = 0.002F}, {.pitch = 0.008F, .yaw = -0.002F}},
                               .magazine_capacity = 30},
                     .ammo = {.gravity = 9.81F,
                              .max_range = 1000.0F,
                              .head_damage = 100.0F,
                              .torso_damage = 34.0F,
                              .limb_damage = 25.0F},
                     .starting_health = 100.0F,
                     .player_count = 5},
      .character = "sniper"};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<JoinAcceptedWire>(decoded));
  const auto& received = std::get<JoinAcceptedWire>(decoded);
  EXPECT_EQ(received.session, sent.session);
  EXPECT_EQ(received.tick_rate_hz, sent.tick_rate_hz);
  EXPECT_EQ(received.parameters, sent.parameters);
  EXPECT_EQ(received.character, sent.character);
}

// The Lobby, not Join accepted, says who else is there (ADR-0043).
TEST(ProtocolTest, JoinAcceptedCarriesNoRosterAndNoSpawnPoint) {
  // type, session (4), tick rate (1), parameters (63: the player count, stamina 12,
  // a rifle of 26 with no recoil kick, ammo 20, starting health 4), character (1:
  // an empty one's length).
  EXPECT_EQ(Encode(JoinAcceptedWire{}).size(), 1 + 4 + 1 + 63 + 1);
}

TEST(ProtocolTest, ACharacterInJoinAcceptedLongerThanTheLimitIsTooLong) {
  BytesWire payload = Encode(JoinAcceptedWire{});
  payload.back() = static_cast<std::byte>(augusta::protocol::kMaxCharacterNameLength + 1);
  payload.resize(payload.size() + augusta::protocol::kMaxCharacterNameLength + 1, static_cast<std::byte>('c'));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, JoinRefusedRoundTripsEveryReason) {
  for (const JoinRefusalWire reason : kEveryRefusal) {
    const auto decoded = RoundTrip(JoinRefusedWire{.reason = reason});

    ASSERT_TRUE(std::holds_alternative<JoinRefusedWire>(decoded));
    EXPECT_EQ(std::get<JoinRefusedWire>(decoded).reason, reason);
  }
}

// A full Lobby is refused with the value a full match was, under its new name.
TEST(ProtocolTest, ALobbyFullRefusalKeepsTheWireValueOfAFullMatch) {
  EXPECT_EQ(Encode(JoinRefusedWire{.reason = JoinRefusalWire::kLobbyFull}), BytesOf({kJoinRefusedType, 2}));
  EXPECT_EQ(Encode(JoinRefusedWire{.reason = JoinRefusalWire::kMatchInProgress}), BytesOf({kJoinRefusedType, 4}));
}

TEST(ProtocolTest, FieldsAreFixedWidthLittleEndian) {
  // The session, then tick rate (one byte of whole Hz), parameters and
  // character: all zero here but the player count, which leads the
  // parameters, and the character, a one-byte length and its bytes.
  BytesWire accepted = BytesOf({kJoinAcceptedType, 0x01, 0x02, 0x03, 0x04});
  accepted.resize(accepted.size() + 1, std::byte{0});
  accepted.push_back(std::byte{0x03});
  accepted.resize(accepted.size() + 62, std::byte{0});
  accepted.push_back(std::byte{1});
  accepted.push_back(static_cast<std::byte>('d'));
  EXPECT_EQ(Encode(JoinAcceptedWire{.session = static_cast<SessionIdWire>(0x04030201U),
                                    .parameters = {.stamina = {}, .player_count = 3},
                                    .character = "d"}),
            accepted);
  BytesWire request = WithPackHash(BytesOf({kJoinRequestType, 2, 'a', 'b'}), CountingPackHash());
  request.push_back(std::byte{1});
  request.push_back(static_cast<std::byte>('c'));
  EXPECT_EQ(Encode(JoinRequestWire{.engine_version = "ab", .client_pack = CountingPackHash(), .character = "c"}),
            request);
  EXPECT_EQ(Encode(LobbyWire{
                .version = 0x0A0B0C0DU,
                .roster = {RosterEntryWire{.session = static_cast<SessionIdWire>(0x01020304U), .character = "e"}}}),
            BytesOf({kLobbyType, 0x0D, 0x0C, 0x0B, 0x0A, 1, 0x04, 0x03, 0x02, 0x01, 1, 'e'}));
}

TEST(ProtocolTest, LobbyRoundTrips) {
  const LobbyWire sent{
      .version = 42,
      .roster = {RosterEntryWire{.session = static_cast<SessionIdWire>(7), .character = "soldier"},
                 RosterEntryWire{.session = static_cast<SessionIdWire>(9),
                                 .character = std::string(augusta::protocol::kMaxCharacterNameLength, 'c')}}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<LobbyWire>(decoded));
  const auto& received = std::get<LobbyWire>(decoded);
  EXPECT_EQ(received.version, sent.version);
  ASSERT_EQ(received.roster.size(), sent.roster.size());
  for (std::size_t i = 0; i < sent.roster.size(); ++i) {
    EXPECT_EQ(received.roster[i].session, sent.roster[i].session);
    EXPECT_EQ(received.roster[i].character, sent.roster[i].character);
  }
}

TEST(ProtocolTest, AnEmptyLobbyAndAFullOneRoundTrip) {
  EXPECT_TRUE(std::get<LobbyWire>(RoundTrip(LobbyWire{.version = 1, .roster = {}})).roster.empty());

  LobbyWire full{.version = 1, .roster = {}};
  full.roster.resize(kMaxPlayers);
  EXPECT_EQ(std::get<LobbyWire>(RoundTrip(full)).roster.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersInALobbyThanItHoldsIsTooLong) {
  // type, version (4), then the count.
  EXPECT_EQ(Decode(BytesOf({kLobbyType, 1, 0, 0, 0, static_cast<std::uint8_t>(kMaxPlayers + 1)})).error(),
            DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ACharacterInALobbyLongerThanTheLimitIsTooLong) {
  // type, version, count, session, then the character's length.
  EXPECT_EQ(Decode(BytesOf({kLobbyType, 1, 0, 0, 0, 1, 7, 0, 0, 0,
                            static_cast<std::uint8_t>(augusta::protocol::kMaxCharacterNameLength + 1)}))
                .error(),
            DecodeError::kFieldTooLong);
}

// A player whose body is entity 100 more than its session, so the two never pass for each other.
MatchPlayerWire MatchPlayer(std::uint32_t session, std::string character, float x) {
  return MatchPlayerWire{.spawn = Vec3(x, 0.5F, -8.0F),
                         .session = static_cast<SessionIdWire>(session),
                         .entity = static_cast<EntityIdWire>(session + 100),
                         .character = std::move(character)};
}

TEST(ProtocolTest, MatchStartRoundTripsWithEveryPlayersCharacterAndSpawnPoint) {
  const MatchStartWire sent{.players = {MatchPlayer(3, "soldier", 4.0F), MatchPlayer(4, "sniper", -12.345F)}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<MatchStartWire>(decoded));
  const auto& received = std::get<MatchStartWire>(decoded);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    EXPECT_EQ(received.players[i].entity, sent.players[i].entity);
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, SnapPosition(sent.players[i].spawn));
  }
}

TEST(ProtocolTest, AMatchStartOfAFullMatchRoundTrips) {
  MatchStartWire sent;
  sent.players.assign(kMaxPlayers, MatchPlayer(1, "soldier", 0.0F));

  EXPECT_EQ(std::get<MatchStartWire>(RoundTrip(sent)).players.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersInAMatchStartThanAMatchHoldsIsTooLong) {
  EXPECT_EQ(Decode(BytesOf({kMatchStartType, static_cast<std::uint8_t>(kMaxPlayers + 1)})).error(),
            DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ReadyRoundTripsTheVersionItWasLoadedFor) {
  const auto decoded = RoundTrip(ReadyWire{.version = 0xA1B2C3D4U});

  ASSERT_TRUE(std::holds_alternative<ReadyWire>(decoded));
  EXPECT_EQ(std::get<ReadyWire>(decoded).version, 0xA1B2C3D4U);
  EXPECT_EQ(Encode(ReadyWire{.version = 0x01020304U}), BytesOf({kReadyType, 0x04, 0x03, 0x02, 0x01}));
}

TEST(ProtocolTest, MatchEndRoundTripsItsWinnersSession) {
  const auto decoded = RoundTrip(MatchEndWire{.winner = static_cast<SessionIdWire>(0xA1B2C3D4U)});

  ASSERT_TRUE(std::holds_alternative<MatchEndWire>(decoded));
  EXPECT_EQ(std::get<MatchEndWire>(decoded).winner, static_cast<SessionIdWire>(0xA1B2C3D4U));
  EXPECT_EQ(Encode(MatchEndWire{.winner = static_cast<SessionIdWire>(0x01020304U)}),
            BytesOf({kMatchEndType, 0x04, 0x03, 0x02, 0x01}));
}

// Session IDs start at 1, so a winner of 0 is a draw (ADR-0038).
TEST(ProtocolTest, AMatchEndThatIsADrawNamesSessionZero) {
  EXPECT_EQ(Encode(MatchEndWire{}), BytesOf({kMatchEndType, 0, 0, 0, 0}));
  EXPECT_EQ(Encode(MatchEndWire{.winner = augusta::protocol::kDraw}), BytesOf({kMatchEndType, 0, 0, 0, 0}));
}

TEST(ProtocolTest, AShotRoundTripsWithItsShooterTickOriginAndDirection) {
  const ShotWire shot{.tick = 1200,
                      .origin = Vec3(12.5F, 1.75F, -40.0F),
                      .shooter = static_cast<EntityIdWire>(0xA1B2C3D4U),
                      .yaw = -1.5F,
                      .pitch = 0.25F};

  const auto decoded = RoundTrip(shot);

  ASSERT_TRUE(std::holds_alternative<ShotWire>(decoded));
  EXPECT_EQ(std::get<ShotWire>(decoded), shot);
}

// Its origin and its direction travel on the grids a body's position and a
// command's view do (ADR-0038): three bytes a number.
TEST(ProtocolTest, AShotTravelsInTwentyEightBytes) {
  const ShotWire shot{.tick = 0x0A0B0C0D0E0F1011U,
                      .origin = Vec3(1.0F, 0.0F, -1.0F / 1024.0F),
                      .shooter = static_cast<EntityIdWire>(0x01020304U),
                      .yaw = 1.0F,
                      .pitch = -1.0F / 2097152.0F};

  // The type, the shooter, the tick, the origin's x, y and z, the yaw and the pitch.
  EXPECT_EQ(Encode(shot),
            BytesOf({kShotType, 0x04, 0x03, 0x02, 0x01, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x00,
                     0x04,      0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x20, 0xFF, 0xFF, 0xFF}));
}

TEST(ProtocolTest, BytesAfterAShotAreTrailing) {
  BytesWire shot = Encode(ShotWire{});
  shot.push_back(std::byte{0});

  EXPECT_EQ(Decode(shot).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, AHitConfirmationRoundTripsWithItsTargetBodyPartAndDamage) {
  for (const BodyPartWire part : {BodyPartWire::kHead, BodyPartWire::kTorso, BodyPartWire::kLimb}) {
    const HitConfirmationWire hit{.target = static_cast<EntityIdWire>(0xA1B2C3D4U), .damage = 37.5F, .part = part};

    const auto decoded = RoundTrip(hit);

    ASSERT_TRUE(std::holds_alternative<HitConfirmationWire>(decoded));
    EXPECT_EQ(std::get<HitConfirmationWire>(decoded), hit);
  }
}

TEST(ProtocolTest, AHitConfirmationTravelsInTenBytes) {
  const HitConfirmationWire hit{
      .target = static_cast<EntityIdWire>(0x01020304U), .damage = 1.0F, .part = BodyPartWire::kLimb};

  // The type, the target, the body part and the damage's bits.
  EXPECT_EQ(Encode(hit), BytesOf({kHitConfirmationType, 0x04, 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x80, 0x3F}));
}

TEST(ProtocolTest, AHitConfirmationsBodyPartOutsideItsRangeIsInvalid) {
  // type, target, then the body part.
  constexpr std::size_t kBodyPartOffset = 1 + 4;
  for (const std::uint8_t bad : {std::uint8_t{0}, std::uint8_t{4}, std::uint8_t{255}}) {
    BytesWire payload = Encode(HitConfirmationWire{});
    payload[kBodyPartOffset] = static_cast<std::byte>(bad);

    EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum) << static_cast<int>(bad);
  }
}

TEST(ProtocolTest, BytesAfterAHitConfirmationAreTrailing) {
  BytesWire hit = Encode(HitConfirmationWire{});
  hit.push_back(std::byte{0});

  EXPECT_EQ(Decode(hit).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, ADeathRoundTripsWithItsVictimKillerBodyPartAndDirection) {
  for (const BodyPartWire part : {BodyPartWire::kHead, BodyPartWire::kTorso, BodyPartWire::kLimb}) {
    const DeathWire death{.victim = static_cast<EntityIdWire>(0xA1B2C3D4U),
                          .killer = static_cast<EntityIdWire>(7),
                          .yaw = -1.5F,
                          .pitch = 0.25F,
                          .part = part};

    const auto decoded = RoundTrip(death);

    ASSERT_TRUE(std::holds_alternative<DeathWire>(decoded));
    EXPECT_EQ(std::get<DeathWire>(decoded), death);
  }
}

// Its direction travels on the grid a Shot's does (ADR-0038): three bytes an angle.
TEST(ProtocolTest, ADeathTravelsInSixteenBytes) {
  const DeathWire death{.victim = static_cast<EntityIdWire>(0x01020304U),
                        .killer = static_cast<EntityIdWire>(0x05060708U),
                        .yaw = 1.0F,
                        .pitch = -1.0F / 2097152.0F,
                        .part = BodyPartWire::kHead};

  // The type, the victim, the killer, the body part, the yaw and the pitch.
  EXPECT_EQ(Encode(death), BytesOf({kDeathType, 0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, 0x01, 0x00, 0x00, 0x20,
                                    0xFF, 0xFF, 0xFF}));
}

TEST(ProtocolTest, ADeathsBodyPartOutsideItsRangeIsInvalid) {
  // type, victim, killer, then the body part.
  constexpr std::size_t kBodyPartOffset = 1 + 4 + 4;
  for (const std::uint8_t bad : {std::uint8_t{0}, std::uint8_t{4}, std::uint8_t{255}}) {
    BytesWire payload = Encode(DeathWire{});
    payload[kBodyPartOffset] = static_cast<std::byte>(bad);

    EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum) << static_cast<int>(bad);
  }
}

TEST(ProtocolTest, BytesAfterADeathAreTrailing) {
  BytesWire death = Encode(DeathWire{});
  death.push_back(std::byte{0});

  EXPECT_EQ(Decode(death).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, ACharacterInAMatchStartLongerThanTheLimitIsTooLong) {
  BytesWire payload = Encode(MatchStartWire{.players = {MatchPlayer(1, "", 0.0F)}});
  // type, count, session, entity, then the character's length.
  constexpr std::size_t kCharacterOffset = 1 + 1 + 4 + 4;
  payload[kCharacterOffset] = static_cast<std::byte>(augusta::protocol::kMaxCharacterNameLength + 1);

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, AnEmptyPayloadIsEmpty) { EXPECT_EQ(Decode(BytesWire{}).error(), DecodeError::kEmpty); }

TEST(ProtocolTest, AnUnknownTypeIsRejected) {
  EXPECT_EQ(Decode(BytesOf({0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({13, 0, 0, 0, 0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({0xFF})).error(), DecodeError::kUnknownType);
}

TEST(ProtocolTest, EveryTruncationOfEveryMessageIsTruncatedNotACrash) {
  const std::array<MessageWire, 12> messages = {
      JoinRequestWire{.engine_version = "0.1.0", .character = "soldier"},
      JoinAcceptedWire{.session = static_cast<SessionIdWire>(7), .character = "soldier"},
      JoinRefusedWire{.reason = JoinRefusalWire::kMatchInProgress},
      CommandsWire{.commands = {SequencedCommandWire{.sequence = 1}, {.sequence = 2}}},
      AuthoritativeStateWire{.tick = 3, .bodies = {EntityStateWire{}, {}}, .queued_commands = 2},
      LobbyWire{.version = 2, .roster = {RosterEntryWire{.character = "a"}, {.character = "b"}}},
      MatchStartWire{.players = {MatchPlayer(1, "soldier", 0.0F), MatchPlayer(2, "a", 1.0F)}},
      ReadyWire{.version = 0x01020304U},
      ShotWire{.tick = 9, .origin = Vec3(1.0F, 2.0F, 3.0F), .shooter = static_cast<EntityIdWire>(7)},
      HitConfirmationWire{.target = static_cast<EntityIdWire>(7), .damage = 20.0F, .part = BodyPartWire::kHead},
      DeathWire{.victim = static_cast<EntityIdWire>(7), .killer = static_cast<EntityIdWire>(8)},
      MatchEndWire{.winner = static_cast<SessionIdWire>(3)}};
  for (const MessageWire& message : messages) {
    const BytesWire whole = Encode(message);
    for (std::size_t length = 1; length < whole.size(); ++length) {
      const BytesWire cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
      EXPECT_EQ(Decode(cut).error(), DecodeError::kTruncated)
          << "type " << static_cast<int>(whole[0]) << " cut to " << length;
    }
  }
}

TEST(ProtocolTest, ALengthPointingPastThePayloadIsTruncatedWithoutReadingIt) {
  // Claims 32 bytes of version, supplies 2.
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 32, 'a', 'b'})).error(), DecodeError::kTruncated);
}

TEST(ProtocolTest, AVersionLongerThanAllowedIsTooLongEvenWhenAllOfItIsPresent) {
  BytesWire payload = BytesOf({kJoinRequestType, static_cast<std::uint8_t>(kMaxEngineVersionLength + 1)});
  payload.resize(payload.size() + kMaxEngineVersionLength + 1, static_cast<std::byte>('v'));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ALengthOf255IsRejectedBeforeAnythingIsAllocatedForIt) {
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 255})).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ARefusalReasonOutsideTheEnumerationIsInvalid) {
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 0})).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 6})).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 0xFF})).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, BytesAfterAMessageAreTrailing) {
  BytesWire request = WithPackHash(BytesOf({kJoinRequestType, 0}), PackHashWire{});
  request.push_back(std::byte{0});
  request.push_back(std::byte{0});
  EXPECT_EQ(Decode(request).error(), DecodeError::kTrailingBytes);
  BytesWire accepted = Encode(JoinAcceptedWire{});
  accepted.push_back(std::byte{0});
  EXPECT_EQ(Decode(accepted).error(), DecodeError::kTrailingBytes);
  BytesWire lobby = Encode(LobbyWire{});
  lobby.push_back(std::byte{0});
  EXPECT_EQ(Decode(lobby).error(), DecodeError::kTrailingBytes);
  BytesWire start = Encode(MatchStartWire{});
  start.push_back(std::byte{0});
  EXPECT_EQ(Decode(start).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kReadyType, 1, 0, 0, 0, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kMatchEndType, 1, 0, 0, 0, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 1, 1})).error(), DecodeError::kTrailingBytes);
}

// A command with every field set to something other than its default.
SequencedCommandWire BusyCommand(augusta::command::Sequence sequence) {
  SequencedCommandWire sequenced{.sequence = sequence};
  sequenced.command.direction = Vec3(0.5F, -0.25F, 1.0F);
  sequenced.command.yaw = 3.5F;
  sequenced.command.pitch = -1.25F;
  sequenced.command.flags = CommandWire::kSprint | CommandWire::kAds | CommandWire::kFire | CommandWire::kReload;
  sequenced.command.desired_stance = augusta::protocol::StanceWire::kProne;
  sequenced.command.seen_age = static_cast<std::uint8_t>(sequence);
  sequenced.command.seen_fraction = 0.3F;
  return sequenced;
}

// actual is what Decode gave back for expected: its numbers on their grids.
void ExpectSameCommand(const SequencedCommandWire& actual, const SequencedCommandWire& expected) {
  EXPECT_EQ(actual.sequence, expected.sequence);
  EXPECT_EQ(actual.command.direction, SnapDirection(expected.command.direction));
  EXPECT_EQ(actual.command.yaw, SnapAngle(expected.command.yaw));
  EXPECT_EQ(actual.command.pitch, SnapAngle(expected.command.pitch));
  EXPECT_EQ(actual.command.flags, expected.command.flags);
  EXPECT_EQ(actual.command.desired_stance, expected.command.desired_stance);
  EXPECT_EQ(actual.command.seen_age, expected.command.seen_age);
  EXPECT_EQ(actual.command.seen_fraction, SnapFraction(expected.command.seen_fraction));
}

TEST(ProtocolTest, CommandsRoundTripWithEveryField) {
  const CommandsWire sent{.commands = {BusyCommand(41), BusyCommand(42), SequencedCommandWire{.sequence = 43}},
                          .seen_tick = 123456};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<CommandsWire>(decoded));
  const CommandsWire& received = std::get<CommandsWire>(decoded);
  EXPECT_EQ(received.seen_tick, sent.seen_tick);
  ASSERT_EQ(received.commands.size(), sent.commands.size());
  for (std::size_t i = 0; i < sent.commands.size(); ++i) {
    ExpectSameCommand(received.commands[i], sent.commands[i]);
  }
}

TEST(ProtocolTest, CommandsWithNoCommandsRoundTrip) {
  const auto decoded = RoundTrip(CommandsWire{});

  EXPECT_TRUE(std::get<CommandsWire>(decoded).commands.empty());
}

TEST(ProtocolTest, CommandsCarryTheMostAMessageAllows) {
  CommandsWire sent;
  for (std::uint32_t i = 0; i < kMaxCommandsPerMessage; ++i) {
    sent.commands.push_back(BusyCommand(i + 1));
  }

  EXPECT_EQ(std::get<CommandsWire>(RoundTrip(sent)).commands.size(), kMaxCommandsPerMessage);
}

TEST(ProtocolTest, MoreCommandsThanAMessageAllowsIsTooLong) {
  EXPECT_EQ(Decode(BytesOf({kCommandsType, static_cast<std::uint8_t>(kMaxCommandsPerMessage + 1)})).error(),
            DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ANonFiniteNumberIsSentAsZeroOrItsNearestBound) {
  SequencedCommandWire sequenced = BusyCommand(1);
  sequenced.command.yaw = std::numeric_limits<float>::quiet_NaN();
  sequenced.command.direction.x = std::numeric_limits<float>::infinity();
  sequenced.command.direction.y = -std::numeric_limits<float>::infinity();

  const auto decoded = std::get<CommandsWire>(RoundTrip(CommandsWire{.commands = {sequenced}}));

  EXPECT_EQ(decoded.commands[0].command.yaw, 0.0F);
  EXPECT_EQ(decoded.commands[0].command.direction.x, SnapDirection(Vec3(1000.0F, 0.0F, 0.0F)).x);
  EXPECT_EQ(decoded.commands[0].command.direction.y, SnapDirection(Vec3(0.0F, -1000.0F, 0.0F)).y);
}

// type, count, then per command: sequence (4), direction (6), yaw (3), pitch
// (3), one byte for the flags and the stance, and one each for the Seen time's
// age and its fraction: a command is 15 bytes. Then the message's Seen tick (4).
constexpr std::size_t kCommandYawOffset = 2 + kSequenceBytes + 6;
constexpr std::size_t kCommandFlagsOffset = kCommandYawOffset + 3 + 3;
constexpr std::size_t kCommandSeenOffset = kCommandFlagsOffset + 1;
constexpr std::size_t kSeenTickOffset = kCommandSeenOffset + 2;

// The angle grid's step, 2^-21 rad.
constexpr float kAngleStep = 1.0F / 2097152.0F;

TEST(ProtocolTest, YawAndPitchTravelAsThreeBytesEachInStepsOfTwoToTheMinus21Radians) {
  SequencedCommandWire sequenced{.sequence = 1};
  sequenced.command.yaw = kAngleStep;
  sequenced.command.pitch = -kAngleStep;

  const BytesWire payload = Encode(CommandsWire{.commands = {sequenced}});

  const auto yaw_offset = static_cast<std::ptrdiff_t>(kCommandYawOffset);
  const BytesWire angles(payload.begin() + yaw_offset, payload.begin() + yaw_offset + 6);
  EXPECT_EQ(angles, BytesOf({0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF}));
}

TEST(ProtocolTest, AnAngleIsClampedWithinFourRadiansAndANaNIsZero) {
  EXPECT_EQ(SnapAngle(100.0F), 4.0F - kAngleStep);
  EXPECT_EQ(SnapAngle(-100.0F), -4.0F);
  EXPECT_EQ(SnapAngle(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

// Half a step: under 0.2 mm at 800 m, so the network adds no aim error a
// weapon's spread would notice.
TEST(ProtocolTest, AnAnglesRoundingErrorIsAtMostHalfAStep) {
  constexpr float kMaxError = kAngleStep / 2.0F;
  static_assert(kMaxError * 800.0F < 0.0002F);
  for (const float value : {0.1F, -0.1F, 1.0F / 3.0F, 1.2345678F, -2.7182817F, 3.1415927F, -3.9999F}) {
    EXPECT_LE(std::abs(SnapAngle(value) - value), kMaxError) << value;
  }
}

TEST(ProtocolTest, ACommandsFlagsAndStanceShareOneByte) {
  SequencedCommandWire sequenced{.sequence = 1};
  sequenced.command.flags = CommandWire::kSprint | CommandWire::kReload;
  sequenced.command.desired_stance = augusta::protocol::StanceWire::kProne;

  const BytesWire payload = Encode(CommandsWire{.commands = {sequenced}});

  ASSERT_EQ(payload.size(), kSeenTickOffset + kTickBytes);
  // The flags in the low four bits, the stance in the two above them.
  EXPECT_EQ(payload[kCommandFlagsOffset], std::byte{0b0010'1001});
}

// What the shooter was shown (ADR-0044): the newest tick once for the whole
// message, and each command's own as how far before it, with its fraction in
// 256ths.
TEST(ProtocolTest, ACommandsSeenTimeTravelsAsItsAgeAndItsFractionInAByteEachAndTheMessagesSeenTickAtTheEnd) {
  SequencedCommandWire sequenced{.sequence = 1};
  sequenced.command.seen_age = 3;
  sequenced.command.seen_fraction = 0.75F;

  const BytesWire payload = Encode(CommandsWire{.commands = {sequenced}, .seen_tick = 0x0102030405060708U});

  EXPECT_EQ(payload[kCommandSeenOffset], std::byte{3});
  EXPECT_EQ(payload[kCommandSeenOffset + 1], std::byte{192});
  const auto tick_offset = static_cast<std::ptrdiff_t>(kSeenTickOffset);
  EXPECT_EQ(BytesWire(payload.begin() + tick_offset, payload.end()),
            BytesOf({0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
}

TEST(ProtocolTest, ASeenTimesFractionIsHeldBelowOneAndANaNIsZero) {
  EXPECT_EQ(SnapFraction(0.5F), 0.5F);
  EXPECT_EQ(SnapFraction(1.0F), 255.0F / 256.0F);
  EXPECT_EQ(SnapFraction(7.0F), 255.0F / 256.0F);
  EXPECT_EQ(SnapFraction(-1.0F), 0.0F);
  EXPECT_EQ(SnapFraction(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

TEST(ProtocolTest, ACommandsStanceOrUnusedBitsOutsideTheirRangeAreInvalid) {
  const BytesWire payload = Encode(CommandsWire{.commands = {BusyCommand(1)}});
  for (const std::uint8_t bad : {std::uint8_t{0b0011'0000}, std::uint8_t{0b0100'0000}, std::uint8_t{0b1000'0000}}) {
    BytesWire altered = payload;
    altered[kCommandFlagsOffset] = static_cast<std::byte>(bad);

    EXPECT_EQ(Decode(altered).error(), DecodeError::kInvalidEnum) << static_cast<int>(bad);
  }
}

TEST(ProtocolTest, AuthoritativeStateRoundTrips) {
  AuthoritativeStateWire sent{.tick = 900, .bodies = {}, .acknowledged_sequence = 875};
  for (std::uint32_t i = 0; i < 3; ++i) {
    EntityStateWire body{.entity = static_cast<EntityIdWire>(10 + i)};
    body.body.position = Vec3(1.0F + static_cast<float>(i), 2.0F, -3.5F);
    body.body.velocity = Vec3(0.0F, -9.81F, 3.0F);
    body.body.stance = static_cast<augusta::protocol::StanceWire>(i);
    body.body.stamina = 0.25F * static_cast<float>(i);
    body.yaw = 1.25F - static_cast<float>(i);
    sent.bodies.push_back(body);
  }

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<AuthoritativeStateWire>(decoded));
  const auto& received = std::get<AuthoritativeStateWire>(decoded);
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    EXPECT_EQ(received.bodies[i].entity, sent.bodies[i].entity);
    ExpectSnappedBody(received.bodies[i].body, sent.bodies[i].body);
    EXPECT_EQ(received.bodies[i].yaw, sent.bodies[i].yaw);
  }
}

// Where a body faces travels on the grid a command's view does (ADR-0038),
// after the body: three bytes.
TEST(ProtocolTest, ABodysYawTravelsAsThreeBytesAfterTheBody) {
  EntityStateWire body;
  body.yaw = 1.0F;
  const BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {body}});
  // type, tick, acknowledged sequence, count, entity, the body (18), then the yaw.
  constexpr std::ptrdiff_t kYawOffset = 1 + kTickBytes + kSequenceBytes + 1 + 4 + 18;

  const BytesWire yaw(payload.begin() + kYawOffset, payload.begin() + kYawOffset + 3);
  EXPECT_EQ(yaw, BytesOf({0x00, 0x00, 0x20}));
}

TEST(ProtocolTest, AuthoritativeStateWithAFullMatchRoundTrips) {
  AuthoritativeStateWire sent;
  sent.bodies.resize(kMaxPlayers);

  EXPECT_EQ(std::get<AuthoritativeStateWire>(RoundTrip(sent)).bodies.size(), kMaxPlayers);
}

TEST(ProtocolTest, AnUpdateTellsItsRecipientHowManyOfItsCommandsTheServerHolds) {
  for (const std::uint8_t queued : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{255}}) {
    const auto received = std::get<AuthoritativeStateWire>(
        RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .queued_commands = queued}));

    EXPECT_EQ(received.queued_commands, queued);
    EXPECT_EQ(received.tick, 9U);
    EXPECT_EQ(received.bodies.size(), 1U);
  }
}

// The server's tick counts from its start and never starts over (ADR-0038), so
// one past the last 32 bits hold travels whole in every message that carries one.
TEST(ProtocolTest, ATickPastThirtyTwoBitsRoundTripsInEveryMessageThatCarriesOne) {
  constexpr augusta::tick::Tick kTick = augusta::tick::Tick{std::numeric_limits<std::uint32_t>::max()} + 1;

  EXPECT_EQ(std::get<AuthoritativeStateWire>(RoundTrip(AuthoritativeStateWire{.tick = kTick, .bodies = {}})).tick,
            kTick);
  EXPECT_EQ(std::get<ShotWire>(RoundTrip(ShotWire{.tick = kTick})).tick, kTick);
  EXPECT_EQ(std::get<CommandsWire>(RoundTrip(CommandsWire{.commands = {}, .seen_tick = kTick})).seen_tick, kTick);
}

// The recipient's rifle is what it replays its commands from, so its times
// arrive as the floats they were, off any grid, and its Recoil offset, which is
// on the angle grid, as it was.
TEST(ProtocolTest, AnUpdateTellsItsRecipientItsOwnRifleExactly) {
  const WeaponStateWire rifle{.cooldown = 0.1F - (1.0F / 60.0F),
                              .reload_remaining = 2.4833333F,
                              .recoil_pitch = 0.046875F,
                              .recoil_yaw = -0.00390625F,
                              .rounds = 27,
                              .burst_index = 3};

  const auto received = std::get<AuthoritativeStateWire>(
      RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .rifle = rifle, .queued_commands = 1}));

  EXPECT_EQ(received.rifle, rifle);
  EXPECT_EQ(received.queued_commands, 1U);
}

TEST(ProtocolTest, AnUpdateTellsItsRecipientItsOwnHealthExactly) {
  for (const float health : {100.0F, 37.25F, 0.1F, 0.0F}) {
    const auto received = std::get<AuthoritativeStateWire>(
        RoundTrip(AuthoritativeStateWire{.tick = 9, .bodies = {BodyAt(1, 1.0F)}, .health = health}));

    EXPECT_EQ(received.health, health);
  }
}

// A 32-bit float's bits, after the rifle: the last four bytes of an update.
TEST(ProtocolTest, ARecipientsHealthTravelsInFourBytesAfterItsRifle) {
  const BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {}, .health = 1.0F});

  EXPECT_EQ(BytesWire(payload.end() - 4, payload.end()), BytesOf({0x00, 0x00, 0x80, 0x3F}));
}

TEST(ProtocolTest, ARecipientsRifleTravelsInSixteenBytesAfterItsQueuedCommands) {
  const BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {},
                                                          .rifle = {.cooldown = 1.0F,
                                                                    .reload_remaining = -2.0F,
                                                                    .recoil_pitch = 1.0F / 64.0F,
                                                                    .recoil_yaw = -1.0F / 2097152.0F,
                                                                    .rounds = 30,
                                                                    .burst_index = 3}});
  // type, tick, acknowledged sequence, count, the queued commands, then the rifle.
  constexpr std::ptrdiff_t kRifleOffset = 1 + kTickBytes + kSequenceBytes + 1 + 1;

  // The rounds, each time as its IEEE-754 bits, little-endian, the burst index,
  // then the Recoil offset's pitch and yaw as counts of the angle grid's step:
  // 32768 and -1.
  EXPECT_EQ(BytesWire(payload.begin() + kRifleOffset, payload.begin() + kRifleOffset + 16),
            BytesOf({30, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0xC0, 3, 0x00, 0x80, 0x00, 0xFF, 0xFF, 0xFF}));
}

TEST(ProtocolTest, MorePlayersThanAMatchHoldsIsTooLong) {
  // type, tick (8), acknowledged sequence (8), then the count.
  const BytesWire payload = BytesOf({kAuthoritativeStateType, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     static_cast<std::uint8_t>(kMaxPlayers + 1)});

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, APlayersStanceOutsideItsRangeIsInvalid) {
  BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {EntityStateWire{}}});
  // type, tick, acknowledged sequence, count, entity, position (9), velocity (6), then stance.
  constexpr std::size_t kStanceOffset = 1 + kTickBytes + kSequenceBytes + 1 + 4 + 9 + 6;
  payload[kStanceOffset] = static_cast<std::byte>(3);

  EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum);
}

// type, tick, acknowledged sequence, count, entity, position (9), velocity (6), then the stance byte.
constexpr std::size_t kBodyStanceOffset = 1 + kTickBytes + kSequenceBytes + 1 + 4 + 9 + 6;

TEST(ProtocolTest, AnExhaustedBodyRoundTripsInEveryStance) {
  AuthoritativeStateWire sent;
  for (std::uint8_t stance = 0; stance < 3; ++stance) {
    EntityStateWire body = BodyAt(stance, 1.0F);
    body.body.stance = static_cast<augusta::protocol::StanceWire>(stance);
    body.body.flags = BodyStateWire::kExhausted;
    sent.bodies.push_back(body);
  }

  const auto received = std::get<AuthoritativeStateWire>(RoundTrip(sent));

  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    ExpectSnappedBody(received.bodies[i].body, sent.bodies[i].body);
  }
}

TEST(ProtocolTest, ABodysExhaustedFlagSharesTheStanceByte) {
  EntityStateWire body{};
  body.body.stance = augusta::protocol::StanceWire::kProne;
  body.body.flags = BodyStateWire::kExhausted;

  const BytesWire exhausted = Encode(AuthoritativeStateWire{.bodies = {body}});
  body.body.flags = 0;
  const BytesWire rested = Encode(AuthoritativeStateWire{.bodies = {body}});

  // The stance in the low two bits, the flag in the one above them, the rest 0.
  EXPECT_EQ(exhausted[kBodyStanceOffset], std::byte{0b0000'0110});
  EXPECT_EQ(rested[kBodyStanceOffset], std::byte{0b0000'0010});
  EXPECT_EQ(exhausted.size(), rested.size());
}

TEST(ProtocolTest, ABodysUnusedStanceByteBitsSetAreInvalid) {
  const BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {EntityStateWire{}}});
  for (unsigned bit = 3; bit < 8; ++bit) {
    BytesWire altered = payload;
    altered[kBodyStanceOffset] = static_cast<std::byte>(1U << bit);

    EXPECT_EQ(Decode(altered).error(), DecodeError::kInvalidEnum) << bit;
  }
}

// Every number of a body or a command travels as a whole count of its grid's
// step (ADR-0038), in the fewest bytes its range needs.
TEST(ProtocolTest, ABodyTravelsInEighteenBytesAndACommandInFifteen) {
  // type, tick, acknowledged sequence, count, entity, the body, its yaw, the
  // queued commands, then the recipient's rifle and health.
  EXPECT_EQ(Encode(AuthoritativeStateWire{.bodies = {EntityStateWire{}}}).size(),
            1 + kTickBytes + kSequenceBytes + 1 + 4 + 18 + 3 + 1 + 16 + 4);
  EXPECT_EQ(Encode(CommandsWire{.commands = {SequencedCommandWire{}}}).size(), 2 + kSequenceBytes + 15 + kTickBytes);
}

TEST(ProtocolTest, APositionTravelsAsThreeBytesPerAxisInMillimeterSteps) {
  EntityStateWire body;
  body.body.position = Vec3(1.0F, -1.0F / 1024.0F, 0.0F);
  const BytesWire payload = Encode(AuthoritativeStateWire{.bodies = {body}});
  // type, tick, acknowledged sequence, count, entity, then x, y and z.
  constexpr std::ptrdiff_t kPositionOffset = 1 + kTickBytes + kSequenceBytes + 1 + 4;

  const BytesWire position(payload.begin() + kPositionOffset, payload.begin() + kPositionOffset + 9);
  EXPECT_EQ(position, BytesOf({0x00, 0x04, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}));
}

TEST(ProtocolTest, ASnappedValueIsOnItsGridAndSnapsToItself) {
  for (const float value : {0.0F, 0.3F, -0.3F, 1.0F / 3.0F, 12.345F, -7.77F, 1000.001F}) {
    const Vec3 vector(value, -value, value * 0.5F);
    EXPECT_EQ(SnapPosition(SnapPosition(vector)), SnapPosition(vector)) << value;
    EXPECT_EQ(SnapVelocity(SnapVelocity(vector)), SnapVelocity(vector)) << value;
    EXPECT_EQ(SnapDirection(SnapDirection(vector)), SnapDirection(vector)) << value;
    EXPECT_EQ(SnapAngle(SnapAngle(value)), SnapAngle(value)) << value;
    EXPECT_EQ(SnapStamina(SnapStamina(value)), SnapStamina(value)) << value;
    EXPECT_NEAR(SnapPosition(vector).x, vector.x, 0.5F / 1024.0F) << value;
  }
}

TEST(ProtocolTest, AValueBeyondItsRangeIsSentAsTheBound) {
  EXPECT_EQ(SnapPosition(Vec3(1.0e6F, -1.0e6F, 0.0F)), Vec3(8192.0F - (1.0F / 1024.0F), -8192.0F, 0.0F));
  EXPECT_EQ(SnapVelocity(Vec3(100.0F, -100.0F, 0.0F)), Vec3(64.0F - (1.0F / 512.0F), -64.0F, 0.0F));
  EXPECT_EQ(SnapStamina(-0.5F), 0.0F);
}

TEST(ProtocolTest, DecodingAndEncodingAgainGivesTheSameBytes) {
  const AuthoritativeStateWire state{.tick = 1, .bodies = {BodyAt(1, 3.14159F)}, .acknowledged_sequence = 1};
  const BytesWire first = Encode(state);
  EXPECT_EQ(Encode(std::get<AuthoritativeStateWire>(Decode(first).value())), first);

  const BytesWire commands = Encode(CommandsWire{.commands = {BusyCommand(1)}});
  EXPECT_EQ(Encode(std::get<CommandsWire>(Decode(commands).value())), commands);
}

TEST(ProtocolTest, BytesAfterCommandsAndStateAreTrailing) {
  BytesWire commands = Encode(CommandsWire{});
  commands.push_back(std::byte{0});
  BytesWire state = Encode(AuthoritativeStateWire{});
  state.push_back(std::byte{0});

  EXPECT_EQ(Decode(commands).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(state).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, EveryErrorHasADescription) {
  for (const DecodeError error : {DecodeError::kEmpty, DecodeError::kUnknownType, DecodeError::kTruncated,
                                  DecodeError::kTrailingBytes, DecodeError::kInvalidEnum, DecodeError::kFieldTooLong}) {
    EXPECT_FALSE(augusta::protocol::DescribeDecodeError(error).empty());
  }
}

}  // namespace
