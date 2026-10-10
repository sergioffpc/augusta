#include "tick_messages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/ballistics.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/primitives.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "match.h"

// What the Host hands its connections after a tick, message by message: which
// peer is sent which bytes, how reliably, and in what order (ADR-0044).
namespace {

using augusta::networking::Payload;
using augusta::networking::PeerId;
using augusta::networking::Reliability;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::StanceWire;
using augusta::server::ForEachTickMessage;
using augusta::server::TickRecipients;
using augusta::simulation::EntityId;
using augusta::simulation::EntityState;
using augusta::simulation::State;

struct Sent {
  PeerId peer{};
  Payload payload;
  Reliability reliability{};
};

std::vector<Sent> SendAll(const State& state, augusta::tick::Tick tick, const TickRecipients& to) {
  std::vector<Sent> sent;
  const auto handed =
      ForEachTickMessage(state, tick, to, [&sent](PeerId peer, const Payload& payload, Reliability reliability) {
        sent.push_back(Sent{.peer = peer, .payload = payload, .reliability = reliability});
        return true;
      });
  EXPECT_TRUE(handed.has_value());
  return sent;
}

constexpr PeerId kPeer1{11};
constexpr PeerId kPeer2{12};
constexpr PeerId kPeer3{13};

// Three players in a match, told in the order of their bodies 2, 1, 3. Body 3
// is dead: it has none in the state.
TickRecipients ThreePlayers() {
  TickRecipients to;
  to.recipients = {
      {.entity = EntityId{2}, .acknowledged_sequence = 70, .queued_commands = 1},
      {.entity = EntityId{1}, .acknowledged_sequence = 900, .queued_commands = 3},
      {.entity = EntityId{3}, .acknowledged_sequence = 5, .queued_commands = 0},
  };
  to.peers = {{augusta::server::EntityId{1}, kPeer1},
              {augusta::server::EntityId{2}, kPeer2},
              {augusta::server::EntityId{3}, kPeer3}};
  return to;
}

// Bodies 1 and 2, each with a rifle and health of its own.
State TwoBodies() {
  EntityState first{.entity = EntityId{1}, .yaw = 0.5F, .health = 30.0F};
  first.body.position = augusta::math::Vec3(1.0F, 0.0F, -2.0F);
  first.body.velocity = augusta::math::Vec3(0.0F, 0.0F, 4.0F);
  first.body.stamina = 0.25F;
  first.body.exhausted = true;
  first.body.stance = augusta::physics::Stance::kCrouching;
  first.rifle = {.cooldown = 0.05F, .reload_remaining = 0.0F, .rounds = 12, .burst_index = 2};
  EntityState second{.entity = EntityId{2}, .yaw = -1.0F, .health = 100.0F};
  second.body.position = augusta::math::Vec3(-3.0F, 0.0F, 5.0F);
  second.body.stamina = 1.0F;
  second.body.stance = augusta::physics::Stance::kProne;
  second.rifle = {.cooldown = 0.0F, .reload_remaining = 1.5F, .rounds = 3, .burst_index = 0};
  State state;
  state.tick = 42;
  state.bodies = {first, second};
  return state;
}

// Every body of TwoBodies, as each recipient is sent it.
std::vector<EntityStateWire> TwoBodiesOnTheWire() {
  return {
      EntityStateWire{.entity = EntityIdWire{1},
                      .body = BodyStateWire{.position = augusta::math::Vec3(1.0F, 0.0F, -2.0F),
                                            .velocity = augusta::math::Vec3(0.0F, 0.0F, 4.0F),
                                            .stamina = 0.25F,
                                            .flags = BodyStateWire::kExhausted,
                                            .stance = StanceWire::kCrouching},
                      .yaw = 0.5F},
      EntityStateWire{.entity = EntityIdWire{2},
                      .body = BodyStateWire{.position = augusta::math::Vec3(-3.0F, 0.0F, 5.0F),
                                            .stamina = 1.0F,
                                            .stance = StanceWire::kProne},
                      .yaw = -1.0F},
  };
}

Payload Encoded(const AuthoritativeStateWire& state) { return augusta::protocol::Encode(state).value(); }

std::vector<PeerId> PeersOf(const std::vector<Sent>& sent, std::size_t first, std::size_t count) {
  std::vector<PeerId> peers;
  for (std::size_t i = first; i < first + count; ++i) {
    peers.push_back(sent[i].peer);
  }
  std::ranges::sort(peers);
  return peers;
}

TEST(TickMessagesTest, EachRecipientIsSentEveryBodyAndItsOwnFieldsUnreliablyInRecipientOrder) {
  const std::vector<Sent> sent = SendAll(TwoBodies(), 42, ThreePlayers());

  ASSERT_EQ(sent.size(), 3U);
  const AuthoritativeStateWire to_second{
      .tick = 42,
      .bodies = TwoBodiesOnTheWire(),
      .rifle = {.cooldown = 0.0F, .reload_remaining = 1.5F, .rounds = 3, .burst_index = 0},
      .health = 100.0F,
      .acknowledged_sequence = 70,
      .queued_commands = 1,
  };
  const AuthoritativeStateWire to_first{
      .tick = 42,
      .bodies = TwoBodiesOnTheWire(),
      .rifle = {.cooldown = 0.05F, .reload_remaining = 0.0F, .rounds = 12, .burst_index = 2},
      .health = 30.0F,
      .acknowledged_sequence = 900,
      .queued_commands = 3,
  };
  const AuthoritativeStateWire to_dead_third{
      .tick = 42,
      .bodies = TwoBodiesOnTheWire(),
      .rifle = {},
      .health = 0.0F,
      .acknowledged_sequence = 5,
      .queued_commands = 0,
  };
  EXPECT_EQ(sent[0].peer, kPeer2);
  EXPECT_EQ(sent[0].payload, Encoded(to_second));
  EXPECT_EQ(sent[1].peer, kPeer1);
  EXPECT_EQ(sent[1].payload, Encoded(to_first));
  EXPECT_EQ(sent[2].peer, kPeer3);
  EXPECT_EQ(sent[2].payload, Encoded(to_dead_third));
  for (const Sent& message : sent) {
    EXPECT_EQ(message.reliability, Reliability::kUnreliable);
  }
}

// A Replay view is a Replay viewer's alone (ADR-0051): a live tick sends its
// players none, and their updates keep the bytes they had - the protocol's
// authoritative_state seed (tests/fuzz) holds them to that, byte for byte.
TEST(TickMessagesTest, ALiveTickSendsItsPlayersNoReplayView) {
  State state = TwoBodies();
  state.shots = {{.shooter = EntityId{1}, .origin = augusta::math::Vec3(1.0F, 1.5F, -2.0F), .yaw = 0.5F}};

  const std::vector<Sent> sent = SendAll(state, 42, ThreePlayers());

  ASSERT_FALSE(sent.empty());
  for (const Sent& message : sent) {
    EXPECT_NE(message.payload.front(), static_cast<std::byte>(augusta::protocol::MessageTypeWire::kReplayView));
  }
}

TEST(TickMessagesTest, CombatEventsFollowTheUpdatesReliablyShotsThenHitConfirmationsThenDeaths) {
  State state = TwoBodies();
  state.shots = {{.shooter = EntityId{1}, .origin = augusta::math::Vec3(1.0F, 1.5F, -2.0F), .yaw = 0.5F}};
  state.hits = {{.shooter = EntityId{1},
                 .target = EntityId{3},
                 .damage = 100.0F,
                 .health = 0.0F,
                 .part = augusta::ballistics::BodyPart::kHead,
                 .reached_zero = true}};
  state.deaths = {{.victim = EntityId{3},
                   .killer = EntityId{1},
                   .yaw = 0.5F,
                   .pitch = 0.0F,
                   .part = augusta::ballistics::BodyPart::kHead}};

  const std::vector<Sent> sent = SendAll(state, 42, ThreePlayers());

  ASSERT_EQ(sent.size(), 3U + 3U + 1U + 3U);
  const std::vector<PeerId> everyone = {kPeer1, kPeer2, kPeer3};
  const Payload shot =
      augusta::protocol::Encode(
          augusta::protocol::ShotWire{
              .tick = 42, .origin = augusta::math::Vec3(1.0F, 1.5F, -2.0F), .shooter = EntityIdWire{1}, .yaw = 0.5F})
          .value();
  const Payload hit =
      augusta::protocol::Encode(augusta::protocol::HitConfirmationWire{.target = EntityIdWire{3},
                                                                       .damage = 100.0F,
                                                                       .part = augusta::protocol::BodyPartWire::kHead})
          .value();
  const augusta::protocol::DeathWire death_wire{
      .victim = EntityIdWire{3},
      .killer = EntityIdWire{1},
      .yaw = 0.5F,
      .part = augusta::protocol::BodyPartWire::kHead,
  };
  const Payload death = augusta::protocol::Encode(death_wire).value();
  EXPECT_EQ(PeersOf(sent, 3, 3), everyone);
  EXPECT_EQ(sent[6].peer, kPeer1);
  EXPECT_EQ(PeersOf(sent, 7, 3), everyone);
  for (std::size_t i = 3; i < sent.size(); ++i) {
    EXPECT_EQ(sent[i].reliability, Reliability::kReliable) << i;
    EXPECT_EQ(sent[i].payload, i < 6 ? shot : i == 6 ? hit : death) << i;
  }
}

TEST(TickMessagesTest, NoOneInTheMatchMeansNothingIsSent) {
  EXPECT_TRUE(SendAll(TwoBodies(), 42, TickRecipients{}).empty());
}

// An update listing more bodies than the protocol carries is a broken
// invariant, never a peer's doing: it is not sent, in any build, and the
// failure stops the runtime (ADR-0033).
TEST(TickMessagesTest, AnUpdateTheProtocolCannotCarryIsNotSentAndIsAnInvariantFailure) {
  State state = TwoBodies();
  while (state.bodies.size() <= augusta::primitives::kMaxPlayers) {
    EntityState body = state.bodies.front();
    body.entity = EntityId{static_cast<std::uint32_t>(state.bodies.size() + 1)};
    state.bodies.push_back(body);
  }
  std::vector<Sent> sent;

  const auto handed = ForEachTickMessage(state, 42, ThreePlayers(),
                                         [&sent](PeerId peer, const Payload& payload, Reliability reliability) {
                                           sent.push_back({peer, payload, reliability});
                                           return true;
                                         });

  ASSERT_FALSE(handed.has_value());
  EXPECT_EQ(handed.error().code, augusta::failure::Code::kInvariantViolated);
  EXPECT_EQ(augusta::failure::DispositionOf(handed.error().code), augusta::failure::Disposition::kRuntime);
  EXPECT_TRUE(sent.empty());
}

// Every message of a tick is encoded before any is sent: a Death the protocol
// cannot carry, its body part corrupted, sends none of the tick's, not even
// the updates before it.
TEST(TickMessagesTest, AMessageTheProtocolCannotCarrySendsNoneOfTheTicks) {
  State state = TwoBodies();
  state.deaths = {{.victim = EntityId{2},
                   .killer = EntityId{1},
                   .yaw = 0.5F,
                   .pitch = 0.0F,
                   .part = static_cast<augusta::ballistics::BodyPart>(99)}};
  std::vector<Sent> sent;

  const auto handed = ForEachTickMessage(state, 42, ThreePlayers(),
                                         [&sent](PeerId peer, const Payload& payload, Reliability reliability) {
                                           sent.push_back({peer, payload, reliability});
                                           return true;
                                         });

  ASSERT_FALSE(handed.has_value());
  EXPECT_EQ(handed.error().code, augusta::failure::Code::kInvariantViolated);
  EXPECT_TRUE(sent.empty());
}

// A sink that declines a message is handed nothing more of the tick: how the
// Host stops sending once its runtime has failed mid-tick (ADR-0033).
TEST(TickMessagesTest, NothingMoreIsHandedOnOnceTheSinkDeclines) {
  ASSERT_GT(SendAll(TwoBodies(), 42, ThreePlayers()).size(), 1U);
  std::vector<Sent> sent;

  const auto handed = ForEachTickMessage(TwoBodies(), 42, ThreePlayers(),
                                         [&sent](PeerId peer, const Payload& payload, Reliability reliability) {
                                           sent.push_back({peer, payload, reliability});
                                           return false;
                                         });

  EXPECT_TRUE(handed.has_value());
  EXPECT_EQ(sent.size(), 1U);
}

}  // namespace
