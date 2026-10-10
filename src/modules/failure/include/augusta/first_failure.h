#ifndef AUGUSTA_FIRST_FAILURE_H_
#define AUGUSTA_FIRST_FAILURE_H_

#include <mutex>
#include <optional>
#include <utility>

#include "augusta/failure.h"

/// \file
/// Where a runtime's shared component (server::Host, harness::Session) keeps
/// the first failure any of its threads met inside a call that cannot return
/// it - a send in the middle of a tick, say - until one of the runtime's
/// workers takes it and escalates it to the supervisor (supervisor.h,
/// ADR-0033). Taken once, so exactly one worker reports it; that a failure was
/// recorded stays known after it is taken, so the component sends nothing more
/// while its workers stop. Safe from any thread.
namespace augusta::failure {

/// The first failure recorded, until taken.
class FirstFailure {
 public:
  /// Keeps failure if it is the first ever recorded; a later one is its
  /// consequence, and is dropped.
  void Record(Failure failure) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!recorded_) {
      recorded_ = true;
      failure_ = std::move(failure);
    }
  }

  /// The first failure, once: nullopt before it is recorded and after it has
  /// been taken.
  [[nodiscard]] std::optional<Failure> Take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(failure_, std::nullopt);
  }

  /// Whether a failure has ever been recorded, taken or not: once one has, the
  /// runtime has failed, and whatever keeps it sends nothing more.
  [[nodiscard]] bool Recorded() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return recorded_;
  }

 private:
  mutable std::mutex mutex_;
  bool recorded_ = false;
  std::optional<Failure> failure_;
};

}  // namespace augusta::failure

#endif  // AUGUSTA_FIRST_FAILURE_H_
