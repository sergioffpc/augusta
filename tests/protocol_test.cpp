#include "augusta/protocol.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <variant>

#include <gtest/gtest.h>

// The codec is pure: every case here is bytes in, message or error out.
namespace {

using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::Bytes;
using augusta::protocol::Commands;
using augusta::protocol::CommandWire;
using augusta::protocol::Decode;
using augusta::protocol::DecodeError;
using augusta::protocol::Encode;
using augusta::protocol::JoinAccepted;
using augusta::protocol::JoinRefusal;
using augusta::protocol::JoinRefused;
using augusta::protocol::JoinRequest;
using augusta::protocol::kMaxCommandsPerMessage;
using augusta::protocol::kMaxEngineVersionLength;
using augusta::protocol::kMaxPlayers;
using augusta::protocol::LobbyWire;
using augusta::protocol::MatchEnd;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::MatchStartWire;
using augusta::protocol::Message;
using augusta::protocol::MessageType;
using augusta::protocol::PackHash;
using augusta::protocol::PlayerStateWire;
using augusta::protocol::Ready;
using augusta::protocol::RosterEntryWire;
using augusta::protocol::SequencedCommandWire;
using augusta::protocol::SessionId;
using augusta::protocol::SnapAngle;
using augusta::protocol::SnapDirection;
using augusta::protocol::SnapPosition;
using augusta::protocol::SnapStamina;
using augusta::protocol::SnapVelocity;

Bytes BytesOf(std::initializer_list<std::uint8_t> values) {
  Bytes bytes;
  for (const std::uint8_t value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

constexpr auto kJoinRequestType = static_cast<std::uint8_t>(MessageType::kJoinRequest);
constexpr auto kJoinAcceptedType = static_cast<std::uint8_t>(MessageType::kJoinAccepted);
constexpr auto kJoinRefusedType = static_cast<std::uint8_t>(MessageType::kJoinRefused);
constexpr auto kCommandsType = static_cast<std::uint8_t>(MessageType::kCommands);
constexpr auto kAuthoritativeStateType = static_cast<std::uint8_t>(MessageType::kAuthoritativeState);
constexpr auto kLobbyType = static_cast<std::uint8_t>(MessageType::kLobby);
constexpr auto kReadyType = static_cast<std::uint8_t>(MessageType::kReady);
constexpr auto kMatchStartType = static_cast<std::uint8_t>(MessageType::kMatchStart);
constexpr auto kMatchEndType = static_cast<std::uint8_t>(MessageType::kMatchEnd);

// Every refusal the protocol has.
constexpr std::array<JoinRefusal, 5> kEveryRefusal = {JoinRefusal::kVersionMismatch, JoinRefusal::kLobbyFull,
                                                      JoinRefusal::kUnknownCharacter, JoinRefusal::kMatchInProgress,
                                                      JoinRefusal::kPackMismatch};

// A client pack hash of 1, 2, 3 ... 32, so its bytes are told apart on the wire.
PackHash CountingPackHash() {
  PackHash hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::byte>(i + 1);
  }
  return hash;
}

// payload followed by the bytes of hash.
Bytes WithPackHash(Bytes payload, const PackHash& hash) {
  payload.insert(payload.end(), hash.begin(), hash.end());
  return payload;
}

Message RoundTrip(const Message& message) {
  const auto decoded = Decode(Encode(message));
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(Message{});
}

TEST(ProtocolTest, JoinRequestRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{.engine_version = "0.1.0", .character = ""});

  ASSERT_TRUE(std::holds_alternative<JoinRequest>(decoded));
  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "0.1.0");
}

TEST(ProtocolTest, JoinRequestWithTheLongestVersionRoundTrips) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  const auto decoded = RoundTrip(JoinRequest{.engine_version = longest, .character = ""});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, longest);
}

TEST(ProtocolTest, JoinRequestCarriesTheClientPackHash) {
  const auto decoded =
      RoundTrip(JoinRequest{.engine_version = "0.1.0", .client_pack = CountingPackHash(), .character = ""});

  EXPECT_EQ(std::get<JoinRequest>(decoded).client_pack, CountingPackHash());
}

