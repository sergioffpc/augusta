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

using augusta::protocol::AuthoritativeState;
using augusta::protocol::Bytes;
using augusta::protocol::Commands;
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
using augusta::protocol::Message;
using augusta::protocol::MessageType;
using augusta::protocol::ParametersUpdate;
using augusta::protocol::PlayerState;
using augusta::protocol::SequencedCommand;
using augusta::protocol::SessionId;

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
constexpr auto kParametersUpdateType = static_cast<std::uint8_t>(MessageType::kParametersUpdate);

Message RoundTrip(const Message& message) {
  const auto decoded = Decode(Encode(message));
  EXPECT_TRUE(decoded.has_value());
  return decoded.value_or(Message{});
}

TEST(ProtocolTest, JoinRequestRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{.engine_version = "0.1.0"});

  ASSERT_TRUE(std::holds_alternative<JoinRequest>(decoded));
  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "0.1.0");
}

TEST(ProtocolTest, JoinRequestWithTheLongestVersionRoundTrips) {
  const std::string longest(kMaxEngineVersionLength, 'v');

  const auto decoded = RoundTrip(JoinRequest{.engine_version = longest});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, longest);
}

TEST(ProtocolTest, JoinRequestWithAnEmptyVersionRoundTrips) {
  const auto decoded = RoundTrip(JoinRequest{});

  EXPECT_EQ(std::get<JoinRequest>(decoded).engine_version, "");
}

PlayerState PlayerAt(std::uint32_t session, float x) {
  PlayerState player{.session = static_cast<SessionId>(session)};
  player.body.position = augusta::math::Vec3(x, 1.0F, -2.5F);
  player.body.velocity = augusta::math::Vec3(0.5F, 0.0F, 3.0F);
  player.body.stance = augusta::physics::Stance::kCrouching;
  player.body.stamina = 0.75F;
  return player;
}

TEST(ProtocolTest, JoinAcceptedRoundTrips) {
  JoinAccepted sent{
      .session = static_cast<SessionId>(0xA1B2C3D4U),
      .spawn = augusta::math::Vec3(4.0F, 0.5F, -8.0F),
      .tick_rate_hz = 30.0F,
      .generation = 7,
      .parameters = {.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F}},
      .roster = {PlayerAt(1, 10.0F), PlayerAt(2, -3.0F)}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<JoinAccepted>(decoded));
  const auto& received = std::get<JoinAccepted>(decoded);
  EXPECT_EQ(received.session, sent.session);
  EXPECT_EQ(received.spawn, sent.spawn);
  EXPECT_EQ(received.tick_rate_hz, sent.tick_rate_hz);
  EXPECT_EQ(received.generation, sent.generation);
  EXPECT_EQ(received.parameters.stamina.deplete_per_second, sent.parameters.stamina.deplete_per_second);
  EXPECT_EQ(received.parameters.stamina.regen_per_second, sent.parameters.stamina.regen_per_second);
  EXPECT_EQ(received.parameters.stamina.forced_walk_below, sent.parameters.stamina.forced_walk_below);
  ASSERT_EQ(received.roster.size(), sent.roster.size());
  for (std::size_t i = 0; i < sent.roster.size(); ++i) {
    EXPECT_EQ(received.roster[i].session, sent.roster[i].session);
    EXPECT_EQ(received.roster[i].body.position, sent.roster[i].body.position);
    EXPECT_EQ(received.roster[i].body.velocity, sent.roster[i].body.velocity);
    EXPECT_EQ(received.roster[i].body.stance, sent.roster[i].body.stance);
    EXPECT_EQ(received.roster[i].body.stamina, sent.roster[i].body.stamina);
  }
}

TEST(ProtocolTest, JoinAcceptedWithAnEmptyRosterRoundTrips) {
  const auto decoded = RoundTrip(JoinAccepted{.session = static_cast<SessionId>(1)});

  EXPECT_TRUE(std::get<JoinAccepted>(decoded).roster.empty());
}

