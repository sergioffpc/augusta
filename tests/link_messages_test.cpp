#include "augusta/link_messages.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/grid.h"
#include "augusta/link_types.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/tick.h"

// The Link converts between the engine's types and the protocol's plain ones
// (ADR-0038) and nowhere else: what one peer sends must reach the other's
// engine types unchanged, bytes and all.
namespace {

using augusta::link::EntityId;
using augusta::link::FromWire;
using augusta::link::SequencedCommand;
using augusta::link::SessionId;
using augusta::link::ToWire;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::protocol::kMaxCommandsPerMessage;

// message as the other peer decodes it.
template <typename MessageWire>
MessageWire ThroughTheWire(const MessageWire& message) {
  return std::get<MessageWire>(augusta::protocol::Decode(augusta::protocol::Encode(message).value()).value());
}

// commands as the server takes them in, from the one message that carries them.
std::vector<SequencedCommand> Received(const std::vector<SequencedCommand>& commands) {
  return FromWire(ThroughTheWire(ToWire(commands)));
}

// count commands, numbered from first, each sampled on its own tick.
std::vector<SequencedCommand> Commands(std::size_t count, augusta::tick::Sequence first = 1) {
  std::vector<SequencedCommand> commands(count);
  for (std::size_t i = 0; i < count; ++i) {
    commands[i].sequence = first + static_cast<augusta::tick::Sequence>(i);
    commands[i].command.seen_tick = 500 + i;
  }
  return commands;
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

TEST(LinkMessagesTest, ACommandTheClientSendsReachesTheServerUnchanged) {
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

    const SequencedCommand received = Received({{.sequence = 9, .command = sent}}).at(0);

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
TEST(LinkMessagesTest, EachCommandOfAMessageReachesTheServerWithTheSeenTimeItWasSampledOn) {
  std::vector<SequencedCommand> sent(4);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::tick::Sequence>(40 + i);
    sent[i].command.seen_tick = 70000 + (2 * i);
    sent[i].command.seen_fraction = 0.25F * static_cast<float>(i);
  }

  const std::vector<SequencedCommand> received = Received(sent);

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].command.seen_tick, sent[i].command.seen_tick) << i;
    EXPECT_EQ(received[i].command.seen_fraction, sent[i].command.seen_fraction) << i;
  }
}

// The server's ticks never start over (ADR-0038): commands sampled either side
// of the last tick 32 bits hold reach the server with the Seen times they named.
TEST(LinkMessagesTest, SeenTimesEitherSideOfThirtyTwoBitsReachTheServerAsTheyWereSampled) {
  constexpr augusta::tick::Tick kLastOf32Bits = std::numeric_limits<std::uint32_t>::max();
  std::vector<SequencedCommand> sent(4);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::tick::Sequence>(1 + i);
    sent[i].command.seen_tick = kLastOf32Bits - 1 + i;
  }

  const std::vector<SequencedCommand> received = Received(sent);

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].command.seen_tick, sent[i].command.seen_tick) << i;
  }
}

// A command's Seen time travels as how far before the message's newest it is, in a
// byte: one further back arrives as far back as a byte tells, which is already
// beyond what the server judges a shot against.
TEST(LinkMessagesTest, ASeenTimeTooFarBeforeTheMessagesNewestReachesTheServerAsTheOldestAByteTells) {
  std::vector<SequencedCommand> sent(2);
  sent[0].command.seen_tick = 100;
  sent[1].command.seen_tick = 1000;

  const std::vector<SequencedCommand> received = Received(sent);

  EXPECT_EQ(received.at(0).command.seen_tick, 1000U - 255U);
  EXPECT_EQ(received.at(1).command.seen_tick, 1000U);
}

// An age the message's Seen tick cannot go back by names the first tick there is.
TEST(LinkMessagesTest, ASeenAgeBeyondTheMessagesSeenTickIsTheFirstTick) {
  augusta::protocol::CommandsWire message{.commands = {{.sequence = 1}}, .seen_tick = 3};
  message.commands[0].command.seen_age = 10;

  EXPECT_EQ(FromWire(ThroughTheWire(message)).at(0).command.seen_tick, 0U);
}

