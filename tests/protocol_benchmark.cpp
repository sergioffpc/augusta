#include <cstddef>
#include <span>

#include <benchmark/benchmark.h>

#include "augusta/math.h"
#include "augusta/primitives.h"
#include "augusta/protocol.h"

// The two messages the server handles every tick for every player (ADR-0038):
// each client's Authoritative State going out, its Commands coming in. Each is
// as large as the protocol allows, so these time its busiest tick.
namespace {

using augusta::math::Vec3;
using augusta::protocol::AuthoritativeStateWire;
using augusta::protocol::BytesWire;
using augusta::protocol::CommandsWire;
using augusta::protocol::CommandWire;
using augusta::protocol::EntityIdWire;
using augusta::protocol::EntityStateWire;
using augusta::protocol::MessageWire;
using augusta::protocol::SequencedCommandWire;

AuthoritativeStateWire FullAuthoritativeState() {
  AuthoritativeStateWire message;
  message.tick = 123'456;
  for (std::size_t player = 0; player < augusta::primitives::kMaxPlayers; ++player) {
    EntityStateWire entity;
    entity.entity = static_cast<EntityIdWire>(player + 1);
    entity.body.position = Vec3(static_cast<float>(player) * 3.0F, 0.0F, -12.5F);
    entity.body.velocity = Vec3(1.25F, 0.0F, -4.5F);
    entity.body.stamina = 0.75F;
    entity.yaw = 1.5F;
    message.bodies.push_back(entity);
  }
  message.rifle.rounds = 17;
  message.health = 66.0F;
  message.acknowledged_sequence = 4'321;
  message.queued_commands = 2;
  return message;
}

CommandsWire FullCommands() {
  CommandsWire message;
  message.seen_tick = 123'450;
  for (std::size_t index = 0; index < augusta::primitives::kMaxCommandsPerMessage; ++index) {
    SequencedCommandWire command;
    command.sequence = 4'322 + index;
    command.command.direction = Vec3(0.0F, 0.0F, -1.0F);
    command.command.yaw = 1.5F;
    command.command.pitch = -0.1F;
    command.command.flags = CommandWire::kSprint | CommandWire::kFire;
    message.commands.push_back(command);
  }
  return message;
}

void BM_ProtocolEncode(benchmark::State& state, const MessageWire& message) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(augusta::protocol::Encode(message));
  }
}
BENCHMARK_CAPTURE(BM_ProtocolEncode, authoritative_state, MessageWire{FullAuthoritativeState()});
BENCHMARK_CAPTURE(BM_ProtocolEncode, commands, MessageWire{FullCommands()});

void BM_ProtocolDecode(benchmark::State& state, const MessageWire& message) {
  const BytesWire payload = augusta::protocol::Encode(message);
  if (!augusta::protocol::Decode(payload).has_value()) {
    state.SkipWithError("the encoded message does not decode");
    return;
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(augusta::protocol::Decode(std::span<const std::byte>(payload)));
  }
}
BENCHMARK_CAPTURE(BM_ProtocolDecode, authoritative_state, MessageWire{FullAuthoritativeState()});
BENCHMARK_CAPTURE(BM_ProtocolDecode, commands, MessageWire{FullCommands()});

}  // namespace
