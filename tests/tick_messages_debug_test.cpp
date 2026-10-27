#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/body_part.h"
#include "augusta/failure.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/simulation.h"
#include "debug_subscriptions.h"
#include "tick_messages.h"

// Which peer the Host sends a tick's Shot judgements to, in a Development
// build alone (ADR-0038, ADR-0074), as tick_messages_test.cpp has the rest of
// the tick's messages.
namespace {

using augusta::host::DebugSubscriptions;
using augusta::host::ForEachTickMessage;
using augusta::host::ShotJudgementPeers;
using augusta::host::TickRecipients;
using augusta::link::DebugSubscribe;
using augusta::link::MessageType;
using augusta::link::Outgoing;
using augusta::link::PeerId;
using augusta::math::Vec3;
using augusta::protocol::EntityIdWire;
using augusta::simulation::EntityId;
using augusta::simulation::TickResult;

using Payload = std::vector<std::byte>;

struct Sent {
  PeerId peer{};
  Payload payload;
  MessageType type{};
};

constexpr PeerId kPeer1{11};
constexpr PeerId kPeer2{12};
constexpr PeerId kPeer3{13};

// What the tick hands on to whom, or nothing if it is an invariant failure.
std::vector<Sent> SendAll(const TickResult& result, const TickRecipients& to) {
  std::vector<Sent> sent;
  const auto handed = ForEachTickMessage(result, 42, to, [&sent](PeerId peer, const Outgoing& message) {
    sent.push_back(
        Sent{.peer = peer, .payload = {message.Payload().begin(), message.Payload().end()}, .type = message.Type()});
    return true;
  });
  EXPECT_TRUE(handed.has_value());
  return sent;
}

// The players of bodies 1, 2 and 3, on peers 1, 2 and 3, Shot judgements going
// to those subscriptions says want them.
TickRecipients ThreePlayers(const DebugSubscriptions& subscriptions) {
  TickRecipients to;
  to.recipients = {{.entity = EntityId{1}}, {.entity = EntityId{2}}, {.entity = EntityId{3}}};
  to.peers = {
      {augusta::host::EntityId{1}, kPeer1}, {augusta::host::EntityId{2}, kPeer2}, {augusta::host::EntityId{3}, kPeer3}};
  to.shot_judgement_peers = ShotJudgementPeers(to, subscriptions);
  return to;
}

// Subscriptions on a server whose key is served_on, where each of peers set
// flags.
DebugSubscriptions Subscribed(bool served_on, const std::vector<PeerId>& peers, std::uint8_t flags) {
  DebugSubscriptions subscriptions(served_on ? DebugSubscribe::kShotJudgements : std::uint8_t{0});
  for (const PeerId peer : peers) {
    static_cast<void>(subscriptions.Subscribe(peer, flags));
  }
  return subscriptions;
}

// A round body 1 fired on tick 30 at a delay of 6.5 ticks, which crossed body
// 2's bounds on the tick it was fired and struck its head the next.
augusta::simulation::ShotJudgement BodyOnesHeadshot() {
  const augusta::simulation::JudgedPlayer second{.entity = EntityId{2},
                                                 .position = Vec3(3.0F, 0.0F, -40.0F),
                                                 .stance = augusta::physics::Stance::kCrouching,
                                                 .yaw = 1.5F};
  return augusta::simulation::ShotJudgement{
      .shooter = EntityId{1},
      .shot_tick = 30,
      .shooters_delay = 6.5F,
      .struck = augusta::simulation::Struck::kHitbox,
      .part = augusta::body_part::BodyPart::kHead,
      .point = Vec3(3.0F, 1.75F, -40.0F),
      .target = EntityId{2},
      .entries = {{.ticks_after_shot = 0, .players = {second}}, {.ticks_after_shot = 1, .players = {second}}},
      .entries_left_out = false};
}

// The same on the wire.
Payload BodyOnesHeadshotOnTheWire() {
  const augusta::protocol::JudgedPlayerWire second{.position = Vec3(3.0F, 0.0F, -40.0F),
                                                   .entity = EntityIdWire{2},
                                                   .yaw = 1.5F,
                                                   .stance = augusta::protocol::StanceWire::kCrouching};
  return augusta::protocol::Encode(
             augusta::protocol::ShotJudgementWire{.shot_tick = 30,
                                                  .entries = {{.players = {second}, .ticks_after_shot = 0},
                                                              {.players = {second}, .ticks_after_shot = 1}},
                                                  .point = Vec3(3.0F, 1.75F, -40.0F),
                                                  .target = EntityIdWire{2},
                                                  .delay_fraction = 0.5F,
                                                  .delay_ticks = 6,
                                                  .struck = augusta::protocol::StruckWire::kHitbox,
                                                  .part = augusta::protocol::BodyPartWire::kHead,
                                                  .flags = 0})
      .value();
}

// A tick on which body 1 killed body 3 with the round of BodyOnesHeadshot's
// flight that ended.
TickResult ATickWithAShotJudgement() {
  TickResult result;
  result.state.tick = 42;
  result.state.deaths = {{.victim = EntityId{3},
                          .killer = EntityId{1},
                          .yaw = 0.5F,
                          .pitch = 0.0F,
                          .part = augusta::body_part::BodyPart::kHead}};
  result.shot_judgements = {BodyOnesHeadshot()};
  return result;
}

std::vector<Sent> ShotJudgementsOf(const std::vector<Sent>& sent) {
  std::vector<Sent> judgements;
  for (const Sent& message : sent) {
    if (message.type == MessageType::kShotJudgement) {
      judgements.push_back(message);
    }
  }
  return judgements;
}

// The peers sent's Shot judgements went to, in the order they were sent.
std::vector<PeerId> ShotJudgementPeersOf(const std::vector<Sent>& sent) {
  std::vector<PeerId> peers;
  for (const Sent& message : ShotJudgementsOf(sent)) {
    peers.push_back(message.peer);
  }
  return peers;
}

// Every subscribed peer's own rounds alone: body 1's goes to peer 1 though
// all three asked, after the tick's Deaths.
// Requirements: US-30
TEST(TickMessagesDebugTest, AShotJudgementGoesToItsShootersPeerAloneAfterTheTicksDeaths) {
  const std::vector<Sent> sent =
      SendAll(ATickWithAShotJudgement(),
              ThreePlayers(Subscribed(true, {kPeer1, kPeer2, kPeer3}, DebugSubscribe::kShotJudgements)));

  ASSERT_FALSE(sent.empty());
  EXPECT_EQ(sent.back().type, MessageType::kShotJudgement);
  EXPECT_EQ(sent.back().peer, kPeer1);
  EXPECT_EQ(sent.back().payload, BodyOnesHeadshotOnTheWire());
  EXPECT_EQ(sent[sent.size() - 2].type, MessageType::kDeath);
  EXPECT_EQ(ShotJudgementsOf(sent).size(), 1U);
}

// Requirements: US-30
TEST(TickMessagesDebugTest, AShotJudgementIsNotSentWhileTheServerDoesNotServeThem) {
  const std::vector<Sent> served =
      SendAll(ATickWithAShotJudgement(), ThreePlayers(Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements)));
  ASSERT_EQ(ShotJudgementPeersOf(served), std::vector<PeerId>{kPeer1});

