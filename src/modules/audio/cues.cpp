#include "augusta/cues.h"

#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/assets.h"

namespace augusta::audio {

std::string_view CueName(Cue cue) {
  switch (cue) {
    case Cue::kGunshot:
      return "gunshot";
    case Cue::kHitMarker:
      return "hit_marker";
    case Cue::kHitTaken:
      return "hit_taken";
    case Cue::kDeath:
      return "death";
    case Cue::kMatchWon:
      return "match_won";
    case Cue::kMatchLost:
      return "match_lost";
  }
  return "unknown";
}

std::string DescribeCueSoundError(const CueSoundError& error) {
  return std::format("sound {} {}", error.path, assets::DescribeResolveError(error.resolve_error, "sound"));
}

std::expected<CueSounds, CueSoundError> LoadCueSounds(const assets::Pack& pack) {
  const auto sounds_path = pack.ResolveSoundsPath();
  if (!sounds_path) {
    return std::unexpected(
        CueSoundError{.path = std::string(assets::kSoundsPath), .resolve_error = sounds_path.error()});
  }
  CueSounds sounds;
  for (std::size_t i = 0; i < kCues.size(); ++i) {
    std::string path = std::format("{}/{}", *sounds_path, CueName(kCues[i]));
    auto sound = pack.ResolveAudio(path);
    if (!sound) {
      return std::unexpected(CueSoundError{.path = std::move(path), .resolve_error = sound.error()});
    }
    sounds[i] = *std::move(sound);
  }
  return sounds;
}

}  // namespace augusta::audio
