#ifndef AUGUSTA_CUES_H_
#define AUGUSTA_CUES_H_

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "augusta/assets.h"

/// \file
/// The client's cue catalogue (ADR-0020, ADR-0031): every one-shot sound the
/// client plays, fixed in code. A scenario ships a mono PCM sound for each in its
/// client pack, addressed `<sounds folder>/<cue name>`, and the client loads them
/// all at startup, so a missing one is found before a Match rather than during
/// one. The cooker (tools/pack/src/pack/sounds.py) holds the same catalogue.
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

/// The sound of every cue, index for index with kCues.
using CueSounds = std::array<assets::AudioData, kCues.size()>;

/// Why the cue sounds could not be loaded: path is the pack path that did not
/// resolve (kSoundsPath, or a cue's sound), resolve_error why.
struct CueSoundError {
  std::string path;
  assets::ResolveError resolve_error{};
};

/// A message for error fit to print to whoever runs the process.
[[nodiscard]] std::string DescribeCueSoundError(const CueSoundError& error);

/// Resolves every cue's sound from pack, under the sounds folder it names.
[[nodiscard]] std::expected<CueSounds, CueSoundError> LoadCueSounds(const assets::Pack& pack);

}  // namespace augusta::audio

#endif  // AUGUSTA_CUES_H_