  const std::vector<Sent> sent =
      SendAll(ATickWithAShotJudgement(), ThreePlayers(Subscribed(false, {kPeer1}, DebugSubscribe::kShotJudgements)));

  EXPECT_TRUE(ShotJudgementsOf(sent).empty());
}

// Bit 0 cleared, whatever else is set: the stream is not asked for.
// Requirements: US-30
TEST(TickMessagesDebugTest, AShotJudgementIsNotSentToAShooterThatDidNotAskForThem) {
  const std::vector<Sent> asked =
      SendAll(ATickWithAShotJudgement(), ThreePlayers(Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements)));
  ASSERT_EQ(ShotJudgementPeersOf(asked), std::vector<PeerId>{kPeer1});

  for (const std::uint8_t flags : {std::uint8_t{0}, std::uint8_t{0xFE}}) {
    const std::vector<Sent> sent = SendAll(ATickWithAShotJudgement(), ThreePlayers(Subscribed(true, {kPeer1}, flags)));

    EXPECT_TRUE(ShotJudgementsOf(sent).empty()) << +flags;
  }
  const std::vector<Sent> others_asked = SendAll(
      ATickWithAShotJudgement(), ThreePlayers(Subscribed(true, {kPeer2, kPeer3}, DebugSubscribe::kShotJudgements)));
  EXPECT_TRUE(ShotJudgementsOf(others_asked).empty());
}

