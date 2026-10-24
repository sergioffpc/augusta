#include "augusta/link_mapping.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <boost/uuid/uuid.hpp>

#include "augusta/link_types.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/simulation_mapping.h"
#include "augusta/tick.h"

namespace augusta::host {

link::EntityId ToLink(EntityId entity) { return static_cast<link::EntityId>(std::to_underlying(entity)); }

link::JoinRefusal ToLink(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return link::JoinRefusal::kVersionMismatch;
    case JoinRefusal::kLobbyFull:
      return link::JoinRefusal::kLobbyFull;
    case JoinRefusal::kUnknownCharacter:
      return link::JoinRefusal::kUnknownCharacter;
    case JoinRefusal::kMatchInProgress:
      return link::JoinRefusal::kMatchInProgress;
    case JoinRefusal::kPackMismatch:
      return link::JoinRefusal::kPackMismatch;
    case JoinRefusal::kReplayServer:
      return link::JoinRefusal::kReplayServer;
    case JoinRefusal::kUnknownCapture:
      return link::JoinRefusal::kUnknownCapture;
    case JoinRefusal::kNotAReplayServer:
      return link::JoinRefusal::kNotAReplayServer;
    case JoinRefusal::kReenactmentsNotAccepted:
      return link::JoinRefusal::kReenactmentsNotAccepted;
  }
  // A corrupted refusal goes on as the number it holds, which no case of the
  // Link's names either (ADR-0033).
  return static_cast<link::JoinRefusal>(std::to_underlying(reason));
}

link::Admission ToLink(const Admission& admission, std::uint8_t tick_rate_hz,
                       const parameters::Parameters& parameters) {
  return link::Admission{.session = admission.session,
                         .tick_rate_hz = tick_rate_hz,
                         .parameters = parameters,
                         .character = admission.character};
}

link::Lobby ToLink(const Roster& roster) {
  link::Lobby lobby{.version = roster.version, .roster = {}};
  lobby.roster.reserve(roster.players.size());
  for (const RosterEntry& entry : roster.players) {
    lobby.roster.push_back(link::RosterEntry{.session = entry.session, .character = entry.character});
  }
  return lobby;
}

link::MatchStart ToLink(const MatchStart& start, std::span<const math::Vec3> spawns, tick::Tick first_tick,
                        const boost::uuids::uuid& match) {
  link::MatchStart result{.players = {}, .first_tick = first_tick, .match = match};
  result.players.reserve(start.players.size());
  for (std::size_t i = 0; i < start.players.size(); ++i) {
    const MatchPlayer& player = start.players[i];
    result.players.push_back(link::MatchPlayer{
        .session = player.session, .entity = ToLink(player.entity), .character = player.character, .spawn = spawns[i]});
  }
  return result;
}

link::MatchEnd ToLink(const MatchEnd& end) { return link::MatchEnd{.winner = end.winner}; }

link::AuthoritativeState ToLink(const replication::Updates& updates) {
  link::AuthoritativeState state{.tick = updates.tick, .bodies = {}};
  state.bodies.reserve(updates.bodies.size());
  for (const replication::EntityBody& body : updates.bodies) {
    state.bodies.push_back(link::EntityBody{.entity = ToLink(FromSimulation(body.entity)),
                                            .body = body.body,
                                            .yaw = body.yaw,
                                            .pitch = body.pitch,
                                            .ads = body.ads});
  }
  return state;
}

void FillRecipientFields(link::AuthoritativeState& state, const replication::RecipientUpdate& recipient) {
  state.acknowledged_sequence = recipient.acknowledged_sequence;
  state.rifle = recipient.rifle;
  state.health = recipient.health;
  state.queued_commands = recipient.queued_commands;
}

link::Shot ToLink(const replication::Shot& shot) {
  return link::Shot{.shooter = ToLink(FromSimulation(shot.shooter)),
                    .tick = shot.tick,
                    .origin = shot.origin,
                    .yaw = shot.yaw,
                    .pitch = shot.pitch};
}

link::HitConfirmation ToLink(const replication::HitConfirmation& hit) {
  return link::HitConfirmation{.target = ToLink(FromSimulation(hit.target)), .damage = hit.damage, .part = hit.part};
}

link::Death ToLink(const replication::Death& death) {
  return link::Death{.victim = ToLink(FromSimulation(death.victim)),
                     .killer = ToLink(FromSimulation(death.killer)),
                     .yaw = death.yaw,
                     .pitch = death.pitch,
                     .part = death.part};
}

#if AUGUSTA_DEBUG_TOOLS
namespace {

link::Struck ToLink(simulation::Struck struck) {
  switch (struck) {
    case simulation::Struck::kNothing:
      return link::Struck::kNothing;
    case simulation::Struck::kMap:
      return link::Struck::kMap;
    case simulation::Struck::kProp:
      return link::Struck::kProp;
    case simulation::Struck::kHitbox:
      return link::Struck::kHitbox;
  }
  // A corrupted one goes on as the number it holds, which the Link then
  // refuses to encode (ADR-0033).
  return static_cast<link::Struck>(std::to_underlying(struck));
}

}  // namespace

link::ShotJudgement ToLink(const simulation::ShotJudgement& judgement) {
  link::ShotJudgement result{
      .shot_tick = judgement.shot_tick,
      .shooters_delay = judgement.shooters_delay,
      .struck = ToLink(judgement.struck),
      .part = judgement.part,
      .point = judgement.point,
      .target = judgement.target.has_value() ? std::optional(ToLink(FromSimulation(*judgement.target))) : std::nullopt,
      .entries = {},
      .entries_left_out = judgement.entries_left_out,
  };
  result.entries.reserve(judgement.entries.size());
  for (const simulation::ShotJudgementEntry& entry : judgement.entries) {
    link::ShotJudgementEntry& mapped = result.entries.emplace_back(
        link::ShotJudgementEntry{.ticks_after_shot = static_cast<std::uint16_t>(std::min<std::uint32_t>(
                                     entry.ticks_after_shot, std::numeric_limits<std::uint16_t>::max())),
                                 .players = {}});
    mapped.players.reserve(entry.players.size());
    for (const simulation::JudgedPlayer& player : entry.players) {
      mapped.players.push_back(link::JudgedPlayer{.entity = ToLink(FromSimulation(player.entity)),
                                                  .position = player.position,
                                                  .stance = player.stance,
                                                  .yaw = player.yaw});
    }
  }
  return result;
}
#endif

}  // namespace augusta::host
