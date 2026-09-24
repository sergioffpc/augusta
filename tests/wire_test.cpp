#include "wire.h"

#include <array>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/harness.h"
#include "augusta/harness_wire.h"
#include "augusta/input.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "command_queue.h"
#include "host.h"
#include "match.h"

// Each peer converts between the engine's types and the protocol's plain ones
// at its edge (ADR-0038) and nowhere else: the server in server/wire.h, the
// client in harness_wire.h. What one side sends must reach the other's engine
// types unchanged, bytes and all.
namespace {

using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;
using augusta::server::SessionId;
using augusta::simulation::PlayerId;

// message as the other peer decodes it.
template <typename MessageWire>
MessageWire ThroughTheWire(const MessageWire& message) {
  return std::get<MessageWire>(augusta::protocol::Decode(augusta::protocol::Encode(message)).value());
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
  EXPECT_EQ(actual.position, augusta::protocol::SnapPosition(expected.position));
  EXPECT_EQ(actual.velocity, augusta::protocol::SnapVelocity(expected.velocity));
  EXPECT_EQ(actual.stance, expected.stance);
  EXPECT_EQ(actual.stamina, augusta::protocol::SnapStamina(expected.stamina));
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

    const augusta::protocol::CommandsWire message{
        .commands = {{.sequence = 9, .command = augusta::harness::ToWire(sent)}}};
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

    const augusta::protocol::CommandsWire message{
        .commands = {{.sequence = 1, .command = augusta::harness::ToWire(sent)}}};
    const augusta::input::Command received = augusta::server::FromWire(ThroughTheWire(message).commands.at(0)).command;

    EXPECT_EQ(received.movement.sprint, sent.movement.sprint) << flag;
    EXPECT_EQ(received.ads, sent.ads) << flag;
    EXPECT_EQ(received.fire, sent.fire) << flag;
    EXPECT_EQ(received.reload, sent.reload) << flag;
  }
}

TEST(WireTest, AnAuthoritativeStateTheServerSendsReachesTheClientUnchanged) {
  const augusta::replication::Update sent{
      .recipient = PlayerId{2},
      .tick = 42,
      .acknowledged_sequence = 17,
      .players = {{.player = PlayerId{1}, .body = Body(1.0F, Stance::kStanding)},
                  {.player = PlayerId{2}, .body = Body(2.0F, Stance::kCrouching)},
                  {.player = PlayerId{3}, .body = Body(3.0F, Stance::kProne)}},
  };

  const augusta::harness::AuthoritativeState received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  EXPECT_EQ(received.tick, sent.tick);
  EXPECT_EQ(received.acknowledged_sequence, sent.acknowledged_sequence);
  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(Number(received.players[i].session), Number(augusta::server::SessionOf(sent.players[i].player)));
    ExpectSameBody(received.players[i].body, sent.players[i].body);
  }
}

TEST(WireTest, TheParametersAJoinAcceptedCarriesReachTheClientUnchanged) {
  augusta::parameters::Parameters parameters;
  parameters.stamina = {.deplete_per_second = 0.2F, .regen_per_second = 0.1F, .forced_walk_below = 0.05F};
  parameters.player_count = 4;

  const augusta::protocol::JoinAcceptedWire received =
      ThroughTheWire(augusta::protocol::JoinAcceptedWire{.session = augusta::protocol::SessionIdWire{1},
                                                         .parameters = augusta::server::ToWire(parameters),
                                                         .character = 1});

  const augusta::parameters::Parameters received_parameters = augusta::harness::FromWire(received.parameters);
  EXPECT_EQ(received_parameters.stamina.deplete_per_second, parameters.stamina.deplete_per_second);
  EXPECT_EQ(received_parameters.stamina.regen_per_second, parameters.stamina.regen_per_second);
  EXPECT_EQ(received_parameters.stamina.forced_walk_below, parameters.stamina.forced_walk_below);
  EXPECT_EQ(received_parameters.player_count, parameters.player_count);
}

TEST(WireTest, TheRosterTheServerSendsReachesTheClientUnchanged) {
  const augusta::server::Roster sent{
      .version = 7, .players = {{.session = SessionId{3}, .character = 2}, {.session = SessionId{5}, .character = 1}}};

  const augusta::harness::Lobby received = augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  EXPECT_EQ(received.version, sent.version);
  ASSERT_EQ(received.roster.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(Number(received.roster[i].session), Number(sent.players[i].session));
    EXPECT_EQ(received.roster[i].character, sent.players[i].character);
  }
}

TEST(WireTest, AMatchStartTheServerSendsReachesTheClientUnchanged) {
  const augusta::server::MatchStart sent{
      .players = {{.session = SessionId{3}, .character = 2, .spawn = Vec3(4.0F, 0.5F, -8.0F)},
                  {.session = SessionId{5}, .character = 1, .spawn = Vec3(-1.0F, 0.0F, 2.0F)}}};

  const augusta::harness::MatchStart received =
      augusta::harness::FromWire(ThroughTheWire(augusta::server::ToWire(sent)));

  ASSERT_EQ(received.players.size(), sent.players.size());
  for (std::size_t i = 0; i < sent.players.size(); ++i) {
    EXPECT_EQ(Number(received.players[i].session), Number(sent.players[i].session));
    EXPECT_EQ(received.players[i].character, sent.players[i].character);
    EXPECT_EQ(received.players[i].spawn, sent.players[i].spawn);
  }
}

TEST(WireTest, AJoinRequestTheClientSendsReachesTheServerUnchanged) {
  augusta::assets::PackHash client_pack{};
  client_pack.front() = std::byte{0xAB};
  client_pack.back() = std::byte{0x01};
  const augusta::harness::JoinRequest sent{
      .engine_version = "1.2.3", .client_pack = client_pack, .character = "characters/player"};

  const augusta::server::JoinRequest received =
      augusta::server::FromWire(ThroughTheWire(augusta::harness::ToWire(sent)));

  EXPECT_EQ(received.engine_version, sent.engine_version);
  EXPECT_EQ(received.client_pack, sent.client_pack);
  EXPECT_EQ(received.character, sent.character);
}

TEST(WireTest, TheAdmissionTheServerSendsReachesTheClientUnchanged) {
  augusta::parameters::Parameters parameters;
  parameters.player_count = 2;
  const augusta::server::Admission sent{.session = SessionId{7}, .character = 3};

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
  const std::array<std::pair<ServerRefusal, ClientRefusal>, 5> reasons = {{
      {ServerRefusal::kVersionMismatch, ClientRefusal::kVersionMismatch},
      {ServerRefusal::kLobbyFull, ClientRefusal::kLobbyFull},
      {ServerRefusal::kUnknownCharacter, ClientRefusal::kUnknownCharacter},
      {ServerRefusal::kMatchInProgress, ClientRefusal::kMatchInProgress},
      {ServerRefusal::kPackMismatch, ClientRefusal::kPackMismatch},
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
    sent[i].sequence = static_cast<std::uint32_t>(4 + i);
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

TEST(WireTest, APlayerAndItsSessionNameEachOther) {
  EXPECT_EQ(augusta::server::SessionOf(augusta::server::PlayerOf(SessionId{77})), SessionId{77});
  EXPECT_EQ(augusta::server::PlayerOf(augusta::server::SessionOf(PlayerId{9})), PlayerId{9});
  EXPECT_EQ(Number(augusta::server::PlayerOf(SessionId{77})), 77U);
}

}  // namespace
