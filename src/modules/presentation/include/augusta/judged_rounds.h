#ifndef AUGUSTA_JUDGED_ROUNDS_H_
#define AUGUSTA_JUDGED_ROUNDS_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/body_part.h"
#include "augusta/debug_kept_shapes.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/effects.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/shown_hitboxes.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"

/// \file
/// The Shot judgements Debug overlay's rounds (ADR-0075): each of the local
/// player's rounds paired with how the server judged it (GLOSSARY.md's Shot
/// judgement), drawn as the Hitboxes the client showed when it fired (seen)
/// beside those the server judged it against (judged): the gap between them is
/// the Lag compensation error (ADR-0044). PhysicsOverlays owns it and says
/// whether the overlay is on; the client Runtime hands the judgements in.
/// Pure: it owns no clock, the caller passes the time. Main/Render thread.
/// Compiled only in a Development build (ADR-0074).
namespace augusta::presentation {

/// The outline of the Hitboxes the client showed when the local player fired
/// a round.
inline constexpr math::Vec3 kSeenHitboxColor{0.1F, 0.9F, 0.1F};
/// Of the Hitboxes the server judged it against, and the struck one's fill.
inline constexpr math::Vec3 kJudgedHitboxColor{1.0F, 0.1F, 0.1F};
/// The struck Hitbox's fill is this opaque.
inline constexpr float kStruckHitboxAlpha = 0.5F;
/// A round's flight.
inline constexpr math::Vec3 kFlightColor{1.0F, 1.0F, 1.0F};
/// The point a round struck, a filled sphere of kStruckPointRadius metres.
inline constexpr math::Vec3 kStruckPointColor{1.0F, 0.85F, 0.1F};
inline constexpr float kStruckPointRadius = 0.05F;

/// How many judged rounds are kept drawn at most, and how long each.
inline constexpr std::size_t kShotJudgementsKept = 8;
inline constexpr std::chrono::seconds kShotJudgementSeconds{5};
/// How long a round's seen Hitboxes wait for its judgement: longer than any
/// round flies (the server judges it once its flight ends) by a second for the
/// judgement's way back.
inline constexpr std::chrono::seconds kSeenHitboxesWait = ballistics::kMaxFlightTime + std::chrono::seconds{1};
/// How long a round fired with the overlay on waits unanswered before the
/// panel says none has come.
inline constexpr std::chrono::seconds kNoShotJudgementsAfter{2};
inline constexpr std::string_view kNoShotJudgementsLine =
    "no Shot judgements received: the server sends them only with `debug.shot_judgements` on";

/// What ended a round's flight, as its Shot judgement tells it.
enum class Struck : std::uint8_t {
  /// Nothing: it flew past its range or its flight time.
  kNothing,
  kMap,
  kProp,
  kHitbox,
};

/// One player a round was judged against on one tick of its flight: where the
/// server's Hitbox history posed it.
struct JudgedPlayer {
  EntityId entity{};
  hitbox::Pose pose{};
};

/// One tick of a round's flight whose segment crossed some player's bounds.
struct ShotJudgementEntry {
  std::vector<JudgedPlayer> players;
};

/// How the server judged one round the local player fired, in presentation's
/// own terms: the client Runtime converts the Session's at its edge.
struct ShotJudgement {
  /// The server tick its Shot was fired on.
  tick::Tick shot_tick = 0;
  /// Its Shooter's delay (ADR-0044), in ticks: the Seen time it was judged at
  /// is shot_tick less this.
  float shooters_delay = 0.0F;
  Struck struck = Struck::kNothing;
  /// The Body part struck; meaningful only when struck is kHitbox.
  body_part::BodyPart part = body_part::BodyPart::kTorso;
  /// Where it struck; meaningful unless struck is kNothing.
  math::Vec3 point{};
  /// The body or Prop it struck, if any.
  std::optional<EntityId> target;
  /// The ticks of its flight whose segment crossed some player's bounds,
  /// oldest first.
  std::vector<ShotJudgementEntry> entries;
  /// Whether the server left some of those ticks out.
  bool entries_left_out = false;
};

class JudgedRounds {
 public:
  using Clock = debug::KeptShapes::Clock;