TEST(ProtocolTest, JoinRequestCarriesTheChosenCharacter) {
  const auto decoded = RoundTrip(JoinRequest{.engine_version = "0.1.0", .character = "characters/player"});

  EXPECT_EQ(std::get<JoinRequest>(decoded).character, "characters/player");
}

TEST(ProtocolTest, JoinRequestWithTheLongestCharacterRoundTrips) {
  const std::string longest(augusta::protocol::kMaxCharacterPathLength, 'c');

  const auto decoded = RoundTrip(JoinRequest{.engine_version = "0.1.0", .character = longest});

  EXPECT_EQ(std::get<JoinRequest>(decoded).character, longest);
}

TEST(ProtocolTest, ACharacterLongerThanAllowedIsTooLong) {
  Bytes payload = WithPackHash(BytesOf({kJoinRequestType, 0}), PackHash{});
  payload.push_back(static_cast<std::byte>(augusta::protocol::kMaxCharacterPathLength + 1));
  payload.resize(payload.size() + augusta::protocol::kMaxCharacterPathLength + 1, static_cast<std::byte>('c'));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, JoinRequestWithAnEmptyVersionRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "");
}

// actual is what Decode gave back for expected: its numbers on their grids.
void ExpectSnappedBody(const BodyStateWire& actual, const BodyStateWire& expected) {
  EXPECT_EQ(actual.position, SnapPosition(expected.position));
  EXPECT_EQ(actual.velocity, SnapVelocity(expected.velocity));
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, SnapStamina(expected.stamina));
}

PlayerStateWire PlayerAt(std::uint32_t session, float x) {
  PlayerStateWire player{.session = static_cast<SessionId>(session)};
  player.body.position = Vec3(x, 1.0F, -2.5F);
  player.body.velocity = Vec3(0.5F, 0.0F, 3.0F);
  player.body.stance = augusta::protocol::StanceWire::kCrouching;
  player.body.stamina = 0.75F;
  return player;
}

TEST(ProtocolTest, JoinAcceptedRoundTrips) {
  JoinAccepted sent{
      .session = static_cast<SessionId>(0xA1B2C3D4U),
      .tick_rate_hz = 30.0F,
      .parameters = {.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F},
                     .player_count = 5},
      .character = 3};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<JoinAccepted>(decoded));
  const auto& received = std::get<JoinAccepted>(decoded);
  EXPECT_EQ(received.session, sent.session);
  EXPECT_EQ(received.tick_rate_hz, sent.tick_rate_hz);
  EXPECT_EQ(received.parameters.player_count, sent.parameters.player_count);
  EXPECT_EQ(received.parameters.stamina.deplete_per_second, sent.parameters.stamina.deplete_per_second);
  EXPECT_EQ(received.parameters.stamina.regen_per_second, sent.parameters.stamina.regen_per_second);
  EXPECT_EQ(received.parameters.stamina.forced_walk_below, sent.parameters.stamina.forced_walk_below);
  EXPECT_EQ(received.character, sent.character);
}

// The Lobby, not Join accepted, says who else is there (ADR-0043).
TEST(ProtocolTest, JoinAcceptedCarriesNoRosterAndNoSpawnPoint) {
  // type, session (4), tick rate (4), parameters (13), character (1).
  EXPECT_EQ(Encode(JoinAccepted{}).size(), 1 + 4 + 4 + 13 + 1);
}

TEST(ProtocolTest, CharacterIndexZeroInJoinAcceptedIsInvalid) {
  Bytes payload = Encode(JoinAccepted{.character = 1});
  payload.back() = std::byte{0};

  EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, JoinRefusedRoundTripsEveryReason) {
  for (const JoinRefusal reason : kEveryRefusal) {
    const auto decoded = RoundTrip(JoinRefused{.reason = reason});

    ASSERT_TRUE(std::holds_alternative<JoinRefused>(decoded));
    EXPECT_EQ(std::get<JoinRefused>(decoded).reason, reason);
  }
}

