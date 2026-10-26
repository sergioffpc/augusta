#ifndef AUGUSTA_CONTENT_H_
#define AUGUSTA_CONTENT_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/policy.h"
#include "augusta/weapon.h"

/// \file
/// What the server loads from its verified pack at startup (ADR-0018): the
/// scenario it runs, the Parameters its simulation and every client run on, and
/// its Game policy. Loaded before any socket or thread starts, so a pack without
/// usable content exits like a bad pack does. Where content comes from is the
/// executable's business, not the config file's: Host (host.h) is handed it
/// already loaded.
namespace augusta::scenario {

/// A character a player may join as (ADR-0042), with the hitboxes a bullet
/// that reaches a body of that character is judged against (US-11, ADR-0040),
/// the Skeleton they hang from (ADR-0061), the eye it sees and its Shots are
/// zeroed from (ADR-0064) and the Clips it is posed by (ADR-0066).
struct Character {
  /// Its path in the pack, as the scenario's character list names it.
  std::string path;
  /// Each paired with its Joint of skeleton, in that Joint's space.
  std::vector<assets::JointHitbox> hitboxes;
  // Initialized, so a designated initializer may leave it out, as a character
  // with no hitboxes does.
  // NOLINTNEXTLINE(readability-redundant-member-init)
  assets::Skeleton skeleton{};
  /// Where it sees from standing, relative to its feet (ADR-0040).
  math::Vec3 eye{};
  /// Its Clips (ADR-0066), loaded once and shared, never changed, by whatever
  /// poses it: every World built on this content. Never null: a character
  /// whose Clips do not load is refused.
  // NOLINTNEXTLINE(readability-redundant-member-init)
  std::shared_ptr<const animation::CharacterClips> clips{};
};

/// What a match is played in and with: the map's collision and where players
/// spawn, as built by augusta::map, the characters players may join as, the
/// rifle every player holds, and the client pack a player must join with.
struct Scenario {
  std::vector<physics::CollisionMesh> collision;
  /// Game policy gives each player one at every match start (US-03), as
  /// simulation::World::StartMatch does; empty spawns everyone at the origin.
  std::vector<math::Vec3> spawn_points;
  /// The scenario's characters: the only ones a player may join as
  /// (ADR-0042). Empty admits no one.
  std::vector<Character> characters;
  /// The markers every player's Muzzle and Sight are placed from (ADR-0064);
  /// by default all at the rifle's origin.
  weapon::Markers rifle{};
  /// The hash of the client pack cooked with the server's: the only one a
  /// player may join with.
  assets::PackHash client_pack{};
};

/// Everything LoadServerContent loads, for ServerRuntime's constructor.
struct Content {
  Scenario scenario;
  /// The same for the whole run, and told to every client when it joins.
  parameters::Parameters parameters{};
  /// The scenario's Game policy (ADR-0022); a scenario may lack either script.
  policy::Policy policy;
};

/// Loads startup content from the verified pack for a server ticking at
/// tick_rate_hz, which the Parameters script may check its values against
/// (ADR-0039), or returns what is wrong with it: failure::Code::kInvalidContent,
/// with the pack's path and the part of it that failed as context, for the
/// application boundary to report.
/// Every character must have a hitbox for each body part, an eye and Clips that
/// load: every hit on a player resolves to a body part (US-11), every Shot is
/// zeroed onto its shooter's line of sight from its eye (US-07, ADR-0064) and
/// every player is posed by its Clips
/// (ADR-0066), so the server runs only on characters it can judge, fire for and
/// pose. A character whose Clips do not load fails with the character as
/// context, and the Clip's role as `clip` when a Clip is what failed. The
/// scenario must have a rifle: every player's Muzzle is placed from its markers
/// (ADR-0064).
[[nodiscard]] std::expected<Content, failure::Failure> LoadServerContent(const assets::Pack& pack,
                                                                         std::uint8_t tick_rate_hz);

}  // namespace augusta::scenario

#endif  // AUGUSTA_CONTENT_H_
