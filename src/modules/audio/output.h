#ifndef AUGUSTA_AUDIO_OUTPUT_H_
#define AUGUSTA_AUDIO_OUTPUT_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "augusta/assets.h"
#include "augusta/audio.h"
#include "augusta/math.h"

/// \file
/// audio::Engine's open output device: what plays once there is one. Engine
/// (audio.cpp) holds none when OpenOutput fails, and is silent. A build with
/// AUGUSTA_AUDIO_OUTPUT opens one through miniaudio and Steam Audio
/// (output_miniaudio.cpp), both cross-platform; one without has no audio
/// output at all (output_none.cpp), and no audio dependency (ADR-0010).
namespace augusta::audio {

/// Which step of opening the output device failed.
enum class OutputStep : std::uint8_t {
  /// This build has no audio output.
  kUnsupported,
  /// Steam Audio's context.
  kSteamAudioContext,
  /// Steam Audio's HRTF.
  kHrtf,
  /// miniaudio's output device.
  kDevice,
  /// Starting the output device.
  kDeviceStart,
};

/// Why no output device opened: the step, and the library's own result code
/// for it (0 for kUnsupported).
struct OutputError {
  OutputStep step = OutputStep::kUnsupported;
  std::int32_t code = 0;
};

/// A message for error fit for the log.
[[nodiscard]] std::string DescribeOutputError(const OutputError& error);

/// An open output device: the methods of Engine's of the same names.
class Output {
 public:
  Output() = default;
  virtual ~Output() = default;

  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;

  [[nodiscard]] virtual SoundHandle LoadSound(const assets::AudioData& sound) = 0;
  virtual void UnloadSound(SoundHandle sound) = 0;
  virtual void SetListener(const Listener& listener) = 0;
  /// Plays sound at position, or as the listener's own when nullopt.
  virtual VoiceHandle Play(SoundHandle sound, const std::optional<math::Vec3>& position) = 0;
  virtual void Stop(VoiceHandle voice) = 0;
};

/// Opens the default output device, or says why it could not.
[[nodiscard]] std::expected<std::unique_ptr<Output>, OutputError> OpenOutput();

}  // namespace augusta::audio

#endif  // AUGUSTA_AUDIO_OUTPUT_H_
