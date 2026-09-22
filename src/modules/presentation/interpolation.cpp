#include "augusta/interpolation.h"

#include <algorithm>

namespace augusta::presentation {

void RemoteInterpolator::Record(protocol::SessionId session, float timestamp, const physics::BodyState& body) {
  const auto found =
      std::find_if(sessions_.begin(), sessions_.end(), [session](const Buffered& b) { return b.session == session; });
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

void RemoteInterpolator::Sync(std::span<const protocol::SessionId> current) {
  std::erase_if(sessions_, [current](const Buffered& b) {
    return std::find(current.begin(), current.end(), b.session) == current.end();
  });
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
                          .stance = t < 0.5F ? previous.body.stance : latest.body.stance};
      }
    }
    result.push_back(RemotePlayer{.session = buffered.session, .body = body});
  }
  return result;
}

}  // namespace augusta::presentation