TEST(ProtocolTest, JoinAcceptedWithAFullRosterRoundTrips) {
  JoinAccepted sent;
  sent.roster.resize(kMaxPlayers);

  EXPECT_EQ(std::get<JoinAccepted>(RoundTrip(sent)).roster.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersInARosterThanAMatchHoldsIsTooLong) {
  // type, session (4), spawn (12), generation (4), parameters (16: the tick rate, then the stamina rules),
  // then the count.
  Bytes payload = BytesOf({kJoinAcceptedType});
  payload.resize(1 + 4 + 12 + 4 + 16, std::byte{0});
  payload.push_back(static_cast<std::byte>(kMaxPlayers + 1));

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, ParametersUpdateRoundTrips) {
  const ParametersUpdate sent{
      .generation = 3,
      .parameters = {.stamina = {.deplete_per_second = 0.5F, .regen_per_second = 0.25F, .forced_walk_below = 0.15F}}};

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<ParametersUpdate>(decoded));
  const auto& received = std::get<ParametersUpdate>(decoded);
  EXPECT_EQ(received.generation, sent.generation);
  EXPECT_EQ(received.parameters.stamina.deplete_per_second, sent.parameters.stamina.deplete_per_second);
  EXPECT_EQ(received.parameters.stamina.regen_per_second, sent.parameters.stamina.regen_per_second);
  EXPECT_EQ(received.parameters.stamina.forced_walk_below, sent.parameters.stamina.forced_walk_below);
}

TEST(ProtocolTest, AParametersUpdateIsTheTypeByteThenGenerationAndStaminaRules) {
  // type, generation (4), stamina rules (12), all zero but the generation. No tick rate: it is told once, in Join
  // accepted.
  Bytes expected = BytesOf({kParametersUpdateType, 0x03, 0x00, 0x00, 0x00});
  expected.resize(expected.size() + 12, std::byte{0});

  EXPECT_EQ(Encode(ParametersUpdate{.generation = 3}), expected);
}

TEST(ProtocolTest, AParametersUpdateThatEndsEarlyOrRunsOnIsRefused) {
  Bytes whole = Encode(ParametersUpdate{.generation = 1});

  Bytes truncated = whole;
  truncated.pop_back();
  EXPECT_EQ(Decode(truncated).error(), DecodeError::kTruncated);
  Bytes trailing = whole;
  trailing.push_back(std::byte{0});
  EXPECT_EQ(Decode(trailing).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, JoinRefusedRoundTripsEveryReason) {
  for (const JoinRefusal reason : {JoinRefusal::kVersionMismatch, JoinRefusal::kMatchFull}) {
    const auto decoded = RoundTrip(JoinRefused{.reason = reason});

    ASSERT_TRUE(std::holds_alternative<JoinRefused>(decoded));
    EXPECT_EQ(std::get<JoinRefused>(decoded).reason, reason);
  }
}

TEST(ProtocolTest, FieldsAreFixedWidthLittleEndian) {
  // The session, then spawn, tick rate, generation, parameters and roster count, all zero here.
  Bytes accepted = BytesOf({kJoinAcceptedType, 0x01, 0x02, 0x03, 0x04});
  accepted.resize(accepted.size() + 12 + 4 + 4 + 12 + 1, std::byte{0});
  EXPECT_EQ(Encode(JoinAccepted{.session = static_cast<SessionId>(0x04030201U)}), accepted);
  EXPECT_EQ(Encode(JoinRefused{.reason = JoinRefusal::kMatchFull}), BytesOf({kJoinRefusedType, 2}));
  EXPECT_EQ(Encode(JoinRequest{.engine_version = "ab"}), BytesOf({kJoinRequestType, 2, 'a', 'b'}));
}

TEST(ProtocolTest, AnEmptyPayloadIsEmpty) { EXPECT_EQ(Decode(Bytes{}).error(), DecodeError::kEmpty); }

TEST(ProtocolTest, AnUnknownTypeIsRejected) {
  EXPECT_EQ(Decode(BytesOf({0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({7, 0, 0, 0, 0})).error(), DecodeError::kUnknownType);
  EXPECT_EQ(Decode(BytesOf({0xFF})).error(), DecodeError::kUnknownType);
}

TEST(ProtocolTest, EveryTruncationOfEveryMessageIsTruncatedNotACrash) {
  const std::array<Message, 6> messages = {
      JoinRequest{.engine_version = "0.1.0"},
      JoinAccepted{.session = static_cast<SessionId>(7), .roster = {PlayerState{}}},
      JoinRefused{.reason = JoinRefusal::kMatchFull},
      Commands{.commands = {SequencedCommand{.sequence = 1}, {.sequence = 2}}},
      AuthoritativeState{.tick = 3, .players = {PlayerState{}, {}}},
      ParametersUpdate{.generation = 2}};
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
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 3})).error(), DecodeError::kInvalidEnum);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 0xFF})).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, BytesAfterAMessageAreTrailing) {
  EXPECT_EQ(Decode(BytesOf({kJoinRequestType, 0, 0})).error(), DecodeError::kTrailingBytes);
  Bytes accepted = Encode(JoinAccepted{});
  accepted.push_back(std::byte{0});
  EXPECT_EQ(Decode(accepted).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(BytesOf({kJoinRefusedType, 1, 1})).error(), DecodeError::kTrailingBytes);
}