TEST(LinkMessagesTest, EachFlagOfACommandReachesTheServerAsItselfAlone) {
  for (int flag = 0; flag < 4; ++flag) {
    augusta::command::Command sent;
    sent.movement.sprint = flag == 0;
    sent.ads = flag == 1;
    sent.fire = flag == 2;
    sent.reload = flag == 3;

    const augusta::command::Command received = Received({{.sequence = 1, .command = sent}}).at(0).command;

    EXPECT_EQ(received.movement.sprint, sent.movement.sprint) << flag;
    EXPECT_EQ(received.ads, sent.ads) << flag;
    EXPECT_EQ(received.fire, sent.fire) << flag;
    EXPECT_EQ(received.reload, sent.reload) << flag;
  }
}

TEST(LinkMessagesTest, TheCommandsTheClientSendsReachTheServerInOrder) {
  std::vector<SequencedCommand> sent(3);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    sent[i].sequence = static_cast<augusta::tick::Sequence>(4 + i);
    sent[i].command.yaw = 0.25F * static_cast<float>(i);
  }

  const std::vector<SequencedCommand> received = Received(sent);

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t i = 0; i < sent.size(); ++i) {
    EXPECT_EQ(received[i].sequence, sent[i].sequence);
    EXPECT_EQ(received[i].command.yaw, sent[i].command.yaw);
  }
}

TEST(LinkMessagesTest, AsManyCommandsAsAMessageCarriesAllTravel) {
  const std::vector<SequencedCommand> sent = Commands(kMaxCommandsPerMessage);

  const std::vector<SequencedCommand> received = Received(sent);

  ASSERT_EQ(received.size(), kMaxCommandsPerMessage);
  EXPECT_EQ(received.front().sequence, sent.front().sequence);
  EXPECT_EQ(received.back().sequence, sent.back().sequence);
}

// More unacknowledged commands than a message carries: the tick's one message
// carries the newest within the protocol's bound, oldest first, and the older
// ones are never sent.
TEST(LinkMessagesTest, CommandsBeyondWhatAMessageCarriesLeaveOnlyTheNewest) {
  const std::vector<SequencedCommand> sent = Commands((2 * kMaxCommandsPerMessage) + 1, 30);

  const std::vector<SequencedCommand> received = Received(sent);

  ASSERT_EQ(received.size(), kMaxCommandsPerMessage);
  const std::size_t first_carried = sent.size() - kMaxCommandsPerMessage;
  for (std::size_t i = 0; i < received.size(); ++i) {
    EXPECT_EQ(received[i].sequence, sent[first_carried + i].sequence) << i;
    EXPECT_EQ(received[i].command.seen_tick, sent[first_carried + i].command.seen_tick) << i;
  }
}

// The message's Seen tick is the newest of the commands it carries, as it was
// before any was left out.
TEST(LinkMessagesTest, AMessageOfTheNewestCommandsIsSeenAsOfTheNewest) {
  const std::vector<SequencedCommand> sent = Commands(kMaxCommandsPerMessage + 3);

  EXPECT_EQ(ToWire(sent).seen_tick, sent.back().command.seen_tick);
}

TEST(LinkMessagesTest, WhatTheMessageCarriesIsAllASenderNeedKeep) {
  const std::vector<SequencedCommand> few = Commands(2);
  const std::vector<SequencedCommand> many = Commands(kMaxCommandsPerMessage + 3);

  EXPECT_EQ(augusta::link::Carried(few).size(), few.size());
  ASSERT_EQ(augusta::link::Carried(many).size(), kMaxCommandsPerMessage);
  EXPECT_EQ(augusta::link::Carried(many).front().sequence, many[3].sequence);
}

