#include "augusta/audio.h"

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
    LW("subsystem=audio event=output_unavailable reason=\"{}\" fallback=silent", DescribeOutputError(output.error()));
    return;
  }
  output_ = *std::move(output);
  LI("subsystem=audio event=output_opened");
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

VoiceHandle Engine::Play(SoundHandle sound, const math::Vec3& world_position) {
  return output_ ? output_->Play(sound, world_position) : VoiceHandle{};
}

VoiceHandle Engine::Play(SoundHandle sound) { return output_ ? output_->Play(sound, std::nullopt) : VoiceHandle{}; }

void Engine::StopVoice(VoiceHandle voice) {
  if (output_) {
    output_->StopVoice(voice);
  }
}

}  // namespace augusta::audio