// A command with every field set to something other than its default.
SequencedCommand BusyCommand(std::uint32_t sequence) {
  SequencedCommand sequenced{.sequence = sequence};
  sequenced.command.movement.direction = augusta::math::Vec3(0.5F, -0.25F, 1.0F);
  sequenced.command.movement.sprint = true;
  sequenced.command.movement.desired_stance = augusta::physics::Stance::kProne;
  sequenced.command.yaw = 3.5F;
  sequenced.command.pitch = -1.25F;
  sequenced.command.ads = true;
  sequenced.command.fire = true;
  sequenced.command.reload = true;
  return sequenced;
}

void ExpectSameCommand(const SequencedCommand& actual, const SequencedCommand& expected) {
  EXPECT_EQ(actual.sequence, expected.sequence);
  EXPECT_EQ(actual.command.movement.direction, expected.command.movement.direction);
  EXPECT_EQ(actual.command.movement.sprint, expected.command.movement.sprint);
  EXPECT_EQ(actual.command.movement.desired_stance, expected.command.movement.desired_stance);
  EXPECT_EQ(actual.command.yaw, expected.command.yaw);
  EXPECT_EQ(actual.command.pitch, expected.command.pitch);
  EXPECT_EQ(actual.command.ads, expected.command.ads);
  EXPECT_EQ(actual.command.fire, expected.command.fire);
  EXPECT_EQ(actual.command.reload, expected.command.reload);
}

