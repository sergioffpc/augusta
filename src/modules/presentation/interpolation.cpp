#include "augusta/interpolation.h"

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "augusta/math.h"
#include "augusta/physics.h"

namespace augusta::presentation {

namespace {

// Below this fraction of the way from the previous update to the latest, the
// nearer (previous) stance is shown; at or beyond it, the latest is - see
// Sample's use below.
constexpr float kMidpointFraction = 0.5F;

}  // namespace

void RemoteInterpolator::Record(SessionId session, float timestamp, const physics::BodyState& body) {
  const auto found = std::ranges::find_if(sessions_, [session](const Buffered& b) { return b.session == session; });
  if (found == sessions_.end()) {
    sessions_.push_back(
        Buffered{.session = session, .previous = std::nullopt, .latest = Update{.timestamp = timestamp, .body = body}});
    return;
  }
  if (timestamp <= found->latest.timestamp) {
    return;
  }
  found->previous = found->latest;
  found->latest = Update{.timestamp = timestamp, .body = body};
}

void RemoteInterpolator::Sync(std::span<const SessionId> current) {
  std::erase_if(sessions_,
                [current](const Buffered& b) { return std::ranges::find(current, b.session) == current.end(); });
}

std::vector<RemotePlayer> RemoteInterpolator::Sample(float render_time) const {
  std::vector<RemotePlayer> result;
  result.reserve(sessions_.size());
  for (const Buffered& buffered : sessions_) {
    RemoteBody body;
    if (!buffered.previous.has_value()) {
      body = RemoteBody{.position = buffered.latest.body.position,
                        .velocity = buffered.latest.body.velocity,
                        .stance = buffered.latest.body.stance};
    } else {
      const Update& previous = *buffered.previous;
      const Update& latest = buffered.latest;
      if (render_time <= previous.timestamp) {
        body = RemoteBody{
            .position = previous.body.position, .velocity = previous.body.velocity, .stance = previous.body.stance};
      } else if (render_time >= latest.timestamp) {
        body = RemoteBody{
            .position = latest.body.position, .velocity = latest.body.velocity, .stance = latest.body.stance};
      } else {
        const float span = latest.timestamp - previous.timestamp;
        const float t = (render_time - previous.timestamp) / span;
        body = RemoteBody{.position = math::Lerp(previous.body.position, latest.body.position, t),
                          .velocity = math::Lerp(previous.body.velocity, latest.body.velocity, t),
                          .stance = t < kMidpointFraction ? previous.body.stance : latest.body.stance};
      }
    }
    result.push_back(RemotePlayer{.session = buffered.session, .body = body});
  }
  return result;
}

}  // namespace augusta::presentation