TEST(LinkMessagesTest, AnExhaustedBodyReachesTheClientStillExhausted) {
  BodyState exhausted = Body(1.0F, Stance::kStanding);
  exhausted.stamina = 0.1F;
  exhausted.exhausted = true;
  const augusta::link::AuthoritativeState sent{
      .tick = 7,
      .acknowledged_sequence = 3,
      .bodies = {{.entity = EntityId{1}, .body = exhausted},
                 {.entity = EntityId{2}, .body = Body(2.0F, Stance::kProne)}},
  };

  const augusta::link::AuthoritativeState received = FromWire(ThroughTheWire(ToWire(sent)));

  ASSERT_EQ(received.bodies.size(), 2U);
  EXPECT_TRUE(received.bodies[0].body.exhausted);
  EXPECT_FALSE(received.bodies[1].body.exhausted);
}

TEST(LinkMessagesTest, AnAuthoritativeStateTheServerSendsReachesTheClientUnchanged) {
  const augusta::link::AuthoritativeState sent{
      .tick = 42,
      .acknowledged_sequence = 17,
      .bodies = {{.entity = EntityId{1}, .body = Body(1.0F, Stance::kStanding)},
                 {.entity = EntityId{2},
                  .body = Body(2.0F, Stance::kCrouching),
                  .yaw = augusta::math::SnapAngle(-2.345678F)},
                 {.entity = EntityId{3}, .body = Body(3.0F, Stance::kProne), .yaw = 1.5F}},
      .health = 55.0F,
      .queued_commands = 2,
  };

  const augusta::link::AuthoritativeState received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  EXPECT_EQ(received.health, sent.health);
  EXPECT_EQ(received.queued_commands, sent.queued_commands);
  ASSERT_EQ(received.bodies.size(), sent.bodies.size());
  for (std::size_t i = 0; i < sent.bodies.size(); ++i) {
    EXPECT_EQ(received.bodies[i].entity, sent.bodies[i].entity);
    ExpectSameBody(received.bodies[i].body, sent.bodies[i].body);
    EXPECT_EQ(received.bodies[i].yaw, sent.bodies[i].yaw);
  }
}

// The recipient's rifle's times are off every grid: they reach the client as
// the exact floats the server stepped them to, and its Recoil offset, which
// weapon::Step keeps on the angle grid, as the server had it.
TEST(LinkMessagesTest, TheRecipientsRifleReachesTheClientExactly) {
  const augusta::link::AuthoritativeState sent{
      .tick = 7,
      .acknowledged_sequence = 3,
      .bodies = {{.entity = EntityId{1}, .body = Body(1.0F, Stance::kStanding)}},
      .rifle = {.cooldown = 0.1F - (1.0F / 60.0F),
                .reload_remaining = 2.4833333F,
                .recoil = {.pitch = augusta::math::SnapAngle(0.0421F), .yaw = augusta::math::SnapAngle(-0.0037F)},
                .rounds = 27,
                .burst_index = 4},
  };

  const augusta::link::AuthoritativeState received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.rifle, sent.rifle);
}

// One message, its bodies converted once, is addressed to each recipient in
// turn: each is sent its own fields alone, and every body.
TEST(LinkMessagesTest, AnAddressedStateCarriesItsRecipientsOwnFieldsAndEveryBody) {
  const augusta::link::AuthoritativeState first{
      .tick = 9,
      .acknowledged_sequence = 4,
      .bodies = {{.entity = EntityId{1}, .body = Body(1.0F, Stance::kStanding)},
                 {.entity = EntityId{2}, .body = Body(2.0F, Stance::kProne)}},
      .health = 80.0F,
      .queued_commands = 1,
  };
  const augusta::link::AuthoritativeState second{.tick = 1,
                                                 .acknowledged_sequence = 11,
                                                 .bodies = {},
                                                 .rifle = {.rounds = 3},
                                                 .health = 20.0F,
                                                 .queued_commands = 5};
  augusta::protocol::AuthoritativeStateWire message = ToWire(first);

  augusta::link::FillRecipientFields(message, second);
  const augusta::link::AuthoritativeState received = FromWire(ThroughTheWire(message));

  EXPECT_EQ(received.tick, first.tick);
  ASSERT_EQ(received.bodies.size(), first.bodies.size());
  EXPECT_EQ(received.bodies[1].entity, EntityId{2});
  EXPECT_EQ(received.acknowledged_sequence, second.acknowledged_sequence);
  EXPECT_EQ(received.rifle, second.rifle);
  EXPECT_EQ(received.health, second.health);
  EXPECT_EQ(received.queued_commands, second.queued_commands);
}

