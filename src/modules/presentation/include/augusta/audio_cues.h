#ifndef AUGUSTA_AUDIO_CUES_H_
#define AUGUSTA_AUDIO_CUES_H_

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

// What PresentationWorld's AudioCues phase plays (ADR-0024): which of a frame's
// events give which cue, and where each is heard from. Every cue is a one-shot.
//
// No clock, no ECS, no audio device - so it is tested on its own;
// PresentationWorld hands the cues to augusta::audio::Engine (see
// presentation.cpp).
namespace augusta::presentation {

/// One cue to play this frame.
struct CuePlay {
  audio::Cue cue = audio::Cue::kGunshot;
  /// Where it is heard from, spatialized against the listener; nullopt for a
  /// cue heard as the listener's own, from no direction.
  std::optional<math::Vec3> position;

  friend bool operator==(const CuePlay&, const CuePlay&) = default;
};

/// The listener every cue is heard against: the ears at camera, facing where it
/// looks.
[[nodiscard]] audio::Listener ListenerOf(const Camera& camera);

/// Which cues each frame plays. Remembers what a frame's events are heard
/// against: the local player's health as of the previous frame, where the
/// server last reported each body, and whether the last Match end was heard.
class CueSelector {
 public:
  /// The cues of the frame shown from input, in which rounds_fired is how many
  /// rounds the local player's predicted fire fired since the previous frame
  /// (FiredRounds, effects.h).
  [[nodiscard]] std::vector<CuePlay> Select(const FrameInput& input, std::uint32_t rounds_fired);

 private:
  // Whether health is lower than the previous frame's; remembers it.
  [[nodiscard]] bool HitTaken(const std::optional<float>& health);
  // Remembers where snapshot, if any, reports each body.
  void RememberPositions(const std::optional<WorldSnapshot>& snapshot);
  // The stinger to play if match_end arrived since the previous frame.
  [[nodiscard]] std::optional<audio::Cue> MatchEndStinger(const std::optional<MatchEnd>& match_end,
                                                          const std::optional<EntityId>& local_entity);

  std::optional<float> health_;
  // Kept after a body leaves the updates: its Death can arrive later.
  std::map<EntityId, math::Vec3> last_positions_;
  bool match_end_heard_ = false;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_AUDIO_CUES_H_
