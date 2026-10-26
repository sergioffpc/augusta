#ifndef AUGUSTA_WEAPON_H_
#define AUGUSTA_WEAPON_H_

#include <cstdint>
#include <vector>

#include "augusta/assets.h"
#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/math.h"

/// \file
/// augusta::weapon holds the rifle and its ammunition (US-06, US-10, US-12),
/// as the Parameters configure them, and the rifle's rules (US-07 to US-09):
/// what a tick's Command does to a player's rifle, whether it fires a round,
/// and where that round leaves for. It is ARCHITECTURE.md §5's WeaponHandling: pure mechanism
/// in the shared core, with no world and no state of its own, so the two Worlds
/// that run a WeaponHandling phase call the same function, as both call
/// physics::World. The server's SimulationWorld takes the result as
/// authoritative and fires a bullet for each round (ADR-0023); the client's
/// PredictionWorld predicts its own fire, reload and recoil with it (ADR-0024),
/// and is reconciled against the server's State (ADR-0004). Nothing here is
/// random, so the two agree.
///
/// The rifle's values are the Parameters' (ADR-0039), handed in on every call.
/// How a round flies is ballistics' BulletConfig, and what it does to each Body
/// part is body_part's Damage: the Ammo only bundles the two.
namespace augusta::weapon {

/// How far one round of a burst turns the aim, in radians: a positive pitch up,
/// a positive yaw left (as command::Command's view turns).
struct RecoilKick {
  float pitch = 0.0F;
  float yaw = 0.0F;

  bool operator==(const RecoilKick&) const = default;
};

/// The rifle every player carries (US-06 to US-09; the only weapon).
struct Rifle {
  /// How many rounds per minute holding fire shoots. At most one round fires a
  /// tick, so a rate above the tick rate fires at the tick rate.
  float rounds_per_minute = 1.0F;
  /// How fast a round leaves the muzzle, in meters per second.
  float muzzle_velocity = 1.0F;
  /// How long a reload takes, in seconds.
  float reload_seconds = 0.0F;
  /// How fast, in radians per second, the recoil a burst built up returns to
  /// zero while the rifle is not firing.
  float recoil_recovery_per_second = 0.0F;
  /// What aiming down sights scales every recoil kick by, above 0 and at most 1.
  float ads_recoil_scale = 1.0F;
  /// The vertical field of view aiming down sights zooms to, in radians.
  float ads_field_of_view = 1.0F;
  /// The rifle's zero range, in meters: how far from the Eye a round crosses
  /// the line of sight, from the Eye along the view (ADR-0064). Above 0.
  float zero_range = 1.0F;
  /// The kick of each round of a burst, in order, at most
  /// limits::kMaxRecoilKicks; past the last, the last repeats. Empty: no recoil.
  std::vector<RecoilKick> recoil_pattern;
  /// How many rounds a full magazine holds, 1 or more.
  std::uint8_t magazine_capacity = 1;

  bool operator==(const Rifle&) const = default;
};

/// The rifle's ammunition: how its rounds fly (US-10) and what they do (US-12).
struct Ammo {
  /// How a round flies. A range of 1, not BulletConfig's 0, so a default
  /// Parameters is one the simulation can run on.
  ballistics::BulletConfig flight{.gravity = 0.0F, .max_range = 1.0F};
  body_part::Damage damage{};

  bool operator==(const Ammo&) const = default;
};

/// How far a rifle points off its player's view (CONTEXT.md's Recoil offset),
/// in radians: a positive pitch up, a positive yaw left, as command::Command's
/// view turns. Each on the grid an angle travels on (math::kAngleGrid).
struct RecoilOffset {
  float pitch = 0.0F;
  float yaw = 0.0F;

  bool operator==(const RecoilOffset&) const = default;
};

/// The rifle's authored markers (ADR-0064), each in the rifle's own space, each
/// pointing along its -Z: the Grip, where the holding hand holds it; the Muzzle,
/// the end of its barrel, pointing the way the barrel does; the Sight, the
/// point the Eye looks through in ADS, pointing along its line of sight; and the
/// Fore-end, where the left hand holds it (ADR-0066). By default all four at the
/// rifle's origin.
struct Markers {
  math::Mat4 grip{1.0F};
  math::Mat4 muzzle{1.0F};
  math::Mat4 sight{1.0F};
  math::Mat4 fore_end{1.0F};