// A full Lobby is refused with the value a full match was, under its new name.
TEST(ProtocolTest, ALobbyFullRefusalKeepsTheWireValueOfAFullMatch) {
  EXPECT_EQ(Encode(JoinRefused{.reason = JoinRefusal::kLobbyFull}), BytesOf({kJoinRefusedType, 2}));
  EXPECT_EQ(Encode(JoinRefused{.reason = JoinRefusal::kMatchInProgress}), BytesOf({kJoinRefusedType, 4}));
}

TEST(ProtocolTest, FieldsAreFixedWidthLittleEndian) {
  // The session, then tick rate, parameters and character: all zero here but
  // the player count, which leads the parameters, and the character.
  Bytes accepted = BytesOf({kJoinAcceptedType, 0x01, 0x02, 0x03, 0x04});
  accepted.resize(accepted.size() + 4, std::byte{0});
  accepted.push_back(std::byte{0x03});
  accepted.resize(accepted.size() + 12, std::byte{0});
  accepted.push_back(std::byte{0x02});
  EXPECT_EQ(Encode(JoinAccepted{.session = static_cast<SessionId>(0x04030201U),
                                .parameters = {.stamina = {}, .player_count = 3},
                                .character = 2}),
            accepted);
  Bytes request = WithPackHash(BytesOf({kJoinRequestType, 2, 'a', 'b'}), CountingPackHash());
  request.push_back(std::byte{1});
  request.push_back(static_cast<std::byte>('c'));
  EXPECT_EQ(Encode(JoinRequest{.engine_version = "ab", .client_pack = CountingPackHash(), .character = "c"}), request);
  EXPECT_EQ(
      Encode(LobbyWire{.version = 0x0A0B0C0DU,
                       .roster = {RosterEntryWire{.session = static_cast<SessionId>(0x01020304U), .character = 5}}}),
      BytesOf({kLobbyType, 0x0D, 0x0C, 0x0B, 0x0A, 1, 0x04, 0x03, 0x02, 0x01, 5}));
}

TEST(ProtocolTest, LobbyRoundTrips) {
  const LobbyWire sent{.version = 42,
                       .roster = {RosterEntryWire{.session = static_cast<SessionId>(7), .character = 1},
                                  RosterEntryWire{.session = static_cast<SessionId>(9), .character = 255}}};

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

TEST(ProtocolTest, CharacterIndexZeroInALobbyIsInvalid) {
  // type, version, count, session, then the character.
  EXPECT_EQ(Decode(BytesOf({kLobbyType, 1, 0, 0, 0, 1, 7, 0, 0, 0, 0})).error(), DecodeError::kInvalidEnum);
}

MatchPlayerWire MatchPlayer(std::uint32_t session, std::uint8_t character, float x) {
  return MatchPlayerWire{
      .spawn = Vec3(x, 0.5F, -8.0F), .session = static_cast<SessionId>(session), .character = character};
}

TEST(ProtocolTest, MatchStartRoundTripsWithEveryPlayersCharacterAndSpawnPoint) {
  const MatchStartWire sent{.players = {MatchPlayer(3, 1, 4.0F), MatchPlayer(4, 2, -12.345F)}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<MatchStartWire>(decoded));
  const auto& received = std::get<MatchStartWire>(decoded);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, SnapPosition(sent.players[i].spawn));
  }
}

