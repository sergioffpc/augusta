#ifndef AUGUSTA_HARNESS_PENDING_H_
#define AUGUSTA_HARNESS_PENDING_H_

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

/// \file
/// What the server sent that is handed out once, not read: Inbox keeps each
/// kind of such event (Shots, Hit confirmations, Deaths) in one until whoever
/// draws it takes it. Private to the harness module.
namespace augusta::harness {

/// Events kept oldest first, no more than the newest limit of them. Safe to use
/// from any thread: the Network I/O thread adds and another takes.
template <typename Event>
class Pending {
 public:
  explicit Pending(std::size_t limit) : limit_(limit) {}

  void Add(const Event& event) {
    const std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    if (events_.size() > limit_) {
      events_.pop_front();
    }
  }

  /// Every event kept, oldest first; none is kept after.
  std::vector<Event> Take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> taken(events_.begin(), events_.end());
    events_.clear();
    return taken;
  }

  void Clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
  }

 private:
  std::size_t limit_;
  std::mutex mutex_;
  std::deque<Event> events_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_PENDING_H_
