#include "admission.h"

#include <chrono>
#include <unordered_map>
#include <vector>

#include "augusta/link.h"

namespace augusta::host {

void AdmissionDeadlines::Connected(link::PeerId peer, std::chrono::steady_clock::time_point now) {
  connected_at_.insert_or_assign(peer, now);
}

void AdmissionDeadlines::Admitted(link::PeerId peer) { connected_at_.erase(peer); }

void AdmissionDeadlines::Left(link::PeerId peer) { connected_at_.erase(peer); }

std::vector<link::PeerId> AdmissionDeadlines::TakeOverdue(std::chrono::steady_clock::time_point now) {
  std::vector<link::PeerId> overdue;
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

}  // namespace augusta::host
