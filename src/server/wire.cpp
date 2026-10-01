#include "wire.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/tick.h"
#include "command_queue.h"
#include "host.h"
#include "match.h"

namespace augusta::server {

namespace {

// The protocol numbers its stances as the engine does; a stance added to one
// and not the other breaks the build here, not the wire.
static_assert(static_cast<std::uint8_t>(physics::Stance::kStanding) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kStanding));
static_assert(static_cast<std::uint8_t>(physics::Stance::kCrouching) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kCrouching));
static_assert(static_cast<std::uint8_t>(physics::Stance::kProne) ==
              static_cast<std::uint8_t>(protocol::StanceWire::kProne));

protocol::StanceWire ToWire(physics::Stance stance) { return static_cast<protocol::StanceWire>(stance); }

physics::Stance FromWire(protocol::StanceWire stance) { return static_cast<physics::Stance>(stance); }

// The protocol carries a pack's hash as the assets module computes it.
static_assert(protocol::kPackHashSize == assets::kPackHashSize);

protocol::BodyPartWire ToWire(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return protocol::BodyPartWire::kHead;
    case ballistics::BodyPart::kTorso:
      return protocol::BodyPartWire::kTorso;
    case ballistics::BodyPart::kLimb:
      return protocol::BodyPartWire::kLimb;
  }
  std::unreachable();
}

}  // namespace

protocol::EntityIdWire ToWire(EntityId entity) {
  return static_cast<protocol::EntityIdWire>(static_cast<std::uint32_t>(entity));
}

protocol::SessionIdWire ToWire(SessionId session) {
  return static_cast<protocol::SessionIdWire>(static_cast<std::uint32_t>(session));
}

protocol::JoinRefusalWire ToWire(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return protocol::JoinRefusalWire::kVersionMismatch;
    case JoinRefusal::kLobbyFull:
      return protocol::JoinRefusalWire::kLobbyFull;
    case JoinRefusal::kUnknownCharacter:
      return protocol::JoinRefusalWire::kUnknownCharacter;
    case JoinRefusal::kMatchInProgress:
      return protocol::JoinRefusalWire::kMatchInProgress;
    case JoinRefusal::kPackMismatch:
      return protocol::JoinRefusalWire::kPackMismatch;
  }
  std::unreachable();
}

protocol::JoinAcceptedWire ToWire(const Admission& admission, std::uint8_t tick_rate_hz,
                                  const parameters::Parameters& parameters) {
  return protocol::JoinAcceptedWire{
      .session = ToWire(admission.session),
      .tick_rate_hz = tick_rate_hz,
      .parameters = ToWire(parameters),
      .character = admission.character,
  };
}

protocol::BodyStateWire ToWire(const physics::BodyState& body) {
  return protocol::BodyStateWire{
      .position = body.position,
      .velocity = body.velocity,
      .stamina = body.stamina,
      .flags = body.exhausted ? protocol::BodyStateWire::kExhausted : std::uint8_t{0},
      .stance = ToWire(body.stance),
  };
}

protocol::ParametersWire ToWire(const parameters::Parameters& parameters) {
  const parameters::Rifle& rifle = parameters.rifle;
  protocol::RifleWire rifle_wire{
      .rounds_per_minute = rifle.rounds_per_minute,
      .muzzle_velocity = rifle.muzzle_velocity,
      .reload_seconds = rifle.reload_seconds,
      .recoil_recovery_per_second = rifle.recoil_recovery_per_second,
      .ads_recoil_scale = rifle.ads_recoil_scale,
      .ads_field_of_view = rifle.ads_field_of_view,
      .recoil_pattern = {},
      .magazine_capacity = rifle.magazine_capacity,
  };
  rifle_wire.recoil_pattern.reserve(rifle.recoil_pattern.size());
  for (const parameters::RecoilKick& kick : rifle.recoil_pattern) {
    rifle_wire.recoil_pattern.push_back(protocol::RecoilKickWire{.pitch = kick.pitch, .yaw = kick.yaw});
  }
  const parameters::Ammo& ammo = parameters.ammo;
  return protocol::ParametersWire{
      .stamina =
          {
              .deplete_per_second = parameters.stamina.deplete_per_second,
              .regen_per_second = parameters.stamina.regen_per_second,
              .forced_walk_below = parameters.stamina.forced_walk_below,
          },
      .rifle = std::move(rifle_wire),
      .ammo =
          {
              .gravity = ammo.gravity,
              .max_range = ammo.max_range,
              .head_damage = ammo.damage.head,
              .torso_damage = ammo.damage.torso,
              .limb_damage = ammo.damage.limb,
          },
      .starting_health = parameters.starting_health,
      .player_count = parameters.player_count,
  };
}

