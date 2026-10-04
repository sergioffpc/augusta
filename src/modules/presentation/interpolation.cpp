#include "augusta/interpolation.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/tick.h"

namespace augusta::presentation {

namespace {

// Below this fraction of the way from the earlier update to the later, the
// earlier stance is shown; at or beyond it, the later one is - see Sample's
// use below.
constexpr float kMidpointFraction = 0.5F;

RemoteBody AsRemote(const physics::BodyState& body, float yaw) {
  return RemoteBody{.position = body.position, .velocity = body.velocity, .yaw = yaw, .stance = body.stance};
}

}  // namespace

void ServerClock::Advance(double elapsed) {
  if (!now_.has_value()) {
    return;
  }
  const double limit = kMaxSlew * elapsed;
  const double catch_up = std::clamp(behind_, -limit, limit);
  *now_ += elapsed + catch_up;
  behind_ -= catch_up;
}

void ServerClock::Observe(double server_time) {
  if (!now_.has_value() || std::abs(server_time - *now_) > kResyncThreshold) {
    now_ = server_time;
    behind_ = 0.0;
    return;
  }
  behind_ = server_time - *now_;
}

void ServerClock::Reset() {
  now_.reset();
  behind_ = 0.0;
}

std::optional<double> ServerClock::Now() const { return now_; }

SeenTime SeenTimeAt(double sample_time, double tick_duration, tick::Tick oldest_tick, tick::Tick newest_tick) {
  const double ticks =
      std::clamp(sample_time / tick_duration, static_cast<double>(oldest_tick), static_cast<double>(newest_tick));
  const double whole = std::floor(ticks);
  return SeenTime{.tick = static_cast<tick::Tick>(whole), .fraction = static_cast<float>(ticks - whole)};
}

void RemoteInterpolator::Record(EntityId entity, double server_time, const physics::BodyState& body, float yaw) {
  const Update update{.server_time = server_time, .body = body, .yaw = yaw};
  const auto found = std::ranges::find_if(bodies_, [entity](const Buffered& b) { return b.entity == entity; });
  if (found == bodies_.end()) {
    bodies_.push_back(Buffered{.entity = entity, .updates = {update}});
    return;
  }
  std::vector<Update>& updates = found->updates;
  if (server_time <= updates.back().server_time) {
    return;
  }
  updates.push_back(update);
  if (updates.size() > kUpdatesKept) {
    updates.erase(updates.begin());
  }
}

void RemoteInterpolator::Sync(std::span<const EntityId> current) {
  std::erase_if(bodies_,
                [current](const Buffered& b) { return std::ranges::find(current, b.entity) == current.end(); });
}

std::vector<RemotePlayer> RemoteInterpolator::Sample(double sample_time) const {
  std::vector<RemotePlayer> result;
  result.reserve(bodies_.size());
  for (const Buffered& buffered : bodies_) {
    const std::vector<Update>& updates = buffered.updates;
    // The first update after sample_time; the one before it is the other end.
    const auto later = std::ranges::upper_bound(updates, sample_time, {}, &Update::server_time);
    RemoteBody body;
    if (later == updates.begin()) {
      body = AsRemote(updates.front().body, updates.front().yaw);
    } else if (later == updates.end()) {
      body = AsRemote(updates.back().body, updates.back().yaw);
    } else {
      const Update& earlier = *std::prev(later);
      const auto t =
          static_cast<float>((sample_time - earlier.server_time) / (later->server_time - earlier.server_time));
      body = RemoteBody{
          .position = math::Lerp(earlier.body.position, later->body.position, t),
          .velocity = math::Lerp(earlier.body.velocity, later->body.velocity, t),
          .yaw = math::LerpAngle(earlier.yaw, later->yaw, t),
          .stance = t < kMidpointFraction ? earlier.body.stance : later->body.stance,
      };
    }
    result.push_back(RemotePlayer{.entity = buffered.entity, .body = body, .character = {}});
  }
  return result;
}

}  // namespace augusta::presentation