// Requirements: US-22
TEST(LinkMessagesTest, TheParametersAJoinAcceptedCarriesReachTheClientUnchanged) {
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
  parameters.ammo = {.flight = {.gravity = 9.81F, .max_range = 900.0F},
                     .damage = {.head = 100.0F, .torso = 34.0F, .limb = 22.5F}};
  parameters.starting_health = 120.0F;
  parameters.player_count = 4;

  const augusta::protocol::JoinAcceptedWire received = ThroughTheWire(augusta::protocol::JoinAcceptedWire{
      .session = augusta::protocol::SessionIdWire{1}, .parameters = ToWire(parameters), .character = "soldier"});

  // Every parameter travels as its exact bits, so nothing is rounded on the way.
  const augusta::parameters::Parameters received_parameters = FromWire(received.parameters);
  EXPECT_EQ(received_parameters.stamina.deplete_per_second, parameters.stamina.deplete_per_second);
  EXPECT_EQ(received_parameters.stamina.regen_per_second, parameters.stamina.regen_per_second);
  EXPECT_EQ(received_parameters.stamina.forced_walk_below, parameters.stamina.forced_walk_below);
  EXPECT_EQ(received_parameters.rifle, parameters.rifle);
  EXPECT_EQ(received_parameters.ammo, parameters.ammo);
  EXPECT_EQ(received_parameters.starting_health, parameters.starting_health);
  EXPECT_EQ(received_parameters.player_count, parameters.player_count);
}

TEST(LinkMessagesTest, TheRosterTheServerSendsReachesTheClientUnchanged) {
  const augusta::link::Lobby sent{
      .version = 7,
      .roster = {{.session = SessionId{3}, .character = "medic"}, {.session = SessionId{5}, .character = "sniper"}}};

  const augusta::link::Lobby received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.version, sent.version);
  ASSERT_EQ(received.roster.size(), sent.roster.size());
  for (std::size_t i = 0; i < sent.roster.size(); ++i) {
    EXPECT_EQ(received.roster[i].session, sent.roster[i].session);
    EXPECT_EQ(received.roster[i].character, sent.roster[i].character);
  }
}

TEST(LinkMessagesTest, AMatchStartTheServerSendsReachesTheClientUnchangedWithItsFirstTick) {
  const augusta::link::MatchStart sent{
      .players =
          {{.session = SessionId{3}, .entity = EntityId{11}, .character = "medic", .spawn = Vec3(4.0F, 0.5F, -8.0F)},
           {.session = SessionId{5}, .entity = EntityId{12}, .character = "sniper", .spawn = Vec3(-1.0F, 0.0F, 2.0F)}},
      .first_tick = 0x1'0000'0042ULL};

  const augusta::link::MatchStart received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.first_tick, 0x1'0000'0042ULL);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    EXPECT_EQ(received.players[i].entity, sent.players[i].entity);
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, sent.players[i].spawn);
  }
}

TEST(LinkMessagesTest, AJoinRequestTheClientSendsReachesTheServerUnchanged) {
  augusta::assets::PackHash client_pack{};
  client_pack.front() = std::byte{0xAB};
  client_pack.back() = std::byte{0x01};
  const augusta::link::JoinRequest sent{.engine_version = "1.2.3", .client_pack = client_pack, .character = "soldier"};

  const augusta::link::JoinRequest received =
      FromWire(ThroughTheWire(std::get<augusta::protocol::JoinRequestWire>(ToWire(sent))));

  EXPECT_EQ(received.engine_version, sent.engine_version);
  EXPECT_EQ(received.client_pack, sent.client_pack);
  EXPECT_EQ(received.character, sent.character);
  EXPECT_FALSE(received.spawn.has_value());
}

