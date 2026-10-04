#ifndef AUGUSTA_SUPERVISOR_H_
#define AUGUSTA_SUPERVISOR_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/// \file
/// The runtime supervisor both runtimes run their fixed threads under (ADR-0005):
/// it owns the workers' lifetimes, the one stop request every loop watches, and
/// the first terminal error. A worker that fails publishes its error here and
/// requests the stop instead of terminating the process: an exception escaping
/// a std::thread would call std::terminate. The runtime then stops and joins
/// every worker and reports the failure the way it reports any other.
namespace augusta::supervisor {

/// A worker that stopped on an exception: which thread, and what it said.
struct WorkerFailure {
  std::string thread;
  std::string reason;
};

/// A sentence saying which thread failed and why, for logs and for the player.
[[nodiscard]] std::string DescribeWorkerFailure(const WorkerFailure& failure);

/// Owns a runtime's worker threads, its stop request and its first failure.
class Supervisor {
 public:
  Supervisor() = default;
  /// Stops and joins every worker still running, as StopAndJoin does.
  ~Supervisor();

  /// Not copyable or movable: its workers hold a reference to it.
  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;
  Supervisor(Supervisor&&) = delete;
  Supervisor& operator=(Supervisor&&) = delete;

  /// Runs body on a new thread, the worker named thread. body runs until it
  /// returns, which a loop does once StopRequested; an exception escaping it is
  /// recorded as the failure, if it is the first, and requests the stop.
  void Spawn(std::string_view thread, std::function<void()> body);

  /// Runs body on the calling thread, as the worker named thread, with the
  /// same failure handling as Spawn. Returns when body does.
  void Run(std::string_view thread, const std::function<void()>& body);

  /// Asks every worker to stop. Safe from any thread and from a signal
  /// handler: one lock-free atomic store.
  void RequestStop();

  /// Whether a stop has been requested: by RequestStop, StopAndJoin or a failure.
  [[nodiscard]] bool StopRequested() const;

  /// The first worker failure, or nullopt while none has failed.
  [[nodiscard]] std::optional<WorkerFailure> Failure() const;

  /// Requests the stop and waits for every spawned worker to return. Calling
  /// it again does nothing more.
  void StopAndJoin();

 private:
  // Runs body, turning an exception escaping it into the failure.
  void Supervise(std::string_view thread, const std::function<void()>& body);
  void RecordFailure(std::string_view thread, std::string_view reason);

  std::atomic<bool> stop_requested_{false};
  static_assert(std::atomic<bool>::is_always_lock_free, "RequestStop must be safe from a signal handler");
  mutable std::mutex failure_mutex_;
  std::optional<WorkerFailure> failure_;
  // Owner thread only: Spawn and StopAndJoin.
  std::vector<std::thread> workers_;
};

}  // namespace augusta::supervisor

#endif  // AUGUSTA_SUPERVISOR_H_
