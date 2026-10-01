#include <cstddef>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/protocol.h"

// The protocol_decode fuzz target's seeds (tests/fuzz/README.md): one committed
// payload per message kind, so the fuzzer starts from valid messages rather
// than from nothing. They are what Encode writes today, so a change to the wire
// fails here until the seeds are regenerated, and a new message kind fails
// until it has one.
namespace {

using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BodyPartWire;
using augusta::protocol::BodyStateWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::DeathWire;
using augusta::protocol::Encode;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::HitConfirmationWire;
using augusta::protocol::JoinAcceptedWire;
using augusta::protocol::JoinRefusalWire;
using augusta::protocol::JoinRefusedWire;
using augusta::protocol::JoinRequestWire;
using augusta::protocol::LobbyWire;
using augusta::protocol::MatchEndWire;
using augusta::protocol::MatchPlayerWire;
using augusta::protocol::MatchStartWire;
using augusta::protocol::MessageWire;
using augusta::protocol::ParametersWire;
using augusta::protocol::ReadyWire;
using augusta::protocol::RosterEntryWire;
using augusta::protocol::SequencedCommandWire;
using augusta::protocol::SessionIdWire;
using augusta::protocol::ShotWire;
using augusta::protocol::StaminaWire;
using augusta::protocol::StanceWire;
using augusta::protocol::WeaponStateWire;

struct Seed {
  std::string name;
  MessageWire message;
};

// One message of each kind, with every list non-empty and every flag in use
// somewhere, so each field's encoding is in the corpus.
std::vector<Seed> Seeds() {
  const BodyStateWire body{.position = Vec3(12.5F, 1.75F, -40.0F),
                           .velocity = Vec3(3.0F, -0.5F, 4.25F),
                           .stamina = 0.25F,
                           .flags = BodyStateWire::kExhausted,
                           .stance = StanceWire::kCrouching};
  const CommandWire command{.direction = Vec3(0.6F, 0.0F, -0.8F),
                            .yaw = 1.5F,
                            .pitch = -0.25F,
                            .view_fraction = 0.75F,
                            .flags = CommandWire::kSprint | CommandWire::kFire,
                            .desired_stance = StanceWire::kProne,
                            .view_age = 1};
  return {
      {.name = "join_request",
       .message = JoinRequestWire{.engine_version = "0.1.0", .client_pack = {}, .character = "characters/player"}},
      {.name = "join_accepted",
       .message = JoinAcceptedWire{.session = static_cast<SessionIdWire>(7),
                                   .tick_rate_hz = 60,
                                   .parameters = ParametersWire{.stamina = StaminaWire{.deplete_per_second = 0.2F,
                                                                                       .regen_per_second = 0.1F,
                                                                                       .forced_walk_below = 0.05F},
                                                                .player_count = 2},
                                   .character = 1}},
      {.name = "join_refused", .message = JoinRefusedWire{.reason = JoinRefusalWire::kPackMismatch}},
      {.name = "commands",
       .message = CommandsWire{.commands = {SequencedCommandWire{.sequence = 41, .command = CommandWire{}},
                                            SequencedCommandWire{.sequence = 42, .command = command}},
                               .view_tick = 1194}},
      {.name = "authoritative_state",
       .message =
           AuthoritativeStateWire{
               .tick = 1200,
               .acknowledged_sequence = 42,
               .bodies = {EntityStateWire{.entity = static_cast<EntityIdWire>(1), .body = body, .yaw = 1.5F},
                          EntityStateWire{.entity = static_cast<EntityIdWire>(2), .body = BodyStateWire{}}},
               .rifle = WeaponStateWire{.cooldown = 0.0625F,
                                        .reload_remaining = 1.75F,
                                        .recoil_pitch = 0.046875F,
                                        .recoil_yaw = -0.00390625F,
                                        .rounds = 12,
                                        .burst_index = 3},
               .health = 37.25F,
               .queued_commands = 2}},
      {.name = "lobby",
       .message = LobbyWire{.version = 3,
                            .roster = {RosterEntryWire{.session = static_cast<SessionIdWire>(7), .character = 1},
                                       RosterEntryWire{.session = static_cast<SessionIdWire>(9), .character = 2}}}},
      {.name = "ready", .message = ReadyWire{.version = 3}},
      {.name = "match_start",
       .message = MatchStartWire{.players = {MatchPlayerWire{.spawn = Vec3(-8.0F, 0.0F, 16.5F),
                                                             .session = static_cast<SessionIdWire>(7),
                                                             .entity = static_cast<EntityIdWire>(1),
                                                             .character = 1},
                                             MatchPlayerWire{.spawn = Vec3(8.0F, 0.0F, -16.5F),
                                                             .session = static_cast<SessionIdWire>(9),
                                                             .entity = static_cast<EntityIdWire>(2),
                                                             .character = 2}}}},
      {.name = "match_end", .message = MatchEndWire{.winner = static_cast<SessionIdWire>(3)}},
      {.name = "shot",
       .message = ShotWire{.origin = Vec3(12.5F, 1.75F, -40.0F),
                           .shooter = static_cast<EntityIdWire>(2),
                           .tick = 1200,
                           .yaw = 1.5F,
                           .pitch = -0.25F}},
      {.name = "hit_confirmation",
       .message =
           HitConfirmationWire{.target = static_cast<EntityIdWire>(2), .damage = 37.5F, .part = BodyPartWire::kHead}},
      {.name = "death",
       .message = DeathWire{.victim = static_cast<EntityIdWire>(2),
                            .killer = static_cast<EntityIdWire>(1),
                            .yaw = 1.5F,
                            .pitch = -0.25F,
                            .part = BodyPartWire::kTorso}},
  };
}

std::string SeedPath(const Seed& seed) { return std::string(AUGUSTA_PROTOCOL_DECODE_CORPUS) + "/" + seed.name; }

BytesWire ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  BytesWire payload;
  for (const char byte : bytes) {
    payload.push_back(static_cast<std::byte>(byte));
  }
  return payload;
}

TEST(ProtocolDecodeSeedsTest, EveryMessageKindHasASeed) {
  std::set<std::size_t> kinds;
  for (const Seed& seed : Seeds()) {
    kinds.insert(seed.message.index());
  }
  EXPECT_EQ(kinds.size(), std::variant_size_v<MessageWire>);
}

TEST(ProtocolDecodeSeedsTest, EverySeedIsWhatEncodeWrites) {
  for (const Seed& seed : Seeds()) {
    SCOPED_TRACE(seed.name);
    EXPECT_EQ(ReadFile(SeedPath(seed)), Encode(seed.message)) << "regenerate the seeds (tests/fuzz/README.md)";
  }
}

// Not a check: rewrites the seeds from Seeds() when the wire changes on purpose.
// Disabled, so ctest never runs it; the augusta_protocol_decode_seeds build
// target does (tests/fuzz/CMakeLists.txt).
TEST(ProtocolDecodeSeedsTest, DISABLED_RegenerateSeeds) {
  for (const Seed& seed : Seeds()) {
    const BytesWire payload = Encode(seed.message);
    std::ofstream out(SeedPath(seed), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    ASSERT_TRUE(out.good()) << SeedPath(seed);
  }
}

}  // namespace
