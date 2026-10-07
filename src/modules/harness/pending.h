#ifndef AUGUSTA_HARNESS_PENDING_H_
#define AUGUSTA_HARNESS_PENDING_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

/// \file
/// What the server sent that is handed out once, not read: Inbox keeps each
/// kind of such event (Shots, Hit confirmations, Deaths) in one until whoever
/// draws it takes it. Each is kept under the Match it is of, by the count of
/// Matches started (ServerView::matches_started) when it arrived, so that a
/// reader takes only the events of the Match its Server view is of. Private to
/// the harness module.
namespace augusta::harness {

/// Events kept oldest first, no more than the newest limit of them, each under
/// the Match it is of. Safe to use from any thread: the Network I/O thread adds
/// and another takes.
template <typename Event>
class Pending {
 public:
  explicit Pending(std::size_t limit) : limit_(limit) {}

  /// Keeps event as one of match. Matches only start after one another, so an
  /// event of an earlier match still kept is no one's to take any more: it goes.
  void Add(std::uint32_t match, const Event& event) {
    const std::lock_guard<std::mutex> lock(mutex_);
    DropBefore(match);
    events_.push_back({.match = match, .event = event});
    if (events_.size() > limit_) {
      events_.pop_front();
    }
  }

  /// Every event of match kept, oldest first; none of it is kept after, nor any
  /// of an earlier match. Those of a later match are kept for a reader that has
  /// seen it start.
  std::vector<Event> Take(std::uint32_t match) {
    const std::lock_guard<std::mutex> lock(mutex_);
    DropBefore(match);
    std::vector<Event> taken;
    while (!events_.empty() && events_.front().match == match) {
      taken.push_back(events_.front().event);
      events_.pop_front();
    }
    return taken;
  }

 private:
  struct Kept {
    std::uint32_t match = 0;
    Event event{};
  };

  // Events are kept in the order they arrived, so by match: an earlier match's are at the front.
  void DropBefore(std::uint32_t match) {
    while (!events_.empty() && events_.front().match < match) {
      events_.pop_front();
    }
  }

  std::size_t limit_;
  std::mutex mutex_;
  std::deque<Kept> events_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_HARNESS_PENDING_H_
