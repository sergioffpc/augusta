#ifndef AUGUSTA_CUES_H_
#define AUGUSTA_CUES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"

/// \file
/// The client's cue catalogue (ADR-0020, ADR-0031, ADR-0041): every one-shot
/// sound the client plays, fixed in code, as is when each plays (presentation's
/// CueSelector). What each sounds like is the scenario's: its client pack holds,
/// at assets::CueSoundPath, each cue's gain and variants, mono PCM sounds of
/// which one plays at random each time (ChooseVariant). The client loads them all
/// at startup, so a missing one is found before a Match rather than during one.
/// The cooker (tools/pack/src/augusta_pack/sounds.py) holds the same catalogue.
namespace augusta::audio {

/// A named client sound event.
enum class Cue : std::uint8_t {
  kGunshot,
  kHitMarker,
  kHitTaken,
  kDeath,
  kMatchWon,
  kMatchLost,
};

/// Every cue, in catalogue order: index i of CueSounds is kCues[i]'s sound.
inline constexpr std::array kCues = {Cue::kGunshot, Cue::kHitMarker, Cue::kHitTaken,
                                     Cue::kDeath,   Cue::kMatchWon,  Cue::kMatchLost};

/// cue's name, as its sound file and pack path spell it (e.g. "hit_marker").
[[nodiscard]] std::string_view CueName(Cue cue);

/// A cue's sound, as the client plays it: one of its variants each time, at its
/// gain. LoadCueSounds never gives one without a variant.
struct CueSound {
  std::vector<assets::AudioData> variants;
  /// In decibels: 0 plays each variant as its WAV file holds it.
  float gain_db = 0.0F;
};

/// The sound of every cue, index for index with kCues.
using CueSounds = std::array<CueSound, kCues.size()>;

/// Why the cue sounds could not be loaded: path is the pack path that did not
/// resolve (a cue's sound, or one of its variants), resolve_error why.
struct CueSoundError {
  std::string path;
  assets::ResolveError resolve_error{};
};

/// A message for error fit to print to whoever runs the process.
[[nodiscard]] std::string DescribeCueSoundError(const CueSoundError& error);

/// Resolves every cue's sound, and each of its variants, from pack.
[[nodiscard]] std::expected<CueSounds, CueSoundError> LoadCueSounds(const assets::Pack& pack);

/// Which of a cue's count variants plays (count >= 1), given the one it played
/// last (nullopt if it has not played yet) and random, any number from a
/// random source: never last again, unless count is 1.
[[nodiscard]] std::size_t ChooseVariant(std::size_t count, std::optional<std::size_t> last, std::uint32_t random);

}  // namespace augusta::audio

#endif  // AUGUSTA_CUES_H_
