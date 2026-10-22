#include "augusta/debug_subscriptions.h"

#include <bit>
#include <cstdint>
#include <string>

#include "augusta/link.h"
#include "augusta/link_types.h"

namespace augusta::host {

std::uint8_t DebugSubscriptions::Subscribe(link::PeerId peer, std::uint8_t flags) {
  Peer& subscribed = peers_[peer];
  subscribed.flags = flags;
  const auto unserved = static_cast<std::uint8_t>(flags & ~served_);
  const auto first_seen = static_cast<std::uint8_t>(unserved & ~subscribed.unserved_seen);
  subscribed.unserved_seen |= unserved;
  return first_seen;
}

bool DebugSubscriptions::Wants(link::PeerId peer, std::uint8_t stream) const {
  const auto found = peers_.find(peer);
  return (served_ & stream) != 0 && found != peers_.end() && (found->second.flags & stream) != 0;
}

void DebugSubscriptions::Forget(link::PeerId peer) { peers_.erase(peer); }

std::string DebugStreamName(std::uint8_t bit) {
  if (bit == link::DebugSubscribe::kShotJudgements) {
    return "shot_judgements";
  }
  return "bit" + std::to_string(std::countr_zero(bit));
}

}  // namespace augusta::host
