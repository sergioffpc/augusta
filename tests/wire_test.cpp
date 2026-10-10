#include "wire.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/grid.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "command_queue.h"
#include "match.h"

// Each peer converts between the engine's types and the protocol's plain ones
// at its edge (ADR-0038) and nowhere else: the server in server/wire.h, the
// client in harness_wire.h. What one side sends must reach the other's engine
// types unchanged, bytes and all.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::server::EntityId;
using augusta::server::SessionId;

// message as the other peer decodes it.
template <typename MessageWire>
MessageWire ThroughTheWire(const MessageWire& message) {
  return std::get<MessageWire>(augusta::protocol::Decode(augusta::protocol::Encode(message).value()).value());
}

// What the server sends recipient of updates, as the client takes it in.
augusta::harness::AuthoritativeState ReceivedBy(const augusta::replication::Updates& updates,
                                                const augusta::replication::RecipientUpdate& recipient) {
  augusta::protocol::AuthoritativeStateWire sent = augusta::server::ToWire(updates);
  augusta::server::Address(sent, recipient);
  return augusta::harness::FromWire(ThroughTheWire(sent));
}

// The number a Session ID carries, on either side: each peer has its own type for it.
template <typename Id>
std::uint32_t Number(Id id) {
  return static_cast<std::uint32_t>(id);
}

BodyState Body(float x, Stance stance) {
  BodyState body;
  body.position = Vec3(x, 1.5F, -2.0F);
  body.velocity = Vec3(0.25F, -9.5F, 3.0F);
  body.stance = stance;
  body.stamina = 0.4F;
  return body;
}

// actual is expected as it arrives: its numbers on the grids they travel on.
void ExpectSameBody(const BodyState& actual, const BodyState& expected) {
  EXPECT_EQ(actual.position, augusta::math::SnapPosition(expected.position));
  EXPECT_EQ(actual.velocity, augusta::math::SnapVelocity(expected.velocity));
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, augusta::math::SnapStamina(expected.stamina));
  EXPECT_EQ(actual.exhausted, expected.exhausted);
}

TEST(WireTest, ACommandTheClientSendsReachesTheServerUnchanged) {
  for (const Stance stance : {Stance::kStanding, Stance::kCrouching, Stance::kProne}) {
    augusta::command::Command sent;
    sent.movement.direction = Vec3(0.5F, 0.0F, -1.0F);
    sent.movement.sprint = true;
    sent.movement.desired_stance = stance;
    sent.yaw = 1.25F;
    sent.pitch = -0.5F;
    sent.ads = true;
    sent.fire = true;
    sent.reload = true;
    sent.seen_tick = 1200;
    sent.seen_fraction = 0.75F;

    const std::array<augusta::harness::SequencedCommand, 1> commands = {{{.sequence = 9, .command = sent}}};
    const augusta::server::SequencedCommand received =
        augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(commands))).at(0);

    EXPECT_EQ(received.sequence, 9U);
    EXPECT_EQ(received.command.movement.direction, sent.movement.direction);
    EXPECT_EQ(received.command.movement.sprint, sent.movement.sprint);
    EXPECT_EQ(received.command.movement.desired_stance, stance);
    EXPECT_EQ(received.command.yaw, sent.yaw);
    EXPECT_EQ(received.command.pitch, sent.pitch);
    EXPECT_EQ(received.command.ads, sent.ads);
    EXPECT_EQ(received.command.fire, sent.fire);
    EXPECT_EQ(received.command.reload, sent.reload);
    EXPECT_EQ(received.command.seen_tick, sent.seen_tick);
    EXPECT_EQ(received.command.seen_fraction, sent.seen_fraction);
  }
}

// A message's commands were sampled a tick apart, each with its own Seen time:
// every one reaches the server with the tick it named.
TEST(WireTest, EachCommandOfAMessageReachesTheServerWithTheSeenTimeItWasSampledOn) {
  std::vector<augusta::harness::SequencedCommand> sent(4);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::command::Sequence>(40 + i);
    sent[i].command.seen_tick = 70000 + (2 * i);
    sent[i].command.seen_fraction = 0.25F * static_cast<float>(i);
  }

  const std::vector<augusta::server::SequencedCommand> received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].command.seen_tick, sent[i].command.seen_tick) << i;
    EXPECT_EQ(received[i].command.seen_fraction, sent[i].command.seen_fraction) << i;
  }
}

