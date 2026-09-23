#include "wire.h"

#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "command_queue.h"
#include "match.h"

// Each peer converts between the engine's types and the protocol's plain ones
// at its edge (ADR-0038): the server in server::Host, the client in
// harness::Session. What one side sends must reach the other's engine types
// unchanged, bytes and all.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::protocol::SessionId;

// message as the other peer decodes it.
template <typename Message>
Message ThroughTheWire(const Message& message) {
  return std::get<Message>(augusta::protocol::Decode(augusta::protocol::Encode(message)).value());
}

BodyState Body(float x, Stance stance) {
  BodyState body;
  body.position = Vec3(x, 1.5F, -2.0F);
  body.velocity = Vec3(0.25F, -9.5F, 3.0F);
  body.stance = stance;
  body.stamina = 0.4F;
  return body;
}

void ExpectSameBody(const BodyState& actual, const BodyState& expected) {
  EXPECT_EQ(actual.position, expected.position);
  EXPECT_EQ(actual.velocity, expected.velocity);
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, expected.stamina);
}

TEST(WireTest, ACommandTheClientSendsReachesTheServerUnchanged) {
  for (const Stance stance : {Stance::kStanding, Stance::kCrouching, Stance::kProne}) {
    augusta::input::Command sent;
    sent.movement.direction = Vec3(0.5F, 0.0F, -1.0F);
    sent.movement.sprint = true;
    sent.movement.desired_stance = stance;
    sent.yaw = 1.25F;
    sent.pitch = -0.5F;
    sent.ads = true;
    sent.fire = true;
    sent.reload = true;

    const augusta::protocol::Commands message{.commands = {{.sequence = 9, .command = augusta::harness::ToWire(sent)}}};
    const augusta::server::SequencedCommand received =
        augusta::server::FromWire(ThroughTheWire(message).commands.at(0));

    EXPECT_EQ(received.sequence, 9U);
    EXPECT_EQ(received.command.movement.direction, sent.movement.direction);
    EXPECT_EQ(received.command.movement.sprint, sent.movement.sprint);
    EXPECT_EQ(received.command.movement.desired_stance, stance);
    EXPECT_EQ(received.command.yaw, sent.yaw);
    EXPECT_EQ(received.command.pitch, sent.pitch);
    EXPECT_EQ(received.command.ads, sent.ads);
    EXPECT_EQ(received.command.fire, sent.fire);
    EXPECT_EQ(received.command.reload, sent.reload);
  }
}

TEST(WireTest, EachFlagOfACommandReachesTheServerAsItselfAlone) {
  for (int flag = 0; flag < 4; ++flag) {
    augusta::input::Command sent;
    sent.movement.sprint = flag == 0;
    sent.ads = flag == 1;
    sent.fire = flag == 2;
    sent.reload = flag == 3;

    const augusta::protocol::Commands message{.commands = {{.sequence = 1, .command = augusta::harness::ToWire(sent)}}};
    const augusta::input::Command received = augusta::server::FromWire(ThroughTheWire(message).commands.at(0)).command;

    EXPECT_EQ(received.movement.sprint, sent.movement.sprint) << flag;
    EXPECT_EQ(received.ads, sent.ads) << flag;
    EXPECT_EQ(received.fire, sent.fire) << flag;
    EXPECT_EQ(received.reload, sent.reload) << flag;
  }
}

TEST(WireTest, AnAuthoritativeStateTheServerSendsReachesTheClientUnchanged) {
  const augusta::replication::Update sent{
      .recipient = SessionId{2},
      .tick = 42,
      .acknowledged_sequence = 17,
      .players = {{.session = SessionId{1}, .body = Body(1.0F, Stance::kStanding)},
                  {.session = SessionId{2}, .body = Body(2.0F, Stance::kCrouching)},
                  {.session = SessionId{3}, .body = Body(3.0F, Stance::kProne)}},
  };

  const augusta::harness::AuthoritativeState received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(received.players[i].session, sent.players[i].session);
    ExpectSameBody(received.players[i].body, sent.players[i].body);
  }
}

TEST(WireTest, WhatAJoinAcceptedCarriesReachesTheClientUnchanged) {
  const Vec3 spawn(4.0F, 0.5F, -8.0F);
  const augusta::server::RosterEntry entry{.session = SessionId{5}, .body = Body(6.0F, Stance::kCrouching)};
  augusta::parameters::Parameters parameters;
  parameters.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F};

  const augusta::protocol::JoinAccepted received = ThroughTheWire(augusta::protocol::JoinAccepted{
      .session = SessionId{1},
      .spawn = spawn,
      .parameters = augusta::server::ToWire(parameters),
      .roster = {augusta::server::ToWire(entry)},
  });

  EXPECT_EQ(received.spawn, spawn);
  const augusta::parameters::Parameters received_parameters = augusta::harness::FromWire(received.parameters);
  EXPECT_EQ(received_parameters.stamina.deplete_per_second, parameters.stamina.deplete_per_second);
  EXPECT_EQ(received_parameters.stamina.regen_per_second, parameters.stamina.regen_per_second);
  EXPECT_EQ(received_parameters.stamina.forced_walk_below, parameters.stamina.forced_walk_below);
  ASSERT_EQ(received.roster.size(), 1U);
  const augusta::harness::PlayerBody player = augusta::harness::FromWire(received.roster[0]);
  EXPECT_EQ(player.session, entry.session);
  ExpectSameBody(player.body, entry.body);
}

}  // namespace