// Requirements: US-21
TEST(LinkMessagesTest, AJoinThatNamesASpawnTravelsAsAReenactRequestAndReachesTheServerUnchanged) {
  augusta::assets::PackHash client_pack{};
  client_pack.front() = std::byte{0xCD};
  const augusta::link::JoinRequest sent{.engine_version = "1.2.3",
                                        .client_pack = client_pack,
                                        .character = "soldier",
                                        .spawn = augusta::math::SnapPosition(Vec3(4.5F, 0.25F, -9.75F))};

  const augusta::protocol::MessageWire wire = ToWire(sent);
  ASSERT_TRUE(std::holds_alternative<augusta::protocol::ReenactRequestWire>(wire));
  const augusta::link::JoinRequest received =
      FromWire(ThroughTheWire(std::get<augusta::protocol::ReenactRequestWire>(wire)));

  EXPECT_EQ(received.engine_version, sent.engine_version);
  EXPECT_EQ(received.client_pack, sent.client_pack);
  EXPECT_EQ(received.character, sent.character);
  EXPECT_EQ(received.spawn, sent.spawn);
}

TEST(LinkMessagesTest, AReplayRequestTheClientSendsReachesTheServerUnchanged) {
  augusta::assets::PackHash client_pack{};
  client_pack.back() = std::byte{0x7F};
  const augusta::link::ReplayRequest sent{
      .engine_version = "1.2.3", .client_pack = client_pack, .capture = "match.acap"};

  const augusta::link::ReplayRequest received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.engine_version, sent.engine_version);
  EXPECT_EQ(received.client_pack, sent.client_pack);
  EXPECT_EQ(received.capture, sent.capture);
}

TEST(LinkMessagesTest, TheAdmissionTheServerSendsReachesTheClientUnchanged) {
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  const augusta::link::Admission sent{
      .session = SessionId{7}, .tick_rate_hz = 30, .parameters = parameters, .character = "soldier"};

  const augusta::link::Admission received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.session, sent.session);
  EXPECT_EQ(received.character, sent.character);
  EXPECT_EQ(received.tick_rate_hz, 30);
  EXPECT_EQ(received.parameters.player_count, parameters.player_count);
}

TEST(LinkMessagesTest, EveryRefusalTheServerSendsReachesTheClientAsTheSameReason) {
  using augusta::link::JoinRefusal;
  for (const JoinRefusal sent :
       {JoinRefusal::kVersionMismatch, JoinRefusal::kLobbyFull, JoinRefusal::kUnknownCharacter,
        JoinRefusal::kMatchInProgress, JoinRefusal::kPackMismatch, JoinRefusal::kReplayServer,
        JoinRefusal::kUnknownCapture, JoinRefusal::kNotAReplayServer, JoinRefusal::kReenactmentsNotAccepted}) {
    const augusta::protocol::JoinRefusedWire received =
        ThroughTheWire(augusta::protocol::JoinRefusedWire{.reason = ToWire(sent)});
    EXPECT_EQ(FromWire(received.reason), sent) << static_cast<int>(sent);
  }
}

TEST(LinkMessagesTest, AShotTheServerAnnouncesReachesTheClientUnchanged) {
  // Numbers on their grids, as SimulationWorld fires a Shot (ADR-0038).
  const augusta::link::Shot sent{
      .shooter = EntityId{3},
      .tick = 1200,
      .origin = augusta::math::SnapPosition(Vec3(12.345F, 1.6F, -7.77F)),
      .yaw = augusta::math::SnapAngle(-2.345678F),
      .pitch = augusta::math::SnapAngle(0.123456F),
  };

  const augusta::link::Shot received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.shooter, sent.shooter);
  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.origin, sent.origin);
  EXPECT_EQ(received.yaw, sent.yaw);
  EXPECT_EQ(received.pitch, sent.pitch);
}

TEST(LinkMessagesTest, AHitConfirmationTheServerSendsReachesTheClientUnchanged) {
  using augusta::body_part::BodyPart;
  for (const BodyPart part : {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb}) {
    const augusta::link::HitConfirmation sent{.target = EntityId{5}, .damage = 37.5F, .part = part};

    const augusta::link::HitConfirmation received = FromWire(ThroughTheWire(ToWire(sent)));

    EXPECT_EQ(received.target, sent.target);
    EXPECT_EQ(received.part, part);
    EXPECT_EQ(received.damage, sent.damage);
  }
}

