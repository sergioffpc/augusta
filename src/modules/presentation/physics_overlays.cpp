#include "augusta/physics_overlays.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/ballistics.h"
#include "augusta/command.h"
#include "augusta/debug_kept_shapes.h"
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/hitbox.h"
#include "augusta/interpolation.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"
#include "augusta/tracers.h"

namespace augusta::presentation {

namespace {

// What the Physics panel shows of one overlay, and how it starts.
struct OverlayControls {
  // The overlay's toggle's control id; its through-walls toggle's is this with
  // kThroughWallsSuffix.
  std::string_view id;
  std::string_view label;
  bool through_walls_by_default = false;
};

// In PhysicsOverlay's order.
constexpr std::array<OverlayControls, kPhysicsOverlayCount> kOverlays{{
    {.id = "collision", .label = "Collision", .through_walls_by_default = false},
    {.id = "server_bodies", .label = "Server bodies", .through_walls_by_default = false},
    {.id = "hitboxes", .label = "Hitboxes", .through_walls_by_default = false},
    {.id = "shot_judgements", .label = "Shot judgements", .through_walls_by_default = true},
}};

constexpr std::string_view kThroughWallsSuffix = ".through_walls";

// One of Collision's flags: its toggle in the Physics panel and the
// visualization setting it switches.
struct CollisionFlag {
  std::string_view id;
  std::string_view label;
  bool physics::Visualization::* setting;
};

constexpr std::array<CollisionFlag, 5> kCollisionFlags{{
    {.id = "collision.shapes", .label = "Collision shapes", .setting = &physics::Visualization::collision_shapes},
    {.id = "collision.bounds", .label = "Collision bounds", .setting = &physics::Visualization::bounds},
    {.id = "collision.contacts", .label = "Contact points and normals", .setting = &physics::Visualization::contacts},
    {.id = "collision.joints", .label = "Joints", .setting = &physics::Visualization::joints},
    {.id = "collision.controller_volumes",
     .label = "Controller volumes",
     .setting = &physics::Visualization::controller_volumes},
}};

constexpr std::string_view kCollisionRadiusId = "collision.radius";

// A channel of a PhysX colour (0xAARRGGBB) as a fraction.
constexpr float Channel(std::uint32_t argb, int shift) {
  constexpr std::uint32_t kMax = 0xFFU;
  return static_cast<float>((argb >> shift) & kMax) / static_cast<float>(kMax);
}

// Collision: the newest Prediction State's lines, in PhysX's colours.
void AddCollision(const FrameInput& input, bool through_walls, debug::Shapes& shapes) {
  const auto& lines = input.ticks.latest.collision_lines;
  if (lines == nullptr) {
    return;
  }
  constexpr int kRed = 16;
  constexpr int kGreen = 8;
  shapes.segments.reserve(shapes.segments.size() + lines->size());
  for (const physics::VisualizationLine& line : *lines) {
    shapes.segments.push_back(
        debug::Segment{.from = line.from,
                       .to = line.to,
                       .color = math::Vec3(Channel(line.argb, kRed), Channel(line.argb, kGreen), Channel(line.argb, 0)),
                       .alpha = 1.0F,
                       .through_walls = through_walls});
  }
}

constexpr std::size_t IndexOf(PhysicsOverlay overlay) { return static_cast<std::size_t>(overlay); }

debug::Capsule WireframeCapsule(const physics::BodyState& body, const math::Vec3& color, bool through_walls) {
  const physics::Capsule capsule = physics::CapsuleOf(body);
  return debug::Capsule{.from = capsule.bottom,
                        .to = capsule.top,
                        .radius = capsule.radius,
                        .color = color,
                        .alpha = 1.0F,
                        .style = debug::Style::kWireframe,
                        .through_walls = through_walls};
}

// Server bodies: the newest update's bodies, and the local player's predicted
// body beside its own.
void AddServerBodies(const FrameInput& input, bool through_walls, debug::Shapes& shapes) {
  if (input.updates.empty()) {
    return;
  }
  bool lists_local = false;
  for (const DynamicBody& body : input.updates.back().bodies) {
    shapes.capsules.push_back(WireframeCapsule(body.state, kServerBodyColor, through_walls));
    lists_local = lists_local || body.entity == input.local_entity;
  }
  if (lists_local) {
    shapes.capsules.push_back(WireframeCapsule(input.ticks.latest.local_body, kPredictedBodyColor, through_walls));
  }
}

// Where body's Hitboxes stand.
hitbox::Pose PoseOf(const RemoteBody& body) {
  return hitbox::Pose{.position = body.position, .yaw = body.yaw, .stance = body.stance};
}

// Each of hitbox's triangles outlined: its three edges in color.
void AddOutline(const hitbox::PlacedHitbox& hitbox, const math::Vec3& color, bool through_walls,
                debug::Shapes& shapes) {
  for (const ballistics::Triangle& triangle : hitbox.triangles) {
    for (const auto& [from, to] :
         {std::pair{triangle.a, triangle.b}, std::pair{triangle.b, triangle.c}, std::pair{triangle.c, triangle.a}}) {
      shapes.segments.push_back(
          debug::Segment{.from = from, .to = to, .color = color, .alpha = 1.0F, .through_walls = through_walls});
    }
  }
}

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

template <typename Shape>
void Append(std::vector<Shape>& to, const std::vector<Shape>& shapes) {
  to.insert(to.end(), shapes.begin(), shapes.end());
}

void Append(debug::Shapes& to, const debug::Shapes& shapes) {
  Append(to.segments, shapes.segments);
  Append(to.triangles, shapes.triangles);
  Append(to.boxes, shapes.boxes);
  Append(to.spheres, shapes.spheres);
  Append(to.capsules, shapes.capsules);
  Append(to.voxels, shapes.voxels);
}

}  // namespace

PhysicsOverlays::PhysicsOverlays(const PhysicsOverlaysStart& start) {
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    switches_.at(i).through_walls = kOverlays.at(i).through_walls_by_default;
  }
  switches_.at(IndexOf(PhysicsOverlay::kCollision)).on = start.collision;
  switches_.at(IndexOf(PhysicsOverlay::kServerBodies)).on = start.server_bodies;
  switches_.at(IndexOf(PhysicsOverlay::kHitboxes)).on = start.hitboxes;
  switches_.at(IndexOf(PhysicsOverlay::kShotJudgements)).on = start.shot_judgements;
}

