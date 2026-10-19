#include "augusta/cues.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
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
  CueSounds sounds;
  for (std::size_t i = 0; i < kCues.size(); ++i) {
    std::string path = assets::CueSoundPath(CueName(kCues[i]));
    const auto sound = pack.ResolveCueSound(path);
    if (!sound) {
      return std::unexpected(CueSoundError{.path = std::move(path), .resolve_error = sound.error()});
    }
    sounds[i].gain_db = sound->gain_db;
    sounds[i].variants.reserve(sound->variants.size());
    for (const std::string& variant_path : sound->variants) {
      auto variant = pack.ResolveAudio(variant_path);
      if (!variant) {
        return std::unexpected(CueSoundError{.path = variant_path, .resolve_error = variant.error()});
      }
      sounds[i].variants.push_back(*std::move(variant));
    }
  }
  return sounds;
}

std::size_t ChooseVariant(std::size_t count, std::optional<std::size_t> last, std::uint32_t random) {
  if (count <= 1) {
    return 0;
  }
  if (!last.has_value()) {
    return random % count;
  }
  // One of the count - 1 others, skipping over last.
  const std::size_t other = random % (count - 1);
  return other < *last ? other : other + 1;
}

}  // namespace augusta::audio