TEST(LinkMessagesTest, AMatchEndTheServerSendsReachesTheClientWithItsWinnerOrAsADraw) {
  const augusta::link::MatchEnd won{.winner = SessionId{5}};
  const augusta::link::MatchEnd drawn{.winner = std::nullopt};

  const augusta::link::MatchEnd won_received = FromWire(ThroughTheWire(ToWire(won)));
  const augusta::link::MatchEnd drawn_received = FromWire(ThroughTheWire(ToWire(drawn)));

  EXPECT_EQ(won_received.winner, SessionId{5});
  EXPECT_FALSE(drawn_received.winner.has_value());
}

TEST(LinkMessagesTest, ADeathTheServerTellsReachesTheClientUnchanged) {
  using augusta::body_part::BodyPart;
  for (const BodyPart part : {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb}) {
    // Its direction on the angle grid, as its Shot's (ADR-0038).
    const augusta::link::Death sent{
        .victim = EntityId{5},
        .killer = EntityId{3},
        .yaw = augusta::math::SnapAngle(-2.345678F),
        .pitch = augusta::math::SnapAngle(0.123456F),
        .part = part,
    };

    const augusta::link::Death received = FromWire(ThroughTheWire(ToWire(sent)));

    EXPECT_EQ(received.victim, sent.victim);
    EXPECT_EQ(received.killer, sent.killer);
    EXPECT_EQ(received.yaw, sent.yaw);
    EXPECT_EQ(received.pitch, sent.pitch);
    EXPECT_EQ(received.part, part);
  }
}

TEST(LinkMessagesTest, AReplayViewTheReplayServerSendsReachesTheViewerUnchanged) {
  const augusta::link::ReplayView sent{
      .tick = 90,
      .players = {{.entity = EntityId{1}, .pitch = augusta::math::SnapAngle(0.25F), .ads = true},
                  {.entity = EntityId{2}, .pitch = augusta::math::SnapAngle(-0.5F), .ads = false}}};

  const augusta::link::ReplayView received = FromWire(ThroughTheWire(ToWire(sent)));

  EXPECT_EQ(received.tick, sent.tick);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].entity, sent.players[i].entity);
    EXPECT_EQ(received.players[i].pitch, sent.players[i].pitch);
    EXPECT_EQ(received.players[i].ads, sent.players[i].ads);
  }
}

TEST(LinkMessagesTest, AReplayListTheReplayServerSendsReachesTheClientUnchanged) {
  const std::vector<augusta::link::ReplayListing> sent{
      {.name = "first.acap",
       .started = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(1'700'000'000'123)),
       .ticks = 3600,
       .tick_rate_hz = 60,
       .characters = {"medic", "sniper"}}};

  EXPECT_EQ(FromWire(ThroughTheWire(ToWire(sent))), sent);
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

// A message the protocol cannot carry is the broken invariant that stops the
// sender's runtime (ADR-0033), naming the message's type.
TEST(LinkMessagesTest, AMessageThatCannotBeEncodedIsAnInvariantFailureNamingItsType) {
  const augusta::protocol::LobbyWire too_long{
      .version = 1, .roster = {{.character = std::string(augusta::protocol::kMaxCharacterNameLength + 1, 'c')}}};
  const augusta::protocol::ReadyWire fine{.version = 1};

  const auto encoded = augusta::link::EncodeToSend(too_long);

  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
  EXPECT_EQ(ContextOf(encoded.error(), "message_type"), "6");
  EXPECT_FALSE(encoded.error().detail.empty());
  EXPECT_EQ(augusta::link::EncodeToSend(fine).value(), augusta::protocol::Encode(fine).value());
}

