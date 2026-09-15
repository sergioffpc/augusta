#include "augusta/audio.h"

namespace augusta::audio {

// TODO(sergioffpc): every method below is a placeholder - neither Steam
// Audio nor miniaudio are wired in yet (ADR-0010, ADR-0028). Just enough
// is defined here for callers to construct/link against this module.

// TODO(sergioffpc): open the output device and initialize Steam Audio's
// HRTF context.
Engine::Engine() = default;

SoundHandle Engine::LoadSound([[maybe_unused]] const std::string& asset_path) {
  // TODO(sergioffpc): decode the mono PCM asset via miniaudio.
  return {};
}

void Engine::UnloadSound([[maybe_unused]] SoundHandle sound) {
  // TODO(sergioffpc): stop and free every voice playing sound.
}

void Engine::SetListener([[maybe_unused]] const Listener& listener) {
  // TODO(sergioffpc): update Steam Audio's HRTF listener pose.
}

VoiceHandle Engine::PlaySound([[maybe_unused]] SoundHandle sound, [[maybe_unused]] const math::Vec3& world_position) {
  // TODO(sergioffpc): spatialize via Steam Audio, mix via miniaudio.
  return {};
}

void Engine::StopVoice([[maybe_unused]] VoiceHandle voice) {
  // TODO(sergioffpc): stop a still-playing voice immediately.
}

}  // namespace augusta::audio
