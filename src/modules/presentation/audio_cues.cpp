#include "augusta/audio_cues.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "augusta/audio.h"
#include "augusta/cues.h"
#include "augusta/interpolation.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

namespace augusta::presentation {

audio::Listener ListenerOf(const Camera& camera) {
  return audio::Listener{.position = camera.position,
                         .forward = camera.rotation * math::Vec3(0.0F, 0.0F, -1.0F),
                         .up = camera.rotation * math::Vec3(0.0F, 1.0F, 0.0F)};
}

std::vector<CuePlay> CueSelector::Select(const FrameInput& input, std::uint32_t rounds_fired) {
  // The local player's own gunshot comes from its predicted fire, on the frame
  // it fires - not from its Shot, a round trip later.
  std::vector<CuePlay> cues(rounds_fired, CuePlay{.cue = audio::Cue::kGunshot, .position = std::nullopt});
  for (const Shot& shot : input.shots) {
    if (shot.shooter == input.local_entity) {
      continue;
    }
    cues.push_back(CuePlay{.cue = audio::Cue::kGunshot, .position = shot.origin});
  }
  // Only the server confirms a hit. Hits confirmed together are heard as one
  // marker, as they show as one (HitMarker, local_view.h).
  if (input.hit_confirmations > 0) {
    cues.push_back(CuePlay{.cue = audio::Cue::kHitMarker, .position = std::nullopt});
  }
  if (HitTaken(input.health)) {
    cues.push_back(CuePlay{.cue = audio::Cue::kHitTaken, .position = std::nullopt});
  }
  RememberPositions(input.snapshot);
  // Heard from where the body was, the local player's own included; one never
  // reported has nowhere to be heard from.
  for (const EntityId victim : input.deaths) {
    if (const auto found = last_positions_.find(victim); found != last_positions_.end()) {
      cues.push_back(CuePlay{.cue = audio::Cue::kDeath, .position = found->second});
    }
  }
  if (const std::optional<audio::Cue> stinger = MatchEndStinger(input.match_end, input.local_entity);
      stinger.has_value()) {
    cues.push_back(CuePlay{.cue = *stinger, .position = std::nullopt});
    // After the deaths that ended the match: entities are named anew in the next.
    last_positions_.clear();
  }
  return cues;
}

bool CueSelector::HitTaken(const std::optional<float>& health) {
  const bool dropped = health_.has_value() && health.has_value() && *health < *health_;
  health_ = health;
  return dropped;
}

void CueSelector::RememberPositions(const std::optional<WorldSnapshot>& snapshot) {
  if (snapshot.has_value()) {
    for (const DynamicBody& body : snapshot->bodies) {
      last_positions_.insert_or_assign(body.entity, body.state.position);
    }
  }
}

std::optional<audio::Cue> CueSelector::MatchEndStinger(const std::optional<MatchEnd>& match_end,
                                                       const std::optional<EntityId>& local_entity) {
  // Match end stays told until the next match starts: heard once.
  const bool arrived = match_end.has_value() && !match_end_heard_;
  match_end_heard_ = match_end.has_value();
  if (!arrived) {
    return std::nullopt;
  }
  // Everyone but the winner loses, a draw and a spectator included.
  const bool won = match_end->winner.has_value() && match_end->winner == local_entity;
  return won ? audio::Cue::kMatchWon : audio::Cue::kMatchLost;
}

}  // namespace augusta::presentation
