#include "admission.h"

#include <chrono>
#include <unordered_map>
#include <vector>

#include "augusta/networking.h"

namespace augusta::server {

void AdmissionDeadlines::Connected(networking::PeerId peer, std::chrono::steady_clock::time_point now) {
  connected_at_.insert_or_assign(peer, now);
}

void AdmissionDeadlines::Admitted(networking::PeerId peer) { connected_at_.erase(peer); }

void AdmissionDeadlines::Left(networking::PeerId peer) { connected_at_.erase(peer); }

std::vector<networking::PeerId> AdmissionDeadlines::TakeOverdue(std::chrono::steady_clock::time_point now) {
  std::vector<networking::PeerId> overdue;
  std::erase_if(connected_at_, [&](const auto& entry) {
    const auto& [peer, connected_at] = entry;
    if (now - connected_at < kAdmissionDeadline) {
      return false;
    }
    overdue.push_back(peer);
    return true;
  });
  return overdue;
}

}  // namespace augusta::server