  bool operator==(const Markers&) const = default;
};

/// The markers of rifle, as a pack holds them (ADR-0064): each a frame in the
/// rifle's space, at its translation and turned by its rotation. What the
/// server and the client each resolve the scenario's rifle into.
[[nodiscard]] Markers MarkersOf(const assets::RifleData& rifle);

/// Where the Muzzle of the rifle markers describes is, held by hand at its
/// Grip: hand times the Grip's inverse times the Muzzle (ADR-0064). The one
/// function the server and the client place a Muzzle with, whatever the hand:
/// a posed one's (its weapon socket, ADR-0066) or a tracked one's.
[[nodiscard]] math::Mat4 PlaceMuzzle(const math::Mat4& hand, const Markers& markers);

/// Where the Sight of the rifle markers describes is, held by hand at its Grip,
/// as PlaceMuzzle places the Muzzle.
[[nodiscard]] math::Mat4 PlaceSight(const math::Mat4& hand, const Markers& markers);

/// Where the Fore-end of the rifle markers describes is, held by hand at its
/// Grip, as PlaceMuzzle places the Muzzle: where the left hand reaches.
[[nodiscard]] math::Mat4 PlaceForeEnd(const math::Mat4& hand, const Markers& markers);

/// hand, which holds the rifle at its Grip, turned about the Grip by recoil:
/// as recoil turns a view of yaw and pitch, which a Shot leaves along
/// (ADR-0064). Only the rifle turns; the body that holds it does not.
[[nodiscard]] math::Mat4 Recoiled(const math::Mat4& hand, float yaw, float pitch, const RecoilOffset& recoil);

/// A direction as a view's yaw and pitch, in radians (command::ViewDirection
/// gives it as a vector).
struct Heading {
  float yaw = 0.0F;
  float pitch = 0.0F;

  bool operator==(const Heading&) const = default;
};

/// direction, of any length but 0, as a view's yaw and pitch:
/// command::ViewDirection's inverse.
[[nodiscard]] Heading HeadingOf(const math::Vec3& direction);

/// Where a round of rifle leaves muzzle for, zeroed (ADR-0064): so that, flying
/// as flight makes it in ballistics::World steps of step_seconds, it crosses
/// the line of sight from eye along sight at the rifle's zero range. Computed
/// from how far ballistics::World drops a round, with no raycast. Nearer or
/// further, the round passes off the line of sight, by the Muzzle's offset from
/// it and by the drop. Unwrapped as sight is: sight's yaw, turned.
[[nodiscard]] Heading Zeroed(const Rifle& rifle, const ballistics::BulletConfig& flight, float step_seconds,
                             const math::Vec3& muzzle, const math::Vec3& eye, const Heading& sight);

/// One player's rifle between two ticks.
struct State {
  /// How long, in seconds, until the next round may fire; 0 or less when one
  /// may. Below 0 only while the trigger is held: see Step.
  float cooldown = 0.0F;
  /// How long, in seconds, the reload under way still takes; 0 when there is none.
  float reload_remaining = 0.0F;
  /// What the rounds fired so far have left the rifle pointing off the view by.
  RecoilOffset recoil{};
  /// How many rounds are left in the magazine.
  std::uint8_t rounds = 0;
  /// How many rounds the Burst under way has fired: which kick of the recoil
  /// pattern the next one gives. 0 when there is none.
  std::uint8_t burst_index = 0;

  bool operator==(const State&) const = default;
};

/// rifle with a full magazine, ready to fire along the view: what a player
/// starts a Match with.
[[nodiscard]] State Loaded(const Rifle& rifle);

/// What one tick did to a rifle.
struct Result {
  /// The rifle after the tick.
  State state{};
  /// Where the round fired is aimed, as a view's yaw and pitch in radians
  /// (command::ViewDirection gives the direction): the Command's view turned by
  /// the Recoil offset the rifle had, the line of sight its round is zeroed
  /// onto (Zeroed). Only of a tick that fired.
  float yaw = 0.0F;
  float pitch = 0.0F;
  /// Whether a round was fired on it. At most one fires a tick.
  bool fired = false;
};

/// Runs one fixed tick of delta_time seconds of rifle, in state, on command.
///
/// Fire (US-07): while command holds fire and the magazine has a round, a round
/// fires on the first tick one may, and then one every fire interval (60 /
/// rounds_per_minute seconds): a tap fires one round, and holding fires at the
/// rifle's rate until the magazine is empty. An interval that is not a whole
/// number of ticks is kept on average, the fraction of a tick a round waited
/// being taken off the wait for the next; a released trigger carries nothing
/// over. An interval shorter than a tick fires every tick.
///
/// Reload (US-08): a command that presses reload with a magazine that is not
/// full starts a reload of reload_seconds, counted from that tick and rounded
/// up to whole ticks. No round fires on a tick of it, the one it starts on
/// included, and with its last tick the magazine is full. A press with a full
/// magazine, or during a reload, does nothing: a reload is never started over.
///
/// Recoil (US-09): a round leaves along the view turned by the Recoil offset,
/// and then adds its kick to it: the recoil pattern's kick for its place in the
/// Burst, the last kick again past the pattern's end, scaled by
/// ads_recoil_scale while command holds ads. So the first round of a rifle at
/// rest leaves along the view, and each one after it further off. A Burst lasts
/// while the trigger is pulled: fire held, with a round in the magazine and no
/// reload under way. On a tick it is not, the next round is a Burst's first
/// again, and the offset shrinks toward zero by recoil_recovery_per_second. The
/// offset is the rifle's alone: command's yaw and pitch are never changed.
[[nodiscard]] Result Step(const Rifle& rifle, const State& state, const command::Command& command, float delta_time);

}  // namespace augusta::weapon

#endif  // AUGUSTA_WEAPON_H_