debug::Panel PhysicsOverlays::BuildPanel() const {
  debug::Panel panel{.id = std::string(kPhysicsPanelId), .title = "Physics", .elements = {}};
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    const OverlayControls& overlay = kOverlays.at(i);
    panel.elements.emplace_back(
        debug::Toggle{.id = std::string(overlay.id), .label = std::string(overlay.label), .on = switches_.at(i).on});
    panel.elements.emplace_back(debug::Toggle{.id = std::format("{}{}", overlay.id, kThroughWallsSuffix),
                                              .label = std::format("{} through walls", overlay.label),
                                              .on = switches_.at(i).through_walls});
  }
  for (const CollisionFlag& flag : kCollisionFlags) {
    panel.elements.emplace_back(
        debug::Toggle{.id = std::string(flag.id), .label = std::string(flag.label), .on = collision_.*flag.setting});
  }
  panel.elements.emplace_back(debug::Slider{.id = std::string(kCollisionRadiusId),
                                            .label = "Collision radius (m)",
                                            .value = collision_.cull_half_extent,
                                            .min = kCollisionRadiusMin,
                                            .max = kCollisionRadiusMax});
  panel.elements.emplace_back(debug::Text{.text = std::format("Shot judgements kept: {}", judged_.Count())});
  if (poses_left_out_) {
    panel.elements.emplace_back(debug::Text{.text = "A Shot judgement left Hitbox poses out"});
  }
  if (unanswered_too_long_) {
    panel.elements.emplace_back(debug::Text{.text = std::string(kNoShotJudgementsLine)});
  }
  return panel;
}

void PhysicsOverlays::Update(const debug::ControlEvent& event) {
  if (event.panel != kPhysicsPanelId) {
    return;
  }
  const std::string_view control = event.control;
  if (const float* radius = std::get_if<float>(&event.value); radius != nullptr && control == kCollisionRadiusId) {
    collision_.cull_half_extent = std::clamp(*radius, kCollisionRadiusMin, kCollisionRadiusMax);
    return;
  }
  const bool* on = std::get_if<bool>(&event.value);
  if (on == nullptr) {
    return;
  }
  for (const CollisionFlag& flag : kCollisionFlags) {
    if (control == flag.id) {
      collision_.*flag.setting = *on;
    }
  }
  for (std::size_t i = 0; i < kPhysicsOverlayCount; ++i) {
    const std::string_view id = kOverlays.at(i).id;
    if (control == id) {
      switches_.at(i).on = *on;
    } else if (control.starts_with(id) && control.substr(id.size()) == kThroughWallsSuffix) {
      switches_.at(i).through_walls = *on;
    }
  }
}

