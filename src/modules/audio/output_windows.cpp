#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <miniaudio.h>
#include <phonon.h>
#include <phonon_version.h>

#include "augusta/assets.h"
#include "augusta/audio.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "output.h"

// The Windows output device: miniaudio opens it and calls Mix on its own audio
// thread for every period; Mix spatializes each voice through its own Steam
// Audio binaural effect (HRTF) and sums them into the period. The Main/Render
// thread only starts and stops voices and moves the listener, under mutex_.
namespace augusta::audio {

namespace {

constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels = 2;
// One period, and Steam Audio's frame: 256 samples at 48 kHz is 5.3 ms, the
// latency a cue adds on top of the frame that plays it.
constexpr ma_uint32 kFrameSize = 256;
// Voices beyond this many replace the oldest playing: a full Match of rifles on
// full auto stays well under it.
constexpr std::size_t kMaxVoices = 64;
// How far, in meters, a voice is heard at full volume before it fades with the
// inverse of its distance. Judged by ear (ADR-0013).
constexpr float kFullVolumeDistance = 10.0F;
// Closer than this, in meters, a voice has no direction to be heard from, and is
// heard as the listener's own.
constexpr float kMinDirectionDistance = 0.01F;

IPLVector3 ToIpl(const math::Vec3& vec) { return IPLVector3{.x = vec.x, .y = vec.y, .z = vec.z}; }

constexpr std::uint8_t kBitsPerByte = 8;

// The sample format of a PCM WAV file's samples (assets::AudioData): unsigned
// at one byte per sample, signed at two, three or four.
std::optional<ma_format> PcmFormatOf(std::uint8_t bytes_per_sample) {
  switch (bytes_per_sample) {
    case 1:
      return ma_format_u8;
    case 2:
      return ma_format_s16;
    case 3:
      return ma_format_s24;
    case 4:
      return ma_format_s32;
    default:
      return std::nullopt;
  }
}

// One playing instance of a sound.
struct Voice {
  VoiceHandle handle{};
  SoundHandle sound{};
  const std::vector<float>* samples = nullptr;
  std::size_t cursor = 0;
  // Where it is heard from, with the binaural effect that spatializes it there;
  // nullopt and nullptr for one heard as the listener's own.
  std::optional<math::Vec3> position;
  IPLBinauralEffect effect = nullptr;
};

class WindowsOutput final : public Output {
 public:
  WindowsOutput() = default;
  ~WindowsOutput() override;

  WindowsOutput(const WindowsOutput&) = delete;
  WindowsOutput& operator=(const WindowsOutput&) = delete;
  WindowsOutput(WindowsOutput&&) = delete;
  WindowsOutput& operator=(WindowsOutput&&) = delete;

  // Creates Steam Audio's context and HRTF, then opens and starts the device.
  std::optional<OutputError> Open();

  SoundHandle LoadSound(const assets::AudioData& sound) override;
  void UnloadSound(SoundHandle sound) override;
  void SetListener(const Listener& listener) override;
  VoiceHandle Play(SoundHandle sound, const std::optional<math::Vec3>& position) override;
  void StopVoice(VoiceHandle voice) override;

 private:
  static void OnData(ma_device* device, void* output, const void* input, ma_uint32 frame_count);
  void Mix(float* output, ma_uint32 frame_count);
  void MixVoice(Voice& voice, float* output, ma_uint32 frame_count);
  // Moves voice's effect to retired_ for the Main/Render thread to release.
  void Retire(Voice& voice);
  // Releases the effects retired_ holds. Main/Render thread only.
  void ReleaseRetired();

  IPLContext context_ = nullptr;
  IPLHRTF hrtf_ = nullptr;
  IPLAudioSettings audio_settings_{.samplingRate = static_cast<IPLint32>(kSampleRate),
                                   .frameSize = static_cast<IPLint32>(kFrameSize)};
  ma_device device_{};
  bool device_open_ = false;

  // Main/Render thread only, but for the samples a playing voice points into:
  // an entry is erased only once no voice plays it.
  std::unordered_map<std::uint32_t, std::vector<float>> sounds_;
  std::uint32_t next_sound_ = 1;
  std::uint32_t next_voice_ = 1;

  // Guards what both threads touch.
  std::mutex mutex_;
  std::vector<Voice> voices_;
  Listener listener_{.position = {}, .forward = {0.0F, 0.0F, -1.0F}, .up = {0.0F, 1.0F, 0.0F}};
  std::vector<IPLBinauralEffect> retired_;