protocol::LobbyWire ToWire(const Roster& roster) {
  protocol::LobbyWire lobby{.version = roster.version, .roster = {}};
  lobby.roster.reserve(roster.players.size());
  for (const RosterEntry& entry : roster.players) {
    lobby.roster.push_back(protocol::RosterEntryWire{.session = ToWire(entry.session), .character = entry.character});
  }
  return lobby;
}

protocol::MatchStartWire ToWire(const MatchStart& start) {
  protocol::MatchStartWire message;
  message.players.reserve(start.players.size());
  for (const MatchPlayer& player : start.players) {
    message.players.push_back(protocol::MatchPlayerWire{.spawn = player.spawn,
                                                        .session = ToWire(player.session),
                                                        .entity = ToWire(player.entity),
                                                        .character = player.character});
  }
  return message;
}

protocol::AuthoritativeStateWire ToWire(const replication::Update& update) {
  protocol::AuthoritativeStateWire state{
      .tick = update.tick,
      .bodies = {},
      .rifle = {.cooldown = update.rifle.cooldown,
                .reload_remaining = update.rifle.reload_remaining,
                .recoil_pitch = update.rifle.recoil.pitch,
                .recoil_yaw = update.rifle.recoil.yaw,
                .rounds = update.rifle.rounds,
                .burst_index = update.rifle.burst_index},
      .health = update.health,
      .acknowledged_sequence = update.acknowledged_sequence,
      .queued_commands = update.queued_commands,
  };
  state.bodies.reserve(update.bodies.size());
  for (const replication::EntityBody& body : update.bodies) {
    state.bodies.push_back(protocol::EntityStateWire{
        .entity = ToWire(FromSimulation(body.entity)), .body = ToWire(body.body), .yaw = body.yaw});
  }
  return state;
}

protocol::ShotWire ToWire(const replication::Shot& shot) {
  return protocol::ShotWire{
      .tick = shot.tick,
      .origin = shot.origin,
      .shooter = ToWire(FromSimulation(shot.shooter)),
      .yaw = shot.yaw,
      .pitch = shot.pitch,
  };
}

protocol::HitConfirmationWire ToWire(const replication::HitConfirmation& hit) {
  return protocol::HitConfirmationWire{
      .target = ToWire(FromSimulation(hit.target)),
      .damage = hit.damage,
      .part = ToWire(hit.part),
  };
}

protocol::DeathWire ToWire(const replication::Death& death) {
  return protocol::DeathWire{
      .victim = ToWire(FromSimulation(death.victim)),
      .killer = ToWire(FromSimulation(death.killer)),
      .yaw = death.yaw,
      .pitch = death.pitch,
      .part = ToWire(death.part),
  };
}

assets::PackHash FromWire(const protocol::PackHashWire& hash) {
  assets::PackHash result{};
  std::ranges::copy(hash, result.begin());
  return result;
}

JoinRequest FromWire(const protocol::JoinRequestWire& request) {
  return JoinRequest{
      .engine_version = request.engine_version,
      .client_pack = FromWire(request.client_pack),
      .character = request.character,
  };
}

command::Command FromWire(const protocol::CommandWire& command, tick::Tick view_tick) {
  command::Command result;
  result.movement.direction = command.direction;
  result.movement.sprint = (command.flags & protocol::CommandWire::kSprint) != 0;
  result.movement.desired_stance = FromWire(command.desired_stance);
  result.yaw = command.yaw;
  result.pitch = command.pitch;
  result.ads = (command.flags & protocol::CommandWire::kAds) != 0;
  result.fire = (command.flags & protocol::CommandWire::kFire) != 0;
  result.reload = (command.flags & protocol::CommandWire::kReload) != 0;
  result.view_tick = view_tick - std::min<tick::Tick>(command.view_age, view_tick);
  result.view_fraction = command.view_fraction;
  return result;
}

SequencedCommand FromWire(const protocol::SequencedCommandWire& command, tick::Tick view_tick) {
  return SequencedCommand{.sequence = command.sequence, .command = FromWire(command.command, view_tick)};
}

std::vector<SequencedCommand> FromWire(const protocol::CommandsWire& message) {
  std::vector<SequencedCommand> commands;
  commands.reserve(message.commands.size());
  for (const protocol::SequencedCommandWire& command : message.commands) {
    commands.push_back(FromWire(command, message.view_tick));
  }
  return commands;
}

}  // namespace augusta::server