bool PhysicsOverlays::IsOn(PhysicsOverlay overlay) const { return switches_.at(IndexOf(overlay)).on; }

bool PhysicsOverlays::ThroughWalls(PhysicsOverlay overlay) const {
  return switches_.at(IndexOf(overlay)).through_walls;
}

bool PhysicsOverlays::WantsShotJudgements() const { return IsOn(PhysicsOverlay::kShotJudgements); }

void PhysicsOverlays::SetParameters(const parameters::Parameters& parameters, float tick_duration) {
  tracer_rules_ = TracerRulesOf(parameters, tick_duration);
}

void PhysicsOverlays::SetCharacterHitboxes(const std::string& character,
                                           std::vector<hitbox::CharacterHitbox> hitboxes) {
  hitboxes_.insert_or_assign(character, std::move(hitboxes));
}

physics::Visualization PhysicsOverlays::Visualization(const math::Vec3& camera) const {
  if (!IsOn(PhysicsOverlay::kCollision)) {
    return physics::Visualization{};
  }
  physics::Visualization visualization = collision_;
  visualization.on = true;
  visualization.cull_center = camera;
  return visualization;
}

debug::Shapes PhysicsOverlays::BuildShapes(const FrameInput& input, const State& state) const {
  debug::Shapes shapes;
  if (IsOn(PhysicsOverlay::kCollision)) {
    AddCollision(input, ThroughWalls(PhysicsOverlay::kCollision), shapes);
  }
  if (IsOn(PhysicsOverlay::kServerBodies)) {
    AddServerBodies(input, ThroughWalls(PhysicsOverlay::kServerBodies), shapes);
  }
  if (IsOn(PhysicsOverlay::kHitboxes)) {
    for (const hitbox::PlacedHitbox& hitbox : PlaceOtherPlayers(state)) {
      AddOutline(hitbox, HitboxColorOf(hitbox.part), ThroughWalls(PhysicsOverlay::kHitboxes), shapes);
    }
  }
  if (IsOn(PhysicsOverlay::kShotJudgements)) {
    debug::Shapes judged = judged_alive_;
    SetThroughWalls(judged, ThroughWalls(PhysicsOverlay::kShotJudgements));
    Append(shapes, judged);
  }
  return shapes;
}

std::vector<hitbox::PlacedHitbox> PhysicsOverlays::PlaceOtherPlayers(const State& state) const {
  std::vector<hitbox::PlacedHitbox> placed;
  for (const RemotePlayer& player : state.remote_players) {
    if (const auto hitboxes = hitboxes_.find(player.character); hitboxes != hitboxes_.end()) {
      std::ranges::move(hitbox::Place(hitboxes->second, PoseOf(player.body)), std::back_inserter(placed));
    }
  }
  return placed;
}

void PhysicsOverlays::Advance(const FrameInput& input, const State& state, Clock::time_point now) {
  const std::uint32_t fired = fired_rounds_.Update(input.ticks.latest.total_rounds_fired);
  std::erase_if(seen_, [now](const SeenRecord& record) { return record.fired + kSeenHitboxesWait <= now; });
  std::erase_if(own_shots_, [now](const OwnShot& shot) { return shot.received + kSeenHitboxesWait <= now; });
  judged_alive_ = judged_.Alive(now);
  characters_.clear();
  for (const PlayerCharacter& player : input.characters) {
    characters_.insert_or_assign(player.entity, player.character);
  }
  if (!IsOn(PhysicsOverlay::kShotJudgements)) {
    seen_.clear();
    own_shots_.clear();
    unanswered_since_.reset();
    unanswered_too_long_ = false;
    return;
  }
  for (const Shot& shot : input.shots) {
    if (shot.shooter == input.local_entity) {
      own_shots_.push_back(OwnShot{.tick = shot.tick,
                                   .origin = shot.origin,
                                   .direction = command::ViewDirection(shot.yaw, shot.pitch),
                                   .received = now});
    }
  }
  if (fired > 0) {
    unanswered_since_ = unanswered_since_.value_or(now);
    if (state.seen_time.has_value()) {
      SeenRecord record{.seen_time = static_cast<double>(state.seen_time->tick) + state.seen_time->fraction,
                        .fired = now,
                        .outlines = {}};
      for (const hitbox::PlacedHitbox& hitbox : PlaceOtherPlayers(state)) {
        AddOutline(hitbox, kSeenHitboxColor, true, record.outlines);
      }
      seen_.insert(seen_.end(), fired, record);
    }
  }
  unanswered_too_long_ = unanswered_since_.has_value() && now - *unanswered_since_ >= kNoShotJudgementsAfter;
}