// The server's ticks never start over (ADR-0038): commands sampled either side
// of the last tick 32 bits hold reach the server with the Seen times they named.
TEST(WireTest, SeenTimesEitherSideOfThirtyTwoBitsReachTheServerAsTheyWereSampled) {
  constexpr augusta::tick::Tick kLastOf32Bits = std::numeric_limits<std::uint32_t>::max();
  std::vector<augusta::harness::SequencedCommand> sent(4);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::command::Sequence>(1 + i);
    sent[i].command.seen_tick = kLastOf32Bits - 1 + i;
  }

  const std::vector<augusta::server::SequencedCommand> received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].command.seen_tick, sent[i].command.seen_tick) << i;
  }
}

// A command's Seen time travels as how far before the message's newest it is, in a
// byte: one further back arrives as far back as a byte tells, which is already
// beyond what the server judges a shot against.
TEST(WireTest, ASeenTimeTooFarBeforeTheMessagesNewestReachesTheServerAsTheOldestAByteTells) {
  std::vector<augusta::harness::SequencedCommand> sent(2);
  sent[0].command.seen_tick = 100;
  sent[1].command.seen_tick = 1000;

  const std::vector<augusta::server::SequencedCommand> received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  EXPECT_EQ(received.at(0).command.seen_tick, 1000U - 255U);
  EXPECT_EQ(received.at(1).command.seen_tick, 1000U);
}

// An age the message's Seen tick cannot go back by names the first tick there is.
TEST(WireTest, ASeenAgeBeyondTheMessagesSeenTickIsTheFirstTick) {
  augusta::protocol::CommandsWire message{.commands = {{.sequence = 1}}, .seen_tick = 3};
  message.commands[0].command.seen_age = 10;

  EXPECT_EQ(augusta::server::FromWire(ThroughTheWire(message)).at(0).command.seen_tick, 0U);
}

TEST(WireTest, EachFlagOfACommandReachesTheServerAsItselfAlone) {
  for (int flag = 0; flag < 4; ++flag) {
    augusta::command::Command sent;
    sent.movement.sprint = flag == 0;
    sent.ads = flag == 1;
    sent.fire = flag == 2;
    sent.reload = flag == 3;

    const std::array<augusta::harness::SequencedCommand, 1> commands = {{{.sequence = 1, .command = sent}}};
    const augusta::command::Command received =
        augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(commands))).at(0).command;

    EXPECT_EQ(received.movement.sprint, sent.movement.sprint) << flag;
    EXPECT_EQ(received.ads, sent.ads) << flag;
    EXPECT_EQ(received.fire, sent.fire) << flag;
    EXPECT_EQ(received.reload, sent.reload) << flag;
  }
}

TEST(WireTest, AnExhaustedBodyReachesTheClientStillExhausted) {
  BodyState exhausted = Body(1.0F, Stance::kStanding);
  exhausted.stamina = 0.1F;
  exhausted.exhausted = true;
  const augusta::replication::Updates sent{
      .tick = 7,
      .bodies = {{.entity = augusta::simulation::EntityId{1}, .body = exhausted},
                 {.entity = augusta::simulation::EntityId{2}, .body = Body(2.0F, Stance::kProne)}},
      .recipients = {},
  };

  const augusta::harness::AuthoritativeState received =
      ReceivedBy(sent, {.entity = augusta::simulation::EntityId{1}, .acknowledged_sequence = 3});

  ASSERT_EQ(received.bodies.size(), 2U);
  EXPECT_TRUE(received.bodies[0].body.exhausted);
  EXPECT_FALSE(received.bodies[1].body.exhausted);
}

TEST(WireTest, AnAuthoritativeStateTheServerSendsReachesTheClientUnchanged) {
  const augusta::replication::Updates sent{
      .tick = 42,
      .bodies = {{.entity = augusta::simulation::EntityId{1}, .body = Body(1.0F, Stance::kStanding)},
                 {.entity = augusta::simulation::EntityId{2},
                  .body = Body(2.0F, Stance::kCrouching),
                  .yaw = augusta::math::SnapAngle(-2.345678F)},
                 {.entity = augusta::simulation::EntityId{3}, .body = Body(3.0F, Stance::kProne), .yaw = 1.5F}},
      .recipients = {},
  };
  const augusta::replication::RecipientUpdate recipient{
      .entity = augusta::simulation::EntityId{2}, .acknowledged_sequence = 17, .health = 55.0F, .queued_commands = 2};

  const augusta::harness::AuthoritativeState received = ReceivedBy(sent, recipient);

  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, recipient.acknowledged_sequence);
  EXPECT_EQ(received.health, recipient.health);
  EXPECT_EQ(received.queued_commands, recipient.queued_commands);
  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    EXPECT_EQ(Number(received.bodies[i].entity), Number(sent.bodies[i].entity));
    ExpectSameBody(received.bodies[i].body, sent.bodies[i].body);
    EXPECT_EQ(received.bodies[i].yaw, sent.bodies[i].yaw);
  }
}