  // The audio thread's own scratch, one period long.
  std::array<float, kFrameSize> mono_{};
  std::array<float, kFrameSize> left_{};
  std::array<float, kFrameSize> right_{};
};

WindowsOutput::~WindowsOutput() {
  if (device_open_) {
    // Stops the audio thread: nothing calls Mix after this.
    ma_device_uninit(&device_);
  }
  for (Voice& voice : voices_) {
    Retire(voice);
  }
  ReleaseRetired();
  if (hrtf_ != nullptr) {
    iplHRTFRelease(&hrtf_);
  }
  if (context_ != nullptr) {
    iplContextRelease(&context_);
  }
}

std::optional<OutputError> WindowsOutput::Open() {
  IPLContextSettings context_settings{};
  context_settings.version = STEAMAUDIO_VERSION;
  context_settings.simdLevel = IPL_SIMDLEVEL_AVX2;
  if (const IPLerror error = iplContextCreate(&context_settings, &context_); error != IPL_STATUS_SUCCESS) {
    return OutputError{.step = OutputStep::kSteamAudioContext, .code = static_cast<std::int32_t>(error)};
  }
  IPLHRTFSettings hrtf_settings{};
  hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
  hrtf_settings.volume = 1.0F;
  hrtf_settings.normType = IPL_HRTFNORMTYPE_NONE;
  if (const IPLerror error = iplHRTFCreate(context_, &audio_settings_, &hrtf_settings, &hrtf_);
      error != IPL_STATUS_SUCCESS) {
    return OutputError{.step = OutputStep::kHrtf, .code = static_cast<std::int32_t>(error)};
  }

  voices_.reserve(kMaxVoices);
  retired_.reserve(kMaxVoices);
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = kChannels;
  config.sampleRate = kSampleRate;
  // With miniaudio's default fixed-size callback, every Mix is one Steam Audio frame.
  config.periodSizeInFrames = kFrameSize;
  config.dataCallback = &WindowsOutput::OnData;
  config.pUserData = this;
  if (const ma_result result = ma_device_init(nullptr, &config, &device_); result != MA_SUCCESS) {
    return OutputError{.step = OutputStep::kDevice, .code = static_cast<std::int32_t>(result)};
  }
  device_open_ = true;
  if (const ma_result result = ma_device_start(&device_); result != MA_SUCCESS) {
    return OutputError{.step = OutputStep::kDeviceStart, .code = static_cast<std::int32_t>(result)};
  }
  return std::nullopt;
}

SoundHandle WindowsOutput::LoadSound(const assets::AudioData& sound) {
  const SoundHandle handle{next_sound_++};
  std::vector<float>& samples = sounds_[static_cast<std::uint32_t>(handle)];
  const auto bytes_per_sample = static_cast<std::uint8_t>(sound.bits_per_sample / kBitsPerByte);
  const std::optional<ma_format> format = PcmFormatOf(bytes_per_sample);
  if (!format.has_value() || sound.sample_rate == 0) {
    // The pack's decoder only lets valid PCM through: this sound plays silence.
    return handle;
  }
  const ma_uint64 frames_in = sound.samples.size() / bytes_per_sample;
  // With no output buffer, miniaudio says how long the converted sound is.
  samples.resize(static_cast<std::size_t>(ma_convert_frames(
      nullptr, 0, ma_format_f32, 1, kSampleRate, sound.samples.data(), frames_in, *format, 1, sound.sample_rate)));
  const ma_uint64 converted = ma_convert_frames(samples.data(), samples.size(), ma_format_f32, 1, kSampleRate,
                                                sound.samples.data(), frames_in, *format, 1, sound.sample_rate);
  samples.resize(static_cast<std::size_t>(converted));
  return handle;
}

void WindowsOutput::UnloadSound(SoundHandle sound) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& voice : voices_) {
      if (voice.sound == sound) {
        Retire(voice);
      }
    }
    std::erase_if(voices_, [sound](const Voice& voice) { return voice.sound == sound; });
  }
  ReleaseRetired();
  sounds_.erase(static_cast<std::uint32_t>(sound));
}

void WindowsOutput::SetListener(const Listener& listener) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    listener_ = listener;
  }
  ReleaseRetired();
}

VoiceHandle WindowsOutput::Play(SoundHandle sound, const std::optional<math::Vec3>& position) {
  const auto found = sounds_.find(static_cast<std::uint32_t>(sound));
  if (found == sounds_.end() || found->second.empty()) {
    return VoiceHandle{};
  }
  Voice voice{.handle = VoiceHandle{next_voice_++}, .sound = sound, .samples = &found->second, .position = position};
  if (position.has_value()) {
    IPLBinauralEffectSettings effect_settings{.hrtf = hrtf_};
    if (const IPLerror error = iplBinauralEffectCreate(context_, &audio_settings_, &effect_settings, &voice.effect);
        error != IPL_STATUS_SUCCESS) {
      LW("subsystem=audio event=voice_dropped reason=\"binaural effect creation failed\" error={}",
         static_cast<int>(error));
      return VoiceHandle{};
    }
  }
  const VoiceHandle handle = voice.handle;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= kMaxVoices) {
      Retire(voices_.front());
      voices_.erase(voices_.begin());
    }
    voices_.push_back(voice);
  }
  ReleaseRetired();
  return handle;
}

