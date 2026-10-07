#include "frame_mapping.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "augusta/command.h"
#include "augusta/effects.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"
#include "augusta/tracers.h"

namespace augusta::client {

renderer::RemotePlayer ToRenderer(const presentation::RemotePlayer& remote) {
  return {.position = remote.body.position, .yaw = remote.body.yaw, .character = remote.character};
}

renderer::Camera ToRenderer(const presentation::Camera& camera) {
  return {.position = camera.position, .rotation = camera.rotation, .vertical_fov = camera.vertical_fov};
}

std::vector<renderer::Glow> ToRenderer(const std::vector<presentation::Effect>& effects, float lifetime) {
  std::vector<renderer::Glow> glows;
  glows.reserve(effects.size());
  for (const presentation::Effect& effect : effects) {
    glows.push_back({.position = effect.position, .fade = 1.0F - (effect.age / lifetime)});
  }
  return glows;
}

renderer::CombatEffects CombatEffectsOf(const presentation::State& state) {
  renderer::CombatEffects effects{
      .tracers = {},
      .impacts = ToRenderer(state.impacts, presentation::kImpactSeconds),
      .muzzle_flashes = ToRenderer(state.muzzle_flashes, presentation::kMuzzleFlashSeconds),
  };
  effects.tracers.reserve(state.tracers.size());
  for (const presentation::Tracer& tracer : state.tracers) {
    effects.tracers.push_back({.head = tracer.head, .tail = tracer.tail});
  }
  return effects;
}

presentation::EntityId ToPresentation(harness::EntityId entity) {
  return static_cast<presentation::EntityId>(std::to_underlying(entity));
}

presentation::WorldSnapshot ToPresentation(const harness::AuthoritativeState& state, std::uint8_t tick_rate_hz) {
  presentation::WorldSnapshot snapshot{
      .tick = state.tick,
      .tick_duration = 1.0 / static_cast<double>(tick_rate_hz),
      .bodies = {},
  };
  snapshot.bodies.reserve(state.bodies.size());
  for (const harness::EntityBody& body : state.bodies) {
    snapshot.bodies.push_back({.entity = ToPresentation(body.entity), .state = body.body, .yaw = body.yaw});
  }
  return snapshot;
}

presentation::Shot ToPresentation(const harness::Shot& shot) {
  return {.shooter = ToPresentation(shot.shooter), .origin = shot.origin, .yaw = shot.yaw, .pitch = shot.pitch};
}

presentation::Aim ToPresentation(const input::Aim& aim) { return {.yaw = aim.yaw, .pitch = aim.pitch, .ads = aim.ads}; }

std::optional<presentation::WorldSnapshot> SnapshotOf(const harness::ServerView& view) {
  if (!view.authoritative.has_value() || !view.accepted.has_value()) {
    return std::nullopt;
  }
  return ToPresentation(*view.authoritative, view.accepted->tick_rate_hz);
}

std::vector<presentation::PlayerCharacter> CharactersOf(const std::optional<harness::MatchStart>& match_start) {
  std::vector<presentation::PlayerCharacter> characters;
  if (match_start.has_value()) {
    std::vector<harness::MatchPlayer> players = match_start->players;
    std::ranges::sort(players, {}, &harness::MatchPlayer::session);
    characters.reserve(players.size());
    for (const harness::MatchPlayer& player : players) {
      characters.push_back({.entity = ToPresentation(player.entity), .character = player.character});
    }
  }
  return characters;
}

void ConvertedServerView::Update(const harness::ServerView& view) {
  if (view.matches_started != characters_match_) {
    characters_ = CharactersOf(view.match_start);
    characters_match_ = view.matches_started;
  }
  const bool converted = snapshot_.has_value() && view.authoritative.has_value() &&
                         snapshot_match_ == view.matches_started && snapshot_->tick == view.authoritative->tick;
  if (!converted) {
    snapshot_ = SnapshotOf(view);
    snapshot_match_ = view.matches_started;
  }
}

const presentation::WorldSnapshot* ConvertedServerView::Snapshot() const {
  return snapshot_.has_value() ? &*snapshot_ : nullptr;
}

std::span<const presentation::PlayerCharacter> ConvertedServerView::Characters() const { return characters_; }

std::optional<presentation::MatchEnd> MatchEndOf(const harness::ServerView& view) {
  return view.match_end.transform([&view](const harness::MatchEnd& end) {
    presentation::MatchEnd match_end;
    const std::optional<harness::MatchStart>& start = view.match_start;
    if (end.winner.has_value() && start.has_value()) {
      for (const harness::MatchPlayer& player : start->players) {
        if (player.session == *end.winner) {
          match_end.winner = ToPresentation(player.entity);
          break;
        }
      }
    }
    return match_end;
  });
}

command::Command WithSeenTime(command::Command command, const std::optional<presentation::SeenTime>& seen_time) {
  if (seen_time.has_value()) {
    command.seen_tick = seen_time->tick;
    command.seen_fraction = seen_time->fraction;
  }
  return command;
}

}  // namespace augusta::client