// The recipient's rifle's times are off every grid: they reach the client as
// the exact floats the server stepped them to, and its Recoil offset, which
// weapon::Step keeps on the angle grid, as the server had it.
TEST(WireTest, TheRecipientsRifleReachesTheClientExactly) {
  const augusta::replication::Updates updates{
      .tick = 7,
      .bodies = {{.entity = augusta::simulation::EntityId{1}, .body = Body(1.0F, Stance::kStanding)}},
      .recipients = {},
  };
  const augusta::replication::RecipientUpdate sent{
      .entity = augusta::simulation::EntityId{1},
      .acknowledged_sequence = 3,
      .rifle = {.cooldown = 0.1F - (1.0F / 60.0F),
                .reload_remaining = 2.4833333F,
                .recoil = {.pitch = augusta::math::SnapAngle(0.0421F), .yaw = augusta::math::SnapAngle(-0.0037F)},
                .rounds = 27,
                .burst_index = 4},
  };

  const augusta::harness::AuthoritativeState received = ReceivedBy(updates, sent);

  EXPECT_EQ(received.rifle, sent.rifle);
}

// Requirements: US-22
TEST(WireTest, TheParametersAJoinAcceptedCarriesReachTheClientUnchanged) {
  augusta::parameters::Parameters parameters;
  parameters.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F};
  parameters.rifle = {.rounds_per_minute = 650.0F,
                      .muzzle_velocity = 820.0F,
                      .reload_seconds = 2.25F,
                      .recoil_recovery_per_second = 0.15F,
                      .ads_recoil_scale = 0.6F,
                      .ads_field_of_view = 0.65F,
                      .recoil_pattern = {{.pitch = 0.01F, .yaw = 0.002F}, {.pitch = 0.007F, .yaw = -0.003F}},
                      .magazine_capacity = 25};
  parameters.ammo = {.gravity = 9.81F, .max_range = 900.0F, .damage = {.head = 100.0F, .torso = 34.0F, .limb = 22.5F}};
  parameters.starting_health = 120.0F;
  parameters.player_count = 4;

  const augusta::protocol::JoinAcceptedWire received =
      ThroughTheWire(augusta::protocol::JoinAcceptedWire{.session = augusta::protocol::SessionIdWire{1},
                                                         .parameters = augusta::server::ToWire(parameters),
                                                         .character = "soldier"});

  // Every parameter travels as its exact bits, so nothing is rounded on the way.
  const augusta::parameters::Parameters received_parameters = augusta::harness::FromWire(received.parameters);
  EXPECT_EQ(received_parameters.stamina.deplete_per_second, parameters.stamina.deplete_per_second);
  EXPECT_EQ(received_parameters.stamina.regen_per_second, parameters.stamina.regen_per_second);
  EXPECT_EQ(received_parameters.stamina.forced_walk_below, parameters.stamina.forced_walk_below);
  EXPECT_EQ(received_parameters.rifle, parameters.rifle);
  EXPECT_EQ(received_parameters.ammo, parameters.ammo);
  EXPECT_EQ(received_parameters.starting_health, parameters.starting_health);
  EXPECT_EQ(received_parameters.player_count, parameters.player_count);
}

TEST(WireTest, TheRosterTheServerSendsReachesTheClientUnchanged) {
  const augusta::server::Roster sent{
      .version = 7,
      .players = {{.session = SessionId{3}, .character = "medic"}, {.session = SessionId{5}, .character = "sniper"}}};

  const augusta::harness::Lobby received = augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  EXPECT_EQ(received.version, sent.version);
  ASSERT_EQ(received.roster.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(Number(received.roster[i].session), Number(sent.players[i].session));
    EXPECT_EQ(received.roster[i].character, sent.players[i].character);
  }
}

