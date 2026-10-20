#ifndef AUGUSTA_BODY_PART_H_
#define AUGUSTA_BODY_PART_H_

#include <array>
#include <cstdint>
#include <string_view>

/// \file
/// augusta::body_part is the Body part (GLOSSARY.md): where on a player a
/// Hitbox stands and so where a round struck them (US-11), the one domain enum
/// every module naming one uses, from the simulation's damage to the link and
/// the host's logs and metrics. The assets' blob keeps its own numbering, which
/// the host converts to this one. ADR-0040 decides the three, one per Hitbox.
namespace augusta::body_part {

/// The coarse zones REQUIREMENTS.md's "e.g., head, torso, limb" asks for.
enum class BodyPart : std::uint8_t {
  kHead,
  kTorso,
  kLimb,
};

/// Every Body part, in the enum's order.
inline constexpr std::array kBodyParts = {BodyPart::kHead, BodyPart::kTorso, BodyPart::kLimb};

/// part's name as a log line or a metric labels it, e.g. "head".
[[nodiscard]] constexpr std::string_view NameOf(BodyPart part) {
  switch (part) {
    case BodyPart::kHead:
      return "head";
    case BodyPart::kTorso:
      return "torso";
    case BodyPart::kLimb:
      return "limb";
  }
  return {};
}

}  // namespace augusta::body_part

#endif  // AUGUSTA_BODY_PART_H_