  /// Takes the parameters the server admitted the client with and its tick
  /// duration, in seconds: what a round's flight is flown by, as Tracers fly
  /// it. Until then no flight is drawn.
  void SetParameters(const parameters::Parameters& parameters, float tick_duration);

  /// Moves on to the frame shown from input, which state shows, at now, with
  /// the overlay on: for each round the local player's rifle fired since the
  /// last call (FiredRounds), keeps every other player's Hitboxes where state
  /// shows them (the seen Hitboxes, PlaceShownPlayers of hitboxes), by state's
  /// Seen time, until a judgement takes them or kSeenHitboxesWait passes;
  /// keeps the local player's own Shots for their judgements' flights; and
  /// drops the judged rounds whose time has come. Called once a frame, before
  /// Judge.
  void Advance(const FrameInput& input, const State& state, const CharacterHitboxes& hitboxes, Clock::time_point now);

  /// Advance's frame with the overlay off: counts the rounds fired and follows
  /// the match's characters, and drops every round kept, judged or not, so
  /// none returns when it is switched on.
  void Skip(const FrameInput& input);

  /// Keeps judgement, received at now, drawn for kShotJudgementSeconds among
  /// the newest kShotJudgementsKept: the seen Hitboxes whose Seen time is
  /// nearest its own, within a tick (none if there are none); the judged
  /// ones, placed from its Hitbox poses with gameplay's placing (hitboxes of
  /// each judged player's character), the struck one filled; its flight from
  /// its Shot, flown as Tracers fly it against map, the Map they meet, to the
  /// point struck; and the point struck. It answers the oldest round still
  /// unanswered.
  void Judge(const ShotJudgement& judgement, const CharacterHitboxes& hitboxes, const physics::World& map,
             Clock::time_point now);

  /// The judged rounds still kept, as of the last Advance or Judge, every
  /// shape drawn through walls or not.
  [[nodiscard]] debug::Shapes Drawn(bool through_walls) const;

  /// Appends the panel's lines of them: the rounds kept, a line once some
  /// judgement left Hitbox poses out, and kNoShotJudgementsLine from when a
  /// round has waited kNoShotJudgementsAfter unanswered until the next
  /// judgement.
  void AddStatus(debug::Panel& panel) const;

 private:
  // One round's seen Hitboxes, outlined.
  struct SeenRecord {
    // Its frame's Seen time, in ticks.
    double seen_time = 0.0;
    Clock::time_point fired;
    debug::Shapes outlines;
  };
  // One of the local player's own Shots.
  struct OwnShot {
    tick::Tick tick = 0;
    math::Vec3 origin{};
    math::Vec3 direction{};
    Clock::time_point received;
  };

  // Follows input's characters, and returns the rounds fired since the last
  // frame.
  std::uint32_t Follow(const FrameInput& input);
  // Drops what has waited its time out, as of now.
  void Expire(Clock::time_point now);
  // judgement's judged Hitboxes, outlined, the struck one filled, into round.
  void AddJudgedHitboxes(const ShotJudgement& judgement, const CharacterHitboxes& hitboxes, debug::Shapes& round) const;
  // The seen Hitboxes whose Seen time is nearest seen_time, within a tick,
  // taken from those kept; nullopt if none is.
  std::optional<debug::Shapes> TakeSeen(double seen_time);
  // The flight of the Shot fired on shot_tick, from its origin against map:
  // ending at point once it passes it, if given. Empty without that Shot or
  // the parameters.
  std::vector<math::Vec3> FlightOf(tick::Tick shot_tick, const std::optional<math::Vec3>& point,
                                   const physics::World& map);

  FiredRounds fired_rounds_;
  std::deque<SeenRecord> seen_;
  std::deque<OwnShot> own_shots_;
  // When each round still unanswered was fired, oldest first.
  std::deque<Clock::time_point> unanswered_;
  // By entity, as the match named them.
  std::unordered_map<EntityId, std::string> characters_;
  std::optional<TracerRules> tracer_rules_;
  debug::KeptShapes judged_{kShotJudgementsKept};
  debug::Shapes judged_alive_;
  bool unanswered_too_long_ = false;
  bool poses_left_out_ = false;
};

}  // namespace augusta::presentation

#endif  // AUGUSTA_JUDGED_ROUNDS_H_