// Its shooter has left the match: no peer is told of its round.
// Requirements: US-30
TEST(TickMessagesDebugTest, AShotJudgementWhoseShooterHasNoPeerIsNotSent) {
  TickResult result = ATickWithAShotJudgement();
  result.shot_judgements.front().shooter = EntityId{4};
  const TickRecipients everyone =
      ThreePlayers(Subscribed(true, {kPeer1, kPeer2, kPeer3}, DebugSubscribe::kShotJudgements));
  ASSERT_EQ(ShotJudgementPeersOf(SendAll(ATickWithAShotJudgement(), everyone)), std::vector<PeerId>{kPeer1});

  const std::vector<Sent> sent = SendAll(result, everyone);

  EXPECT_TRUE(ShotJudgementsOf(sent).empty());
}

// Like every message of the tick, one the protocol cannot carry, its Body part
// corrupted, sends none of the tick's.
// Requirements: US-30, NFR-18
TEST(TickMessagesDebugTest, AShotJudgementTheProtocolCannotCarryIsAnInvariantFailureWithNothingSent) {
  TickResult result = ATickWithAShotJudgement();
  result.shot_judgements.front().part = static_cast<augusta::body_part::BodyPart>(99);
  std::vector<Sent> sent;

  const auto handed =
      ForEachTickMessage(result, 42, ThreePlayers(Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements)),
                         [&sent](PeerId peer, const Outgoing& message) {
                           sent.push_back(Sent{.peer = peer, .payload = {}, .type = message.Type()});
                           return true;
                         });

  ASSERT_FALSE(handed.has_value());
  EXPECT_EQ(handed.error().code, augusta::failure::Code::kInvariantViolated);
  EXPECT_TRUE(sent.empty());
}

// How far into a round's flight an entry is travels in 16 bits (ADR-0038): a
// round never flies that long, but the most is held there.
// Requirements: US-30
TEST(TickMessagesDebugTest, AShotJudgementsEntryLaterThanItsFieldCarriesIsHeldToTheMost) {
  TickResult result = ATickWithAShotJudgement();
  result.shot_judgements.front().entries.back().ticks_after_shot = 70000;

  const std::vector<Sent> sent =
      SendAll(result, ThreePlayers(Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements)));

  const std::vector<Sent> judgements = ShotJudgementsOf(sent);
  ASSERT_EQ(judgements.size(), 1U);
  const auto decoded = augusta::protocol::Decode(judgements.front().payload);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(std::get<augusta::protocol::ShotJudgementWire>(*decoded).entries.back().ticks_after_shot, 65535);
}

// Requirements: US-30
TEST(DebugSubscriptionsTest, AStreamGoesToAPeerOnlyWhileTheServerServesItAndThePeerAskedForIt) {
  const DebugSubscriptions served = Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements);
  const DebugSubscriptions unserved = Subscribed(false, {kPeer1}, DebugSubscribe::kShotJudgements);
  DebugSubscriptions cleared = Subscribed(true, {kPeer1}, DebugSubscribe::kShotJudgements);
  static_cast<void>(cleared.Subscribe(kPeer1, 0));

  EXPECT_TRUE(served.Wants(kPeer1, DebugSubscribe::kShotJudgements));
  EXPECT_FALSE(served.Wants(kPeer2, DebugSubscribe::kShotJudgements));
  EXPECT_FALSE(unserved.Wants(kPeer1, DebugSubscribe::kShotJudgements));
  EXPECT_FALSE(cleared.Wants(kPeer1, DebugSubscribe::kShotJudgements));
}

}  // namespace