// A refusal no case of ToWire names is a corrupted one: it travels on as a
// value the protocol lacks, which Encode refuses, rather than as undefined
// behaviour.
TEST(LinkMessagesTest, ACorruptedRefusalIsNotEncoded) {
  const auto corrupted = static_cast<augusta::link::JoinRefusal>(99);

  const auto encoded = augusta::link::EncodeToSend(augusta::protocol::JoinRefusedWire{.reason = ToWire(corrupted)});

  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().code, augusta::failure::Code::kInvariantViolated);
}

TEST(LinkMessagesTest, AnEncodedMessageIsOfTheTypeItsFirstByteSays) {
  EXPECT_EQ(augusta::link::TypeOf(augusta::link::EncodeToSend(augusta::protocol::ReadyWire{.version = 1}).value()),
            augusta::link::MessageType::kReady);
  EXPECT_EQ(augusta::link::TypeOf(augusta::link::EncodeToSend(ToWire(augusta::link::Shot{})).value()),
            augusta::link::MessageType::kShot);
}

TEST(LinkMessagesTest, AReadyTheClientSendsReachesTheServerWithItsVersion) {
  EXPECT_EQ(ToWire(augusta::link::Ready{.version = 7}), augusta::protocol::ReadyWire{.version = 7});
}

// What a client takes in is what a server says, each as the engine's type of it.
TEST(LinkMessagesTest, AServerMessageReachesTheClientAsWhatItSays) {
  const auto refused = augusta::link::ServerMessageFromWire(
      augusta::protocol::JoinRefusedWire{.reason = ToWire(augusta::link::JoinRefusal::kLobbyFull)});
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(std::get<augusta::link::JoinRefusal>(*refused), augusta::link::JoinRefusal::kLobbyFull);

  const auto list = augusta::link::ServerMessageFromWire(augusta::protocol::ReplayListWire{});
  ASSERT_TRUE(list.has_value());
  EXPECT_TRUE(std::get<std::vector<augusta::link::ReplayListing>>(*list).empty());
}

TEST(LinkMessagesTest, AMessageOnlyAClientSendsIsNoServerMessage) {
  EXPECT_FALSE(augusta::link::ServerMessageFromWire(augusta::protocol::ReadyWire{.version = 1}).has_value());
  EXPECT_FALSE(augusta::link::ServerMessageFromWire(augusta::protocol::ReplayListRequestWire{}).has_value());
}

// What a server takes in is what a client asks, each as the engine's type of
// it: a Reenact request is a join that names its spawn (ADR-0050).
TEST(LinkMessagesTest, AClientMessageReachesTheServerAsWhatItSays) {
  const auto ready = augusta::link::ClientMessageFromWire(augusta::protocol::ReadyWire{.version = 7});
  ASSERT_TRUE(ready.has_value());
  EXPECT_EQ(std::get<augusta::link::Ready>(*ready).version, 7U);

  const auto reenact = augusta::link::ClientMessageFromWire(augusta::protocol::ReenactRequestWire{
      .engine_version = "1.2.3", .client_pack = {}, .character = "soldier", .spawn = Vec3(1.0F, 2.0F, 3.0F)});
  ASSERT_TRUE(reenact.has_value());
  const auto& join = std::get<augusta::link::JoinRequest>(*reenact);
  EXPECT_EQ(join.character, "soldier");
  ASSERT_TRUE(join.spawn.has_value());
  EXPECT_FLOAT_EQ(join.spawn->y, 2.0F);

  const auto commands = augusta::link::ClientMessageFromWire(ToWire(Commands(3)));
  ASSERT_TRUE(commands.has_value());
  EXPECT_EQ(std::get<std::vector<SequencedCommand>>(*commands).size(), 3U);

  EXPECT_TRUE(augusta::link::ClientMessageFromWire(augusta::protocol::ReplayListRequestWire{}).has_value());
}

TEST(LinkMessagesTest, AMessageOnlyAServerSendsIsNoClientMessage) {
  EXPECT_FALSE(augusta::link::ClientMessageFromWire(ToWire(augusta::link::Shot{})).has_value());
  EXPECT_FALSE(augusta::link::ClientMessageFromWire(augusta::protocol::ReplayListWire{}).has_value());
}

}  // namespace
