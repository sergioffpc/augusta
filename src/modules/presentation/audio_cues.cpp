#include "augusta/audio_cues.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "augusta/audio.h"
#include "augusta/cues.h"
#include "augusta/local_view.h"
#include "augusta/math.h"
#include "augusta/presentation.h"

namespace augusta::presentation {

audio::Listener ListenerOf(const Camera& camera) {
  return audio::Listener{.position = camera.position,
                         .forward = camera.rotation * math::Vec3(0.0F, 0.0F, -1.0F),
                         .up = camera.rotation * math::Vec3(0.0F, 1.0F, 0.0F)};
}

std::vector<CuePlay> SelectCues(const FrameInput& input, std::uint32_t rounds_fired) {
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
  return cues;
}

}  // namespace augusta::presentation