void WindowsOutput::StopVoice(VoiceHandle voice) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::ranges::find(voices_, voice, &Voice::handle);
    if (found != voices_.end()) {
      Retire(*found);
      voices_.erase(found);
    }
  }
  ReleaseRetired();
}

void WindowsOutput::Retire(Voice& voice) {
  if (voice.effect != nullptr) {
    retired_.push_back(voice.effect);
    voice.effect = nullptr;
  }
}

void WindowsOutput::ReleaseRetired() {
  std::vector<IPLBinauralEffect> released;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (retired_.empty()) {
      return;
    }
    released.reserve(kMaxVoices);
    std::swap(released, retired_);
  }
  for (IPLBinauralEffect& effect : released) {
    iplBinauralEffectRelease(&effect);
  }
}

void WindowsOutput::OnData(ma_device* device, void* output, [[maybe_unused]] const void* input, ma_uint32 frame_count) {
  static_cast<WindowsOutput*>(device->pUserData)->Mix(static_cast<float*>(output), frame_count);
}

void WindowsOutput::Mix(float* output, ma_uint32 frame_count) {
  std::fill_n(output, static_cast<std::size_t>(frame_count) * kChannels, 0.0F);
  const std::lock_guard<std::mutex> lock(mutex_);
  // miniaudio's fixed-size callback makes frame_count kFrameSize; any other
  // count is mixed a frame at a time all the same.
  for (ma_uint32 done = 0; done < frame_count; done += kFrameSize) {
    const ma_uint32 frames = std::min(kFrameSize, frame_count - done);
    for (Voice& voice : voices_) {
      MixVoice(voice, output + (static_cast<std::size_t>(done) * kChannels), frames);
    }
  }
  for (Voice& voice : voices_) {
    if (voice.cursor >= voice.samples->size()) {
      Retire(voice);
    }
  }
  std::erase_if(voices_, [](const Voice& voice) { return voice.cursor >= voice.samples->size(); });
}

void WindowsOutput::MixVoice(Voice& voice, float* output, ma_uint32 frame_count) {
  const std::vector<float>& samples = *voice.samples;
  const std::size_t count = std::min<std::size_t>(frame_count, samples.size() - std::min(voice.cursor, samples.size()));
  std::ranges::fill(mono_, 0.0F);
  std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(voice.cursor), count, mono_.begin());
  voice.cursor += count;

  const bool spatial =
      voice.position.has_value() && math::Length(*voice.position - listener_.position) >= kMinDirectionDistance;
  if (!spatial) {
    for (ma_uint32 i = 0; i < frame_count; ++i) {
      output[(i * kChannels) + 0] += mono_[i];
      output[(i * kChannels) + 1] += mono_[i];
    }
    return;
  }

  const IPLVector3 source = ToIpl(*voice.position);
  const IPLVector3 ears = ToIpl(listener_.position);
  IPLDistanceAttenuationModel model{};
  model.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
  model.minDistance = kFullVolumeDistance;
  const float gain = iplDistanceAttenuationCalculate(context_, source, ears, &model);
  IPLBinauralEffectParams params{};
  params.direction =
      iplCalculateRelativeDirection(context_, source, ears, ToIpl(listener_.forward), ToIpl(listener_.up));
  params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
  params.spatialBlend = 1.0F;
  params.hrtf = hrtf_;
  std::array<float*, 1> in_channels = {mono_.data()};
  std::array<float*, 2> out_channels = {left_.data(), right_.data()};
  IPLAudioBuffer in{.numChannels = 1, .numSamples = static_cast<IPLint32>(kFrameSize), .data = in_channels.data()};
  IPLAudioBuffer out{.numChannels = 2, .numSamples = static_cast<IPLint32>(kFrameSize), .data = out_channels.data()};
  iplBinauralEffectApply(voice.effect, &params, &in, &out);
  for (ma_uint32 i = 0; i < frame_count; ++i) {
    output[(i * kChannels) + 0] += left_[i] * gain;
    output[(i * kChannels) + 1] += right_[i] * gain;
  }
}

}  // namespace

std::expected<std::unique_ptr<Output>, OutputError> OpenOutput() {
  auto output = std::make_unique<WindowsOutput>();
  if (const std::optional<OutputError> error = output->Open(); error.has_value()) {
    return std::unexpected(*error);
  }
  return output;
}

}  // namespace augusta::audio