TEST(WireTest, AMatchStartTheServerSendsReachesTheClientUnchangedWithItsFirstTick) {
  const augusta::server::MatchStart sent{
      .players = {{.session = SessionId{3}, .entity = EntityId{11}, .character = "medic"},
                  {.session = SessionId{5}, .entity = EntityId{12}, .character = "sniper"}}};
  const std::vector<Vec3> spawns{Vec3(4.0F, 0.5F, -8.0F), Vec3(-1.0F, 0.0F, 2.0F)};

  const augusta::harness::MatchStart received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent, spawns, 0x1'0000'0042ULL)));

  EXPECT_EQ(received.first_tick, 0x1'0000'0042ULL);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(Number(received.players[i].session), Number(sent.players[i].session));
    EXPECT_EQ(Number(received.players[i].entity), Number(sent.players[i].entity));
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, spawns[i]);
  }
}

TEST(WireTest, AJoinRequestTheClientSendsReachesTheServerUnchanged) {
  augusta::assets::PackHash client_pack{};
  client_pack.front() = std::byte{0xAB};
  client_pack.back() = std::byte{0x01};
  const augusta::harness::JoinRequest sent{
      .engine_version = "1.2.3", .client_pack = client_pack, .character = "soldier"};

  const augusta::server::JoinRequest received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  EXPECT_EQ(received.engine_version, sent.engine_version);
  EXPECT_EQ(received.client_pack, sent.client_pack);
  EXPECT_EQ(received.character, sent.character);
}

TEST(WireTest, TheAdmissionTheServerSendsReachesTheClientUnchanged) {
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  const augusta::server::Admission sent{.session = SessionId{7}, .character = "soldier"};

  const augusta::harness::Admission received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent, 30, parameters)));

  EXPECT_EQ(Number(received.session), Number(sent.session));
  EXPECT_EQ(received.character, sent.character);
  EXPECT_EQ(received.tick_rate_hz, 30);
  EXPECT_EQ(received.parameters.player_count, parameters.player_count);
}

TEST(WireTest, EveryRefusalTheServerSendsReachesTheClientAsTheSameReason) {
  using ClientRefusal = augusta::harness::JoinRefusal;
  using ServerRefusal = augusta::server::JoinRefusal;
  const std::array<std::pair<ServerRefusal, ClientRefusal>, 8> reasons = {{
      {ServerRefusal::kVersionMismatch, ClientRefusal::kVersionMismatch},
      {ServerRefusal::kLobbyFull, ClientRefusal::kLobbyFull},
      {ServerRefusal::kUnknownCharacter, ClientRefusal::kUnknownCharacter},
      {ServerRefusal::kMatchInProgress, ClientRefusal::kMatchInProgress},
      {ServerRefusal::kPackMismatch, ClientRefusal::kPackMismatch},
      {ServerRefusal::kReplayServer, ClientRefusal::kReplayServer},
      {ServerRefusal::kUnknownCapture, ClientRefusal::kUnknownCapture},
      {ServerRefusal::kNotAReplayServer, ClientRefusal::kNotAReplayServer},
  }};

  for (const auto& [sent, expected] : reasons) {
    const augusta::protocol::JoinRefusedWire received =
        ThroughTheWire(augusta::protocol::JoinRefusedWire{.reason = augusta::server::ToWire(sent)});
    EXPECT_EQ(augusta::harness::FromWire(received.reason), expected) << static_cast<int>(sent);
  }
}

TEST(WireTest, TheCommandsTheClientSendsReachTheServerInOrder) {
  std::vector<augusta::harness::SequencedCommand> sent(3);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::command::Sequence>(4 + i);
    sent[i].command.yaw = 0.25F * static_cast<float>(i);
  }

  const std::vector<augusta::server::SequencedCommand> received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].sequence, sent[i].sequence);
    EXPECT_EQ(received[i].command.yaw, sent[i].command.yaw);
  }
}

TEST(WireTest, AShotTheServerAnnouncesReachesTheClientUnchanged) {
  // Numbers on their grids, as SimulationWorld fires a Shot (ADR-0038).
  const augusta::replication::Shot sent{
      .shooter = augusta::simulation::EntityId{3},
      .tick = 1200,
      .origin = augusta::math::SnapPosition(Vec3(12.345F, 1.6F, -7.77F)),
      .yaw = augusta::math::SnapAngle(-2.345678F),
      .pitch = augusta::math::SnapAngle(0.123456F),
  };

  const augusta::harness::Shot received = augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  EXPECT_EQ(Number(received.shooter), Number(sent.shooter));
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.origin, sent.origin);
  EXPECT_EQ(received.yaw, sent.yaw);
  EXPECT_EQ(received.pitch, sent.pitch);
}

