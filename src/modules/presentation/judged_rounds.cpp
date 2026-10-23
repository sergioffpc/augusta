#include "augusta/judged_rounds.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/debug_kept_shapes.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/shown_hitboxes.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"

namespace augusta::presentation {

namespace {

// Each of hitbox's triangles filled in color.
void AddFill(const hitbox::PlacedHitbox& hitbox, const math::Vec3& color, debug::Shapes& shapes) {
  for (const ballistics::Triangle& triangle : hitbox.triangles) {
    shapes.triangles.push_back(
        debug::Triangle{.corners = {triangle.a, triangle.b, triangle.c}, .color = color, .alpha = kStruckHitboxAlpha});
  }
}

// How far point is from the segment between from and to.
float DistanceToSegment(const math::Vec3& point, const math::Vec3& from, const math::Vec3& to) {
  const math::Vec3 along = to - from;
  const float length_squared = math::Dot(along, along);
  const float t =
      length_squared > 0.0F ? std::clamp(math::Dot(point - from, along) / length_squared, 0.0F, 1.0F) : 0.0F;
  return math::Length(point - (from + (along * t)));
}

// How near a flight's tick passes the point its judgement says it struck to
// end there: the server flies it with the same steps, so it lies on one.
constexpr float kStruckPointTolerance = 0.1F;

// Which of judgement's entries its struck Hitbox is on: the last that lists
// its target, the tick its flight ended on; nullopt unless it struck a Hitbox.
std::optional<std::size_t> StruckEntryOf(const ShotJudgement& judgement) {
  if (judgement.struck != Struck::kHitbox || !judgement.target.has_value()) {
    return std::nullopt;
  }
  for (std::size_t i = judgement.entries.size(); i > 0; --i) {
    if (std::ranges::any_of(judgement.entries[i - 1].players,
                            [&](const JudgedPlayer& player) { return player.entity == *judgement.target; })) {
      return i - 1;
    }
  }
  return std::nullopt;
}

// Every shape of shapes drawn through walls or not.
void SetThroughWalls(debug::Shapes& shapes, bool through_walls) {
  const auto set = [through_walls](auto& kind) {
    for (auto& shape : kind) {
      shape.through_walls = through_walls;
    }
  };
  set(shapes.segments);
  set(shapes.triangles);
  set(shapes.boxes);
  set(shapes.spheres);
  set(shapes.capsules);
  set(shapes.voxels);
}

}  // namespace

void JudgedRounds::SetParameters(const parameters::Parameters& parameters, float tick_duration) {
  tracer_rules_ = TracerRulesOf(parameters, tick_duration);
}

void JudgedRounds::Advance(const FrameInput& input, const State& state, const CharacterHitboxes& hitboxes,
                           Clock::time_point now) {
  const std::uint32_t fired = Follow(input);
  Expire(now);
  for (const Shot& shot : input.shots) {
    if (shot.shooter == input.local_entity) {
      own_shots_.push_back(OwnShot{.tick = shot.tick,
                                   .origin = shot.origin,
                                   .direction = command::ViewDirection(shot.yaw, shot.pitch),
                                   .received = now});
    }
  }
  if (fired > 0) {
    unanswered_.insert(unanswered_.end(), fired, now);
    if (state.seen_time.has_value()) {
      SeenRecord record{.seen_time = static_cast<double>(state.seen_time->tick) + state.seen_time->fraction,
                        .fired = now,
                        .outlines = {}};
      for (const hitbox::PlacedHitbox& hitbox : PlaceShownPlayers(hitboxes, state)) {
        AddOutline(hitbox, kSeenHitboxColor, true, record.outlines);
      }
      seen_.insert(seen_.end(), fired, record);
    }
  }
}

void JudgedRounds::Expire(Clock::time_point now) {
  const auto waited = [now](Clock::time_point since) { return since + kSeenHitboxesWait <= now; };
  // Latched until the next judgement, even once the round it was for expires.
  unanswered_too_long_ =
      unanswered_too_long_ || (!unanswered_.empty() && now - unanswered_.front() >= kNoShotJudgementsAfter);
  std::erase_if(unanswered_, waited);
  std::erase_if(seen_, [&waited](const SeenRecord& record) { return waited(record.fired); });
  std::erase_if(own_shots_, [&waited](const OwnShot& shot) { return waited(shot.received); });
  judged_alive_ = judged_.Alive(now);
}

std::uint32_t JudgedRounds::Follow(const FrameInput& input) {
  characters_.clear();
  for (const PlayerCharacter& player : input.characters) {
    characters_.insert_or_assign(player.entity, player.character);
  }
  return fired_rounds_.Update(input.ticks.latest.total_rounds_fired);
}

void JudgedRounds::Skip(const FrameInput& input) {
  static_cast<void>(Follow(input));
  seen_.clear();
  own_shots_.clear();
  unanswered_.clear();
  unanswered_too_long_ = false;
  judged_ = debug::KeptShapes{kShotJudgementsKept};
  judged_alive_ = {};
}

void JudgedRounds::Judge(const ShotJudgement& judgement, const CharacterHitboxes& hitboxes, const physics::World& map,
                         Clock::time_point now) {
  if (!unanswered_.empty()) {
    unanswered_.pop_front();
  }
  unanswered_too_long_ = false;
  poses_left_out_ = poses_left_out_ || judgement.entries_left_out;

  debug::Shapes round;
  const double seen_time = static_cast<double>(judgement.shot_tick) - judgement.shooters_delay;
  if (std::optional<debug::Shapes> seen = TakeSeen(seen_time)) {
    debug::Append(round, *seen);
  }
  AddJudgedHitboxes(judgement, hitboxes, round);
  const bool struck = judgement.struck != Struck::kNothing;
  const std::vector<math::Vec3> flight =
      FlightOf(judgement.shot_tick, struck ? std::optional(judgement.point) : std::nullopt, map);
  for (std::size_t i = 1; i < flight.size(); ++i) {
    round.segments.push_back(debug::Segment{.from = flight[i - 1], .to = flight[i], .color = kFlightColor});
  }
  if (struck) {
    round.spheres.push_back(debug::Sphere{.center = judgement.point,
                                          .radius = kStruckPointRadius,
                                          .color = kStruckPointColor,
                                          .alpha = 1.0F,
                                          .style = debug::Style::kFilled});
  }
  judged_.Keep(round, now + kShotJudgementSeconds);
  judged_alive_ = judged_.Alive(now);
}

debug::Shapes JudgedRounds::Drawn(bool through_walls) const {
  debug::Shapes drawn = judged_alive_;
  SetThroughWalls(drawn, through_walls);
  return drawn;
}

void JudgedRounds::AddStatus(debug::Panel& panel) const {
  panel.elements.emplace_back(debug::Text{.text = std::format("Shot judgements kept: {}", judged_.Count())});
  if (poses_left_out_) {
    panel.elements.emplace_back(debug::Text{.text = "A Shot judgement left Hitbox poses out"});
  }
  if (unanswered_too_long_) {
    panel.elements.emplace_back(debug::Text{.text = std::string(kNoShotJudgementsLine)});
  }
}

void JudgedRounds::AddJudgedHitboxes(const ShotJudgement& judgement, const CharacterHitboxes& hitboxes,
                                     debug::Shapes& round) const {
  const std::optional<std::size_t> struck_entry = StruckEntryOf(judgement);
  for (std::size_t i = 0; i < judgement.entries.size(); ++i) {
    for (const JudgedPlayer& player : judgement.entries[i].players) {
      const auto character = characters_.find(player.entity);
      const auto character_hitboxes =
          character == characters_.end() ? hitboxes.end() : hitboxes.find(character->second);
      if (character_hitboxes == hitboxes.end()) {
        continue;
      }
      const bool struck_player = struck_entry == i && player.entity == judgement.target;
      for (const hitbox::PlacedHitbox& hitbox : hitbox::Place(character_hitboxes->second, player.pose)) {
        AddOutline(hitbox, kJudgedHitboxColor, true, round);
        if (struck_player && hitbox.part == judgement.part) {
          AddFill(hitbox, kJudgedHitboxColor, round);
        }
      }
    }
  }
}

std::optional<debug::Shapes> JudgedRounds::TakeSeen(double seen_time) {
  const auto nearest = std::ranges::min_element(
      seen_, {}, [seen_time](const SeenRecord& record) { return std::abs(record.seen_time - seen_time); });
  if (nearest == seen_.end() || std::abs(nearest->seen_time - seen_time) > 1.0) {
    return std::nullopt;
  }
  debug::Shapes outlines = std::move(nearest->outlines);
  seen_.erase(nearest);
  return outlines;
}

std::vector<math::Vec3> JudgedRounds::FlightOf(tick::Tick shot_tick, const std::optional<math::Vec3>& point,
                                               const physics::World& map) {
  const auto shot = std::ranges::find_if(own_shots_, [shot_tick](const OwnShot& own) { return own.tick == shot_tick; });
  if (shot == own_shots_.end() || !tracer_rules_.has_value()) {
    return {};
  }
  const TracerRules& rules = *tracer_rules_;
  ballistics::World ballistics;
  const ballistics::BulletHandle bullet =
      ballistics.Fire(shot->origin, shot->direction, rules.muzzle_velocity, rules.bullet);
  std::vector<math::Vec3> flight{shot->origin};
  while (true) {
    const ballistics::Segment next = ballistics.NextSegment(bullet, rules.tick_duration);
    if (point.has_value() && DistanceToSegment(*point, next.from, next.to) <= kStruckPointTolerance) {
      flight.push_back(*point);
      break;
    }
    // No hitboxes, as a tracer has none: what it meets here is the Map.
    const ballistics::StepResult step = ballistics.Step(bullet, rules.tick_duration, map, {});
    flight.push_back(step.outcome == ballistics::Outcome::kStruck ? step.impact_point : step.state.position);
    if (step.outcome != ballistics::Outcome::kInFlight) {
      break;
    }
  }
  own_shots_.erase(shot);
  return flight;
}

}  // namespace augusta::presentation
