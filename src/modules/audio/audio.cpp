#include "augusta/audio.h"

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "output.h"

namespace augusta::audio {

std::string DescribeOutputError(const OutputError& error) {
  switch (error.step) {
    case OutputStep::kUnsupported:
      return "this build has no audio output";
    case OutputStep::kSteamAudioContext:
      return std::format("Steam Audio context creation failed (IPLerror {})", error.code);
    case OutputStep::kHrtf:
      return std::format("Steam Audio HRTF creation failed (IPLerror {})", error.code);
    case OutputStep::kDevice:
      return std::format("no output device opened (ma_result {})", error.code);
    case OutputStep::kDeviceStart:
      return std::format("output device did not start (ma_result {})", error.code);
  }
  return "unknown output error";
}

Engine::Engine() {
  auto output = OpenOutput();
  if (!output) {
    LOG_WARN("subsystem=audio event=output_unavailable reason=\"{}\" fallback=silent",
             DescribeOutputError(output.error()));
    return;
  }
  output_ = *std::move(output);
  LOG_INFO("subsystem=audio event=output_opened");
}

Engine::~Engine() = default;

SoundHandle Engine::LoadSound(const assets::AudioData& sound) {
  return output_ ? output_->LoadSound(sound) : SoundHandle{};
}

void Engine::UnloadSound(SoundHandle sound) {
  if (output_) {
    output_->UnloadSound(sound);
  }
}

void Engine::SetListener(const Listener& listener) {
  if (output_) {
    output_->SetListener(listener);
  }
}

namespace {

// The amplitude gain_db decibels scales a sound's samples by.
float AmplitudeOf(float gain_db) {
  // An amplitude ten times as large is 20 dB louder.
  constexpr float kDecade = 10.0F;
  constexpr float kDecibelsPerDecade = 20.0F;
  return std::pow(kDecade, gain_db / kDecibelsPerDecade);
}

}  // namespace

VoiceHandle Engine::Play(SoundHandle sound, const math::Vec3& world_position, float gain_db) {
  return output_ ? output_->Play(sound, world_position, AmplitudeOf(gain_db)) : VoiceHandle{};
}

VoiceHandle Engine::Play(SoundHandle sound, float gain_db) {
  return output_ ? output_->Play(sound, std::nullopt, AmplitudeOf(gain_db)) : VoiceHandle{};
}

void Engine::Stop(VoiceHandle voice) {
  if (output_) {
    output_->Stop(voice);
  }
}

}  // namespace augusta::audio