TEST(WireTest, AHitConfirmationTheServerSendsReachesTheClientUnchanged) {
  using augusta::ballistics::BodyPart;
  for (const BodyPart part : {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb}) {
    const augusta::replication::HitConfirmation sent{
        .recipient = augusta::simulation::EntityId{3},
        .target = augusta::simulation::EntityId{5},
        .damage = 37.5F,
        .part = part,
    };

    const augusta::harness::HitConfirmation received =
        augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

    EXPECT_EQ(Number(received.target), Number(sent.target));
    EXPECT_EQ(received.part, part);
    EXPECT_EQ(received.damage, sent.damage);
  }
}

TEST(WireTest, AMatchEndTheServerSendsReachesTheClientWithItsWinnerOrAsADraw) {
  const augusta::server::MatchEnd won{.players = {SessionId{3}, SessionId{5}}, .winner = SessionId{5}};
  const augusta::server::MatchEnd drawn{.players = {SessionId{3}, SessionId{5}}, .winner = std::nullopt};

  const augusta::harness::MatchEnd won_received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(won)));
  const augusta::harness::MatchEnd drawn_received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(drawn)));

  ASSERT_TRUE(won_received.winner.has_value());
  EXPECT_EQ(Number(*won_received.winner), 5U);
  EXPECT_FALSE(drawn_received.winner.has_value());
}

TEST(WireTest, ADeathTheServerTellsReachesTheClientUnchanged) {
  using augusta::ballistics::BodyPart;
  for (const BodyPart part : {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb}) {
    // Its direction on the angle grid, as its Shot's (ADR-0038).
    const augusta::replication::Death sent{
        .victim = augusta::simulation::EntityId{5},
        .killer = augusta::simulation::EntityId{3},
        .yaw = augusta::math::SnapAngle(-2.345678F),
        .pitch = augusta::math::SnapAngle(0.123456F),
        .part = part,
    };

    const augusta::harness::Death received = augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

    EXPECT_EQ(Number(received.victim), Number(sent.victim));
    EXPECT_EQ(Number(received.killer), Number(sent.killer));
    EXPECT_EQ(received.yaw, sent.yaw);
    EXPECT_EQ(received.pitch, sent.pitch);
    EXPECT_EQ(received.part, part);
  }
}

// The value of failure's context under key, or empty if it has none.
std::string ContextOf(const augusta::failure::Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
}

// Either peer's edge turns a message the protocol cannot carry into the broken
// invariant that stops its runtime (ADR-0033), naming the message's type.
TEST(WireTest, AMessageEitherPeerCannotEncodeIsAnInvariantFailureNamingItsType) {
  const augusta::protocol::LobbyWire too_long{
      .version = 1, .roster = {{.character = std::string(augusta::protocol::kMaxCharacterNameLength + 1, 'c')}}};
  const augusta::protocol::ReadyWire fine{.version = 1};

  for (const auto& encoded : {augusta::server::EncodeToSend(too_long), augusta::harness::EncodeToSend(too_long)}) {
    ASSERT_FALSE(encoded.has_value());
    EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
    EXPECT_EQ(ContextOf(encoded.error(), "message_type"), "6");
    EXPECT_FALSE(encoded.error().detail.empty());
  }
  EXPECT_EQ(augusta::server::EncodeToSend(fine).value(), augusta::protocol::Encode(fine).value());
  EXPECT_EQ(augusta::harness::EncodeToSend(fine).value(), augusta::protocol::Encode(fine).value());
}

TEST(WireTest, ARecordTheServerCannotEncodeIsAnInvariantFailureNamingItsType) {
  const augusta::protocol::RecordingHeaderWire too_long{
      .server_pack = {},
      .engine_version = std::string(augusta::protocol::kMaxEngineVersionLength + 1, 'v'),
      .tick_rate_hz = 60};

  const auto encoded = augusta::server::EncodeToRecord(too_long);

  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
  EXPECT_EQ(ContextOf(encoded.error(), "record_type"), "1");
}

// A refusal no case of ToWire names is a corrupted one: it travels on as a
// value the protocol lacks, which Encode refuses, rather than as undefined
// behaviour.
TEST(WireTest, ACorruptedRefusalIsNotEncoded) {
  const auto corrupted = static_cast<augusta::server::JoinRefusal>(99);

  const auto encoded =
      augusta::server::EncodeToSend(augusta::protocol::JoinRefusedWire{.reason = augusta::server::ToWire(corrupted)});

  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
}

}  // namespace