TEST(ProtocolTest, CommandsRoundTripWithEveryField) {
  const Commands sent{.commands = {BusyCommand(41), BusyCommand(42), SequencedCommand{.sequence = 43}}};

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

TEST(ProtocolTest, ANonFiniteFloatSurvivesTheCodecForTheServerToJudge) {
  SequencedCommand sequenced = BusyCommand(1);
  sequenced.command.yaw = std::numeric_limits<float>::quiet_NaN();
  sequenced.command.movement.direction.x = std::numeric_limits<float>::infinity();

  const auto decoded = std::get<Commands>(RoundTrip(Commands{.commands = {sequenced}}));

  EXPECT_TRUE(std::isnan(decoded.commands[0].command.yaw));
  EXPECT_TRUE(std::isinf(decoded.commands[0].command.movement.direction.x));
}

TEST(ProtocolTest, ACommandsStanceOrFlagOutsideItsRangeIsInvalid) {
  const Bytes payload = Encode(Commands{.commands = {BusyCommand(1)}});
  // type, count, sequence (4), direction (12), then sprint and stance.
  constexpr std::size_t kSprintOffset = 2 + 4 + 12;
  constexpr std::size_t kStanceOffset = kSprintOffset + 1;

  Bytes bad_flag = payload;
  bad_flag[kSprintOffset] = static_cast<std::byte>(2);
  EXPECT_EQ(Decode(bad_flag).error(), DecodeError::kInvalidEnum);

  Bytes bad_stance = payload;
  bad_stance[kStanceOffset] = static_cast<std::byte>(3);
  EXPECT_EQ(Decode(bad_stance).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, AuthoritativeStateRoundTrips) {
  AuthoritativeState sent{.tick = 900, .acknowledged_sequence = 875};
  for (std::uint32_t i = 0; i < 3; ++i) {
    PlayerState player{.session = static_cast<SessionId>(10 + i)};
    player.body.position = augusta::math::Vec3(1.0F + static_cast<float>(i), 2.0F, -3.5F);
    player.body.velocity = augusta::math::Vec3(0.0F, -9.81F, 3.0F);
    player.body.stance = static_cast<augusta::physics::Stance>(i);
    player.body.stamina = 0.25F * static_cast<float>(i);
    sent.players.push_back(player);
  }

  const auto decoded = RoundTrip(sent);

  ASSERT_TRUE(std::holds_alternative<AuthoritativeState>(decoded));
  const auto& received = std::get<AuthoritativeState>(decoded);
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    EXPECT_EQ(received.players[i].body.position, sent.players[i].body.position);
    EXPECT_EQ(received.players[i].body.velocity, sent.players[i].body.velocity);
    EXPECT_EQ(received.players[i].body.stance, sent.players[i].body.stance);
    EXPECT_EQ(received.players[i].body.stamina, sent.players[i].body.stamina);
  }
}

TEST(ProtocolTest, AuthoritativeStateWithAFullMatchRoundTrips) {
  AuthoritativeState sent;
  sent.players.resize(kMaxPlayers);

  EXPECT_EQ(std::get<AuthoritativeState>(RoundTrip(sent)).players.size(), kMaxPlayers);
}

TEST(ProtocolTest, MorePlayersThanAMatchHoldsIsTooLong) {
  // type, tick (4), acknowledged sequence (4), then the count.
  const Bytes payload =
      BytesOf({kAuthoritativeStateType, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<std::uint8_t>(kMaxPlayers + 1)});

  EXPECT_EQ(Decode(payload).error(), DecodeError::kFieldTooLong);
}

TEST(ProtocolTest, APlayersStanceOutsideItsRangeIsInvalid) {
  Bytes payload = Encode(AuthoritativeState{.players = {PlayerState{}}});
  // type, tick, acknowledged sequence, count, session, position (12), velocity (12), then stance.
  constexpr std::size_t kStanceOffset = 1 + 4 + 4 + 1 + 4 + 12 + 12;
  payload[kStanceOffset] = static_cast<std::byte>(3);

  EXPECT_EQ(Decode(payload).error(), DecodeError::kInvalidEnum);
}

TEST(ProtocolTest, BytesAfterCommandsAndStateAreTrailing) {
  Bytes commands = Encode(Commands{});
  commands.push_back(std::byte{0});
  Bytes state = Encode(AuthoritativeState{});
  state.push_back(std::byte{0});

  EXPECT_EQ(Decode(commands).error(), DecodeError::kTrailingBytes);
  EXPECT_EQ(Decode(state).error(), DecodeError::kTrailingBytes);
}

TEST(ProtocolTest, EveryErrorAndRefusalHasADescription) {
  for (const DecodeError error : {DecodeError::kEmpty, DecodeError::kUnknownType, DecodeError::kTruncated,
                                  DecodeError::kTrailingBytes, DecodeError::kInvalidEnum, DecodeError::kFieldTooLong}) {
    EXPECT_FALSE(augusta::protocol::DescribeDecodeError(error).empty());
  }
  for (const JoinRefusal reason : {JoinRefusal::kVersionMismatch, JoinRefusal::kMatchFull}) {
    EXPECT_FALSE(augusta::protocol::DescribeJoinRefusal(reason).empty());
  }
}

}  // namespace
