#include "augusta/agent_report.h"

#include <cstdint>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace augusta::agent {

void ReportQueue::Put(std::uint64_t agent, Report report) {
  {
    const std::scoped_lock lock(mutex_);
    if (closed_) {
      return;
    }
    reports_.push_back({.agent = agent, .report = std::move(report)});
  }
  put_.notify_one();
}

std::vector<AddressedReport> ReportQueue::TakeAll() {
  std::unique_lock lock(mutex_);
  put_.wait(lock, [this] { return closed_ || !reports_.empty(); });
  if (closed_) {
    return {};
  }
  std::vector<AddressedReport> taken(std::make_move_iterator(reports_.begin()),
                                     std::make_move_iterator(reports_.end()));
  reports_.clear();
  return taken;
}

void ReportQueue::Close() {
  {
    const std::scoped_lock lock(mutex_);
    closed_ = true;
    reports_.clear();
  }
  put_.notify_all();
}

}  // namespace augusta::agent
