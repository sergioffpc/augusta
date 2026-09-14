#ifndef AUGUSTA_AUDIO_H_
#define AUGUSTA_AUDIO_H_

#include <cstdint>
#include <string>

#include "augusta/math.h"

// augusta::audio plays spatialized one-shot sound effects on the Windows
// client (ARCHITECTURE.md §7's Audio: "consumes presentation state").
// Two third-party libraries split the work, neither of which alone does
// what a game needs: Steam Audio (ADR-0010) only processes audio buffers
// already in memory - it spatializes a voice via HRTF but opens no
// output device, decodes no files, and mixes no voices; miniaudio
// (ADR-0028) is what actually owns the output device, decodes mono PCM
// (ADR-0020), and mixes simultaneous voices into the output stream. This
// module hides that split entirely: Engine spatializes each voice via
// Steam Audio, then hands the result to miniaudio.
//
// Scope, for now: one-shot spatialized SFX only (the fire/footstep/
// death/round-end stingers ARCHITECTURE.md's AudioCues phase describes -
// ROADMAP.md doesn't meaningfully exercise this module before M4). No
// looping ambience/music - add it if a future milestone needs it.
//
// Like augusta::networking, Engine's methods don't require a specific
// calling thread: miniaudio's actual mixing runs on its own
// library-owned audio callback thread (not one of this engine's three
// fixed threads, ARCHITECTURE.md §8/ADR-0005), and its public API is
// designed to be safe to call while that thread is running. In practice
// PresentationWorld's AudioCues phase (Main/Render thread, per frame)
// is what calls SetListener/PlaySound.
namespace augusta::audio {

// The listener's world-space pose (the local player's ears), consumed by
// Steam Audio's HRTF spatialization for every voice played after it.
struct Listener {
  math::Vec3 position;
  // forward and up together define listener orientation; must be
  // non-zero, need not be pre-normalized.
  math::Vec3 forward;
  math::Vec3 up;
};

// Opaque handle to a loaded sound asset. Valid for the lifetime of the
// Engine that returned it or until UnloadSound is called with it.
enum class SoundHandle : std::uint32_t {};

// Opaque handle to one playing instance of a sound, returned by
// PlaySound. Valid until playback finishes naturally or StopVoice is
// called with it, whichever comes first - there is no need to hold onto
// or clean up a handle for a voice you don't intend to stop early.
enum class VoiceHandle : std::uint32_t {};

// Owns the output device and every currently-loaded sound/playing voice.
// The client constructs exactly one.
class Engine {
 public:
  // Opens the output device and initializes Steam Audio's HRTF context.
  // Throws std::runtime_error if either fails - there is no recoverable
  // path for a client that can't open an audio device.
  Engine();

  // Loads the mono PCM sound at asset_path (a path relative to the
  // signed asset pack's root, per ARCHITECTURE.md §8 - not a filesystem
  // path; pack loading itself is M5/ADR-0018 work, not yet designed, so
  // this module's implementation will depend on that once it exists).
  // Throws std::runtime_error if the asset is missing or isn't mono PCM
  // (ADR-0020).
  [[nodiscard]] SoundHandle LoadSound(const std::string& asset_path);

  // Stops and invalidates any voices currently playing sound, then frees
  // it. Any handle previously returned by PlaySound(sound, ...) becomes
  // invalid.
  void UnloadSound(SoundHandle sound);

  // Sets the pose every subsequent PlaySound call spatializes against,
  // until the next SetListener call. Call once per frame (AudioCues)
  // before playing that frame's cues.
  void SetListener(const Listener& listener);

  // Starts playing one instance of sound at world_position, spatialized
  // against the most recent SetListener call. The voice frees itself
  // when playback finishes; the returned handle is only needed to stop
  // it early.
  VoiceHandle PlaySound(SoundHandle sound, const math::Vec3& world_position);

  // Stops a still-playing voice immediately. A no-op if it already
  // finished naturally.
  void StopVoice(VoiceHandle voice);
};

}  // namespace augusta::audio

#endif  // AUGUSTA_AUDIO_H_
