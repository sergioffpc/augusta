#ifndef AUGUSTA_AUDIO_CUES_H_
#define AUGUSTA_AUDIO_CUES_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "augusta/audio.h"
#include "augusta/cues.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

// What PresentationWorld's AudioCues phase plays (ADR-0024): which of a frame's
// events give which cue, and where each is heard from. Every cue is a one-shot.
//
// Pure - no clock, no ECS, no audio device - so it is tested on its own;
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

/// The cues of one frame shown from input, in which rounds_fired is how many
/// rounds the local player's predicted fire fired since the previous frame
/// (FiredRounds, effects.h).
[[nodiscard]] std::vector<CuePlay> SelectCues(const FrameInput& input, std::uint32_t rounds_fired);

}  // namespace augusta::presentation

#endif  // AUGUSTA_AUDIO_CUES_H_
