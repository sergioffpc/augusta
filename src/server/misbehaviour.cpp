#include "misbehaviour.h"

#include <chrono>
#include <string_view>
#include <utility>

namespace augusta::server {

std::string_view DescribePeerRejection(PeerRejection rejection) {
  switch (rejection) {
    case PeerRejection::kUndecodable:
      return "undecodable message";
    case PeerRejection::kNotAClientMessage:
      return "not a client message";
    case PeerRejection::kNonFiniteCommand:
      return "non-finite value";
    case PeerRejection::kOutOfRangeCommand:
      return "value out of range";
    case PeerRejection::kCommandsBeforeJoining:
      return "commands before joining";
    case PeerRejection::kStaleCommand:
      return "sequence not newer";
    case PeerRejection::kCommandsOutsideMatch:
      return "commands outside a match";
    case PeerRejection::kStaleReady:
      return "ready for an old roster";
    case PeerRejection::kJoinRefused:
      return "join refused";
  }
  std::unreachable();
}

Verdict MisbehaviourTracker::Record(PeerRejection rejection, std::chrono::steady_clock::time_point now) {
  if (!IsMisbehaviour(rejection)) {
    return Verdict::kKeep;
  }
  while (!recent_.empty() && now - recent_.front() >= kMisbehaviourWindow) {
    recent_.pop_front();
  }
  recent_.push_back(now);
  return recent_.size() >= kMisbehaviourThreshold ? Verdict::kDisconnect : Verdict::kKeep;
}

}  // namespace augusta::server
