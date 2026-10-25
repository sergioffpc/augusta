#include "augusta/interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/pose.h"
#include "augusta/tick.h"

namespace augusta::presentation {

namespace {

// Below this fraction of the way from the earlier update to the later, the
// earlier stance is shown; at or beyond it, the later one is - see Sample's
// use below.
constexpr float kMidpointFraction = 0.5F;

RemoteBody AsRemote(const physics::BodyState& body, const Aim& aim) {
  return RemoteBody{.position = body.position,
                    .velocity = body.velocity,
                    .yaw = aim.yaw,
                    .pitch = aim.pitch,
                    .stance = body.stance,
                    .ads = aim.ads,
                    .stride = body.stride,
                    .previous_stance = body.previous_stance,
                    .stance_progress = body.stance_progress,
                    .grounded = body.grounded};
}

// The Stride t of the way from from to to, the shorter way round its cycle.
std::uint8_t LerpStride(std::uint8_t from, std::uint8_t to, float t) {
  constexpr int kHalfCycle = physics::kStrideSteps / 2;
  // How far on to is from from, from -kHalfCycle to kHalfCycle - 1 steps.
  const int on = ((to - from + kHalfCycle + physics::kStrideSteps) % physics::kStrideSteps) - kHalfCycle;
  const auto moved = static_cast<int>(std::lround(static_cast<float>(on) * t));
  return static_cast<std::uint8_t>((from + moved + physics::kStrideSteps) % physics::kStrideSteps);
}

// The body t of the way from earlier to later, each with where its player
// aimed: what RemoteBody says moves between them, the rest the nearer's.
RemoteBody Between(const physics::BodyState& earlier, const Aim& earlier_aim, const physics::BodyState& later,
                   const Aim& later_aim, float t) {
  RemoteBody body = t < kMidpointFraction ? AsRemote(earlier, earlier_aim) : AsRemote(later, later_aim);
  body.position = math::Lerp(earlier.position, later.position, t);
  body.velocity = math::Lerp(earlier.velocity, later.velocity, t);
  body.yaw = math::LerpAngle(earlier_aim.yaw, later_aim.yaw, t);
  body.pitch = std::lerp(earlier_aim.pitch, later_aim.pitch, t);
  body.stride = LerpStride(earlier.stride, later.stride, t);
  if (earlier.stance == later.stance && earlier.previous_stance == later.previous_stance) {
    body.stance_progress = static_cast<std::uint8_t>(std::lround(
        std::lerp(static_cast<float>(earlier.stance_progress), static_cast<float>(later.stance_progress), t)));
  }
  return body;
}

}  // namespace

void ServerClock::Advance(double elapsed) {
  if (!now_.has_value()) {
    return;
  }
  const double rate = std::abs(behind_) > kFastSlewBeyond ? kFastSlew : kSlew;
  const double limit = rate * elapsed;
  const double catch_up = std::clamp(behind_, -limit, limit);
  *now_ += elapsed + catch_up;
  behind_ -= catch_up;
}

void ServerClock::Observe(double server_time) {
  if (!now_.has_value() || std::abs(server_time - *now_) > kResyncThreshold) {
    now_ = server_time;
    newest_ = server_time;
    behind_ = 0.0;
    return;
  }
  newest_ = std::max(newest_, server_time);
  behind_ = server_time - *now_;
}

void ServerClock::Reset() {
  now_.reset();
  newest_ = 0.0;
  behind_ = 0.0;
}

std::optional<double> ServerClock::Now() const { return now_; }

std::optional<double> ServerClock::Slack() const {
  if (!now_.has_value()) {
    return std::nullopt;
  }
  return newest_ - (*now_ - kInterpolationDelay);
}

SeenTime SeenTimeAt(double sample_time, double tick_duration, tick::Tick oldest_tick, tick::Tick newest_tick) {
  const double ticks =
      std::clamp(sample_time / tick_duration, static_cast<double>(oldest_tick), static_cast<double>(newest_tick));
  const double whole = std::floor(ticks);
  return SeenTime{.tick = static_cast<tick::Tick>(whole), .fraction = static_cast<float>(ticks - whole)};
}

animation::PoseInput PoseInputOf(const RemoteBody& body) {
  physics::BodyState state;
  state.position = body.position;
  state.velocity = body.velocity;
  state.stance = body.stance;
  state.stride = body.stride;
  state.previous_stance = body.previous_stance;
  state.stance_progress = body.stance_progress;
  state.grounded = body.grounded;
  return animation::PoseInput{.body = state, .yaw = body.yaw, .pitch = body.pitch, .ads = body.ads};
}

RemoteInterpolator::History::History(const Update& first) {
  slots_.reserve(kUpdatesKept);
  slots_.push_back(first);
}

void RemoteInterpolator::History::Push(const Update& update) {
  if (slots_.size() < kUpdatesKept) {
    slots_.push_back(update);
    return;
  }
  slots_[oldest_] = update;
  oldest_ = (oldest_ + 1) % kUpdatesKept;
}

const RemoteInterpolator::Update& RemoteInterpolator::History::operator[](std::size_t index) const {
  return slots_[(oldest_ + index) % slots_.size()];
}

void RemoteInterpolator::Record(EntityId entity, double server_time, const physics::BodyState& body, const Aim& aim) {
  const Update update{.server_time = server_time, .body = body, .aim = aim};
  const auto [found, added] = index_.try_emplace(entity, bodies_.size());
  if (added) {
    bodies_.push_back(Buffered{.entity = entity, .updates = History(update)});
    return;
  }
  History& updates = bodies_[found->second].updates;
  if (server_time > updates.Newest().server_time) {
    updates.Push(update);
  }
}

void RemoteInterpolator::Sync(std::span<const EntityId> current) {
  for (const EntityId entity : current) {
    if (const auto found = index_.find(entity); found != index_.end()) {
      bodies_[found->second].listed = true;
    }
  }
  const std::size_t forgotten = std::erase_if(bodies_, [](const Buffered& b) { return !b.listed; });
  for (Buffered& buffered : bodies_) {
    buffered.listed = false;
  }
  if (forgotten == 0) {
    return;
  }
  // Erasing moved every body after the first forgotten one.
  index_.clear();
  for (std::size_t position = 0; position < bodies_.size(); ++position) {
    index_.emplace(bodies_[position].entity, position);
  }
}

std::vector<RemotePlayer> RemoteInterpolator::Sample(double sample_time) const {
  std::vector<RemotePlayer> result;
  result.reserve(bodies_.size());
  for (const Buffered& buffered : bodies_) {
    const History& updates = buffered.updates;
    // The first update after sample_time; the one before it is the other end.
    const auto indices = std::views::iota(std::size_t{0}, updates.Size());
    const auto later = static_cast<std::size_t>(
        std::ranges::partition_point(indices,
                                     [&](std::size_t index) { return updates[index].server_time <= sample_time; }) -
        indices.begin());
    RemoteBody body;
    if (later == 0) {
      body = AsRemote(updates[0].body, updates[0].aim);
    } else if (later == updates.Size()) {
      body = AsRemote(updates.Newest().body, updates.Newest().aim);
    } else {
      const Update& earlier = updates[later - 1];
      const Update& next = updates[later];
      const auto t = static_cast<float>((sample_time - earlier.server_time) / (next.server_time - earlier.server_time));
      body = Between(earlier.body, earlier.aim, next.body, next.aim, t);
    }
    result.push_back(RemotePlayer{.entity = buffered.entity, .body = body, .character = {}});
  }
  return result;
}

}  // namespace augusta::presentation