TEST(ProtocolTest, AMatchStartOfAFullMatchRoundTrips) {
  MatchStartWire sent;
  sent.players.assign(kMaxPlayers, MatchPlayer(1, 1, 0.0F));

  EXPECT_EQ(std::get<MatchStartWire>(RoundTrip(sent)).players.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersInAMatchStartThanAMatchHoldsIsTooLong) {
  EXPECT_EQ(Decode(BytesOf({kMatchStartType, static_cast<std::uint8_t>(kMaxPlayers + 1)})).error(),
            DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ReadyRoundTripsTheVersionItWasLoadedFor) {
  const auto decoded = RoundTrip(Ready{.version = 0xA1B2C3D4U});

  ASSERT_TRUE(std::holds_alternative<Ready>(decoded));
  EXPECT_EQ(std::get<Ready>(decoded).version, 0xA1B2C3D4U);
  EXPECT_EQ(Encode(Ready{.version = 0x01020304U}), BytesOf({kReadyType, 0x04, 0x03, 0x02, 0x01}));
}

TEST(ProtocolTest, MatchEndIsItsTypeAlone) {
  EXPECT_EQ(Encode(MatchEnd{}), BytesOf({kMatchEndType}));
  EXPECT_TRUE(std::holds_alternative<MatchEnd>(RoundTrip(MatchEnd{})));
}

TEST(ProtocolTest, CharacterIndexZeroInAMatchStartIsInvalid) {
  Bytes payload = Encode(MatchStartWire{.players = {MatchPlayer(1, 1, 0.0F)}});
  // type, count, session, then the character.
  constexpr std::size_t kCharacterOffset = 1 + 1 + 4;
  payload[kCharacterOffset] = std::byte{0};

  EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, AnEmptyPayloadIsEmpty) { EXPECT_EQ(Decode(Bytes{}).error(), DecodeError::kEmpty); }

TEST(ProtocolTest, AnUnknownTypeIsRejected) {
  EXPECT_EQ(Decode(BytesOf({0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({10, 0, 0, 0, 0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({0xFF})).error(), DecodeError::kUnknownType);
}

TEST(ProtocolTest, EveryTruncationOfEveryMessageIsTruncatedNotACrash) {
  const std::array<Message, 8> messages = {
      JoinRequest{.engine_version = "0.1.0", .character = "characters/player"},
      JoinAccepted{.session = static_cast<SessionId>(7), .character = 1},
      JoinRefused{.reason = JoinRefusal::kMatchInProgress},
      Commands{.commands = {SequencedCommandWire{.sequence = 1}, {.sequence = 2}}},
      AuthoritativeStateWire{.tick = 3, .players = {PlayerStateWire{}, {}}},
      LobbyWire{.version = 2, .roster = {RosterEntryWire{}, {}}},
      MatchStartWire{.players = {MatchPlayer(1, 1, 0.0F), MatchPlayer(2, 2, 1.0F)}},
      Ready{.version = 0x01020304U}};
  for (const Message& message : messages) {
    const Bytes whole = Encode(message);
    for (std::size_t length = 1; length < whole.size(); ++length) {
      const Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
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
  Bytes payload = BytesOf({kJoinRequestType, static_cast<std::uint8_t>(kMaxEngineVersionLength + 1)});
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
  Bytes request = WithPackHash(BytesOf({kJoinRequestType, 0}), PackHash{});
  request.push_back(std::byte{0});
  request.push_back(std::byte{0});
  EXPECT_EQ(Decode(request).error(), DecodeError::kTrailingBytes);
  Bytes accepted = Encode(JoinAccepted{});
  accepted.push_back(std::byte{0});
  EXPECT_EQ(Decode(accepted).error(), DecodeError::kTrailingBytes);
  Bytes lobby = Encode(LobbyWire{});
  lobby.push_back(std::byte{0});
  EXPECT_EQ(Decode(lobby).error(), DecodeError::kTrailingBytes);
  Bytes start = Encode(MatchStartWire{});
  start.push_back(std::byte{0});
  EXPECT_EQ(Decode(start).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kReadyType, 1, 0, 0, 0, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kMatchEndType, 0})).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 1, 1})).error(), DecodeError::kTrailingBytes);
}

// A command with every field set to something other than its default.
SequencedCommandWire BusyCommand(std::uint32_t sequence) {
  SequencedCommandWire sequenced{.sequence = sequence};
  sequenced.command.direction = Vec3(0.5F, -0.25F, 1.0F);
  sequenced.command.yaw = 3.5F;
  sequenced.command.pitch = -1.25F;
  sequenced.command.flags = CommandWire::kSprint | CommandWire::kAds | CommandWire::kFire | CommandWire::kReload;
  sequenced.command.desired_stance = augusta::protocol::StanceWire::kProne;
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
}

TEST(ProtocolTest, CommandsRoundTripWithEveryField) {
  const Commands sent{.commands = {BusyCommand(41), BusyCommand(42), SequencedCommandWire{.sequence = 43}}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<Commands>(decoded));
  const Commands& received = std::get<Commands>(decoded);
  ASSERT_EQ(received.commands.size(), sent.commands.size());
  for (std::size_t i = 0; i < sent.commands.size(); ++i) {
    ExpectSameCommand(received.commands[i], sent.commands[i]);
  }
}

TEST(ProtocolTest, CommandsWithNoCommandsRoundTrip) {
  const auto decoded = RoundTrip(Commands{});

  EXPECT_TRUE(std::get<Commands>(decoded).commands.empty());
}

TEST(ProtocolTest, CommandsCarryTheMostAMessageAllows) {
  Commands sent;
  for (std::uint32_t i = 0; i < kMaxCommandsPerMessage; ++i) {
    sent.commands.push_back(BusyCommand(i + 1));
  }

  EXPECT_EQ(std::get<Commands>(RoundTrip(sent)).commands.size(), kMaxCommandsPerMessage);
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

  const auto decoded = std::get<Commands>(RoundTrip(Commands{.commands = {sequenced}}));

  EXPECT_EQ(decoded.commands[0].command.yaw, 0.0F);
  EXPECT_EQ(decoded.commands[0].command.direction.x, SnapDirection(Vec3(1000.0F, 0.0F, 0.0F)).x);
  EXPECT_EQ(decoded.commands[0].command.direction.y, SnapDirection(Vec3(0.0F, -1000.0F, 0.0F)).y);
}

// type, count, then per command: sequence (4), direction (6), yaw (3), pitch
// (3) and one byte for the flags and the stance: a command is 13 bytes.
constexpr std::size_t kCommandYawOffset = 2 + 4 + 6;
constexpr std::size_t kCommandFlagsOffset = kCommandYawOffset + 3 + 3;

// The angle grid's step, 2^-21 rad.
constexpr float kAngleStep = 1.0F / 2097152.0F;

TEST(ProtocolTest, YawAndPitchTravelAsThreeBytesEachInStepsOfTwoToTheMinus21Radians) {
  SequencedCommandWire sequenced{.sequence = 1};
  sequenced.command.yaw = kAngleStep;
  sequenced.command.pitch = -kAngleStep;

  const Bytes payload = Encode(Commands{.commands = {sequenced}});

  const auto yaw_offset = static_cast<std::ptrdiff_t>(kCommandYawOffset);
  const Bytes angles(payload.begin() + yaw_offset, payload.begin() + yaw_offset + 6);
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

TEST(ProtocolTest, ACommandsFlagsAndStanceShareItsLastByte) {
  SequencedCommandWire sequenced{.sequence = 1};
  sequenced.command.flags = CommandWire::kSprint | CommandWire::kReload;
  sequenced.command.desired_stance = augusta::protocol::StanceWire::kProne;

  const Bytes payload = Encode(Commands{.commands = {sequenced}});

  ASSERT_EQ(payload.size(), kCommandFlagsOffset + 1);
  // The flags in the low four bits, the stance in the two above them.
  EXPECT_EQ(payload[kCommandFlagsOffset], std::byte{0b0010'1001});
}

TEST(ProtocolTest, ACommandsStanceOrUnusedBitsOutsideTheirRangeAreInvalid) {
  const Bytes payload = Encode(Commands{.commands = {BusyCommand(1)}});
  for (const std::uint8_t bad : {std::uint8_t{0b0011'0000}, std::uint8_t{0b0100'0000}, std::uint8_t{0b1000'0000}}) {
    Bytes altered = payload;
    altered[kCommandFlagsOffset] = static_cast<std::byte>(bad);

    EXPECT_EQ(Decode(altered).error(), DecodeError::kInvalidEnum) << static_cast<int>(bad);
  }
}

TEST(ProtocolTest, AuthoritativeStateRoundTrips) {
  AuthoritativeStateWire sent{.tick = 900, .acknowledged_sequence = 875, .players = {}};
  for (std::uint32_t i = 0; i < 3; ++i) {
    PlayerStateWire player{.session = static_cast<SessionId>(10 + i)};
    player.body.position = Vec3(1.0F + static_cast<float>(i), 2.0F, -3.5F);
    player.body.velocity = Vec3(0.0F, -9.81F, 3.0F);
    player.body.stance = static_cast<augusta::protocol::StanceWire>(i);
    player.body.stamina = 0.25F * static_cast<float>(i);
    sent.players.push_back(player);
  }

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<AuthoritativeStateWire>(decoded));
  const auto& received = std::get<AuthoritativeStateWire>(decoded);
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    ExpectSnappedBody(received.players[i].body, sent.players[i].body);
  }
}

TEST(ProtocolTest, AuthoritativeStateWithAFullMatchRoundTrips) {
  AuthoritativeStateWire sent;
  sent.players.resize(kMaxPlayers);

  EXPECT_EQ(std::get<AuthoritativeStateWire>(RoundTrip(sent)).players.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersThanAMatchHoldsIsTooLong) {
  // type, tick (4), acknowledged sequence (4), then the count.
  const Bytes payload =
      BytesOf({kAuthoritativeStateType, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<std::uint8_t>(kMaxPlayers + 1)});

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, APlayersStanceOutsideItsRangeIsInvalid) {
  Bytes payload = Encode(AuthoritativeStateWire{.players = {PlayerStateWire{}}});
  // type, tick, acknowledged sequence, count, session, position (9), velocity (6), then stance.
  constexpr std::size_t kStanceOffset = 1 + 4 + 4 + 1 + 4 + 9 + 6;
  payload[kStanceOffset] = static_cast<std::byte>(3);

  EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum);
}

// Every number of a body or a command travels as a whole count of its grid's
// step (ADR-0038), in the fewest bytes its range needs.
TEST(ProtocolTest, ABodyTravelsInEighteenBytesAndACommandInThirteen) {
  EXPECT_EQ(Encode(AuthoritativeStateWire{.players = {PlayerStateWire{}}}).size(), 1 + 4 + 4 + 1 + 4 + 18);
  EXPECT_EQ(Encode(Commands{.commands = {SequencedCommandWire{}}}).size(), 2 + 4 + 13);
}

TEST(ProtocolTest, APositionTravelsAsThreeBytesPerAxisInMillimeterSteps) {
  PlayerStateWire player;
  player.body.position = Vec3(1.0F, -1.0F / 1024.0F, 0.0F);
  const Bytes payload = Encode(AuthoritativeStateWire{.players = {player}});
  // type, tick, acknowledged sequence, count, session, then x, y and z.
  constexpr std::ptrdiff_t kPositionOffset = 1 + 4 + 4 + 1 + 4;

  const Bytes position(payload.begin() + kPositionOffset, payload.begin() + kPositionOffset + 9);
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
  const AuthoritativeStateWire state{.tick = 1, .acknowledged_sequence = 1, .players = {PlayerAt(1, 3.14159F)}};
  const Bytes first = Encode(state);
  EXPECT_EQ(Encode(std::get<AuthoritativeStateWire>(Decode(first).value())), first);

  const Bytes commands = Encode(Commands{.commands = {BusyCommand(1)}});
  EXPECT_EQ(Encode(std::get<Commands>(Decode(commands).value())), commands);
}

TEST(ProtocolTest, BytesAfterCommandsAndStateAreTrailing) {
  Bytes commands = Encode(Commands{});
  commands.push_back(std::byte{0});
  Bytes state = Encode(AuthoritativeStateWire{});
  state.push_back(std::byte{0});

  EXPECT_EQ(Decode(commands).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(state).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, EveryErrorAndRefusalHasADescription) {
  for (const DecodeError error : {DecodeError::kEmpty, DecodeError::kUnknownType, DecodeError::kTruncated,
                                  DecodeError::kTrailingBytes, DecodeError::kInvalidEnum, DecodeError::kFieldTooLong}) {
    EXPECT_FALSE(augusta::protocol::DescribeDecodeError(error).empty());
  }
  for (const JoinRefusal reason : kEveryRefusal) {
    EXPECT_FALSE(augusta::protocol::DescribeJoinRefusal(reason).empty());
  }
}

}  // namespace
