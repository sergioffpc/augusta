#include "augusta/link_mapping.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "augusta/link_types.h"
#include "augusta/match.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/replay.h"
#include "augusta/replication.h"
#include "augusta/simulation_mapping.h"
#include "augusta/tick.h"
#include "augusta/uuid.h"

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
                        const uuid::Uuid& match) {
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
    state.bodies.push_back(
        link::EntityBody{.entity = ToLink(FromSimulation(body.entity)), .body = body.body, .yaw = body.yaw});
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

link::ReplayView ToLink(const std::vector<PlayerView>& views, tick::Tick tick) {
  link::ReplayView view{.tick = tick, .players = {}};
  view.players.reserve(views.size());
  for (const PlayerView& player : views) {
    view.players.push_back(link::PlayerView{.entity = ToLink(player.entity), .pitch = player.pitch, .ads = player.ads});
  }
  return view;
}

}  // namespace augusta::host