void PhysicsOverlays::Judge(const ShotJudgement& judgement, Clock::time_point now) {
  if (!IsOn(PhysicsOverlay::kShotJudgements)) {
    return;
  }
  unanswered_since_.reset();
  unanswered_too_long_ = false;
  poses_left_out_ = poses_left_out_ || judgement.entries_left_out;

  debug::Shapes round;
  const double seen_time = static_cast<double>(judgement.shot_tick) - judgement.shooters_delay;
  if (std::optional<debug::Shapes> seen = TakeSeen(seen_time)) {
    Append(round, *seen);
  }
  const bool struck_hitbox = judgement.struck == Struck::kHitbox && judgement.target.has_value();
  // The struck Hitbox is the target's on the last tick that lists it: the one
  // its flight ended on.
  std::optional<std::size_t> struck_entry;
  for (std::size_t i = 0; i < judgement.entries.size(); ++i) {
    for (const JudgedPlayer& player : judgement.entries[i].players) {
      if (struck_hitbox && player.entity == *judgement.target) {
        struck_entry = i;
      }
    }
  }
  for (std::size_t i = 0; i < judgement.entries.size(); ++i) {
    for (const JudgedPlayer& player : judgement.entries[i].players) {
      const auto character = characters_.find(player.entity);
      if (character == characters_.end()) {
        continue;
      }
      const auto hitboxes = hitboxes_.find(character->second);
      if (hitboxes == hitboxes_.end()) {
        continue;
      }
      const bool struck_player = struck_entry == i && player.entity == judgement.target;
      for (const hitbox::PlacedHitbox& hitbox : hitbox::Place(hitboxes->second, player.pose)) {
        AddOutline(hitbox, kJudgedHitboxColor, true, round);
        if (struck_player && hitbox.part == judgement.part) {
          AddFill(hitbox, kJudgedHitboxColor, round);
        }
      }
    }
  }
  const bool struck = judgement.struck != Struck::kNothing;
  const std::vector<math::Vec3> flight =
      FlightOf(judgement.shot_tick, struck ? std::optional(judgement.point) : std::nullopt);
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

std::optional<debug::Shapes> PhysicsOverlays::TakeSeen(double seen_time) {
  const auto nearest = std::ranges::min_element(
      seen_, {}, [seen_time](const SeenRecord& record) { return std::abs(record.seen_time - seen_time); });
  if (nearest == seen_.end() || std::abs(nearest->seen_time - seen_time) > 1.0) {
    return std::nullopt;
  }
  debug::Shapes outlines = std::move(nearest->outlines);
  seen_.erase(nearest);
  return outlines;
}

std::vector<math::Vec3> PhysicsOverlays::FlightOf(tick::Tick shot_tick, const std::optional<math::Vec3>& point) {
  const auto shot = std::ranges::find_if(own_shots_, [shot_tick](const OwnShot& own) { return own.tick == shot_tick; });
  if (shot == own_shots_.end() || !tracer_rules_.has_value()) {
    return {};
  }
  if (!no_map_.has_value()) {
    no_map_.emplace(physics::StaminaConfig{});
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
    const ballistics::StepResult step = ballistics.Step(bullet, rules.tick_duration, *no_map_, {});
    flight.push_back(step.state.position);
    if (step.outcome != ballistics::Outcome::kInFlight) {
      break;
    }
  }
  own_shots_.erase(shot);
  return flight;
}

}  // namespace augusta::presentation
