#ifndef AUGUSTA_AUDIO_H_
#define AUGUSTA_AUDIO_H_

#include <cstdint>
#include <memory>

#include "augusta/assets.h"
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
// Scope: one-shot cues only (augusta/cues.h's catalogue, which
// PresentationWorld's AudioCues phase plays). No looping ambience or music,
// no reflection or occlusion.
//
// No output device is not an error. An Engine that cannot open one - no
// device on the machine, or a build with no audio output at all (Linux,
// whose build has no audio dependency, ADR-0010) - logs why at WARN and is
// silent: every method below still works and plays nothing, so the client
// runs the same with or without sound.
//
// Every method is called from one thread (PresentationWorld's, the
// Main/Render thread, ADR-0005). miniaudio mixes on its own library-owned
// audio callback thread (not one of this engine's three fixed threads,
// ADR-0005), which Engine synchronizes with internally.
namespace augusta::audio {

/// The listener's world-space pose (the ears every voice is heard from).
struct Listener {
  math::Vec3 position;
  /// forward and up together define the listener's orientation; must be
  /// non-zero, need not be pre-normalized.
  math::Vec3 forward;
  math::Vec3 up;
};

/// Opaque handle to a loaded sound. Valid for the lifetime of the Engine that
/// returned it or until UnloadSound is called with it.
enum class SoundHandle : std::uint32_t {};

/// Opaque handle to one playing instance of a sound, returned by Play.
/// Valid until playback finishes or StopVoice is called with it, whichever
/// comes first - there is no need to hold onto a voice you won't stop early.
enum class VoiceHandle : std::uint32_t {};

class Output;

/// Owns the output device and every loaded sound and playing voice. The client
/// constructs exactly one; it is neither copied nor moved, since the audio
/// thread holds on to it.
class Engine {
 public:
  /// Opens the default output device and Steam Audio's HRTF; silent, after a
  /// WARN naming why, if it cannot (see the header comment).
  Engine();
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  /// Takes a mono PCM sound (ADR-0020), converted once here to the device's
  /// format and rate.
  [[nodiscard]] SoundHandle LoadSound(const assets::AudioData& sound);

  /// Stops every voice playing sound, then frees it.
  void UnloadSound(SoundHandle sound);

  /// Sets the pose every voice is heard from, until the next call - a voice
  /// already playing is heard from the new pose too. Call once per frame
  /// before playing that frame's cues.
  void SetListener(const Listener& listener);

  /// Starts one instance of sound at world_position, spatialized against the
  /// listener (SetListener). The voice frees itself when playback finishes.
  VoiceHandle Play(SoundHandle sound, const math::Vec3& world_position);

  /// Starts one instance of sound heard as the listener's own: unspatialized,
  /// from no direction.
  VoiceHandle Play(SoundHandle sound);

  /// Stops a still-playing voice immediately. A no-op if it already finished.
  void StopVoice(VoiceHandle voice);

 private:
  // The open device, or nullptr when silent.
  std::unique_ptr<Output> output_;
};

}  // namespace augusta::audio

#endif  // AUGUSTA_AUDIO_H_
