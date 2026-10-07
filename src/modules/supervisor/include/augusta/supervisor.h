#ifndef AUGUSTA_SUPERVISOR_H_
#define AUGUSTA_SUPERVISOR_H_

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "augusta/failure.h"
#include "augusta/faults.h"

/// \file
/// The runtime supervisor both runtimes run their fixed threads under (ADR-0005):
/// it owns the workers' lifetimes, the one stop request every loop watches, and
/// the first terminal failure, the runtime's first cause (ADR-0033). A worker
/// that cannot be started, whose body returns a failure, or whose body throws,
/// becomes that failure if it is the first and requests the stop instead of
/// terminating the process: an exception escaping a std::thread would call
/// std::terminate. Once the stop is requested no new work is admitted; the
/// runtime then stops and joins every worker before releasing what they use,
/// and hands the first cause to its application boundary to report.
namespace augusta::supervisor {

/// What a worker's body returns: nothing once it has returned at a stop, or
/// the failure it stopped on, which the Supervisor keeps with its own Code,
/// and so its Disposition, as the first cause if it is the first. The typed
/// way a worker escalates a failure it can name (a transport, an invariant);
/// a body that throws instead is failure::Code::kWorkerFailed.
using WorkerResult = std::expected<void, failure::Failure>;

/// The context key a Supervisor names the failed worker under, e.g. `thread=network`.
inline constexpr std::string_view kThreadContextKey = "thread";

/// Where a Supervisor is in its one-way life.
enum class State : std::uint8_t {
  /// Workers are admitted and run.
  kRunning,
  /// The stop is requested: no new work is admitted, and running workers return
  /// at their loop's next check.
  kStopping,
  /// Every spawned worker has returned and been joined.
  kStopped,
};

/// Owns a runtime's worker threads, its stop request and its first cause.
class Supervisor {
 public:
  Supervisor() = default;
  /// Asks faults at each worker's creation and before its body runs, so a test
  /// can make either fail; faults must outlive the Supervisor.
  explicit Supervisor(failure::Faults& faults);
  /// Stops and joins every worker still running, as StopAndJoin does.
  ~Supervisor();

  /// Not copyable or movable: its workers hold a reference to it.
  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;
  Supervisor(Supervisor&&) = delete;
  Supervisor& operator=(Supervisor&&) = delete;

  /// Runs body on a new thread, the worker named thread. body runs until it
  /// returns, which a loop does once StopRequested. A failure it returns, an
  /// exception escaping it (failure::Code::kWorkerFailed) or a thread that
  /// cannot be started (kWorkerCreationFailed) is recorded as the first
  /// cause, if it is the first, and requests the stop. Never throws. Once the
  /// stop is requested body is not run. Owner thread only.
  void Spawn(std::string_view thread, std::function<WorkerResult()> body);

  /// Runs body on the calling thread, as the worker named thread, with the
  /// same failure handling and admission as Spawn. Returns when body does.
  void Run(std::string_view thread, const std::function<WorkerResult()>& body);

  /// Asks every worker to stop. Safe from any thread and from a signal
  /// handler: one lock-free atomic operation. Only the first request moves it
  /// to State::kStopping; later ones change nothing.
  void RequestStop();

  /// Whether a stop has been requested: by RequestStop, StopAndJoin or a failure.
  [[nodiscard]] bool StopRequested() const;

  [[nodiscard]] State GetState() const;

  /// The first cause: the first worker failure, with the worker's name added
  /// to its context under kThreadContextKey, or nullopt while none has failed.
  /// A worker failing after a stop requested from outside, with no failure
  /// before it, is still the first cause: the runtime did not stop cleanly.
  /// Safe from any thread.
  [[nodiscard]] std::optional<failure::Failure> Failure() const;

  /// Requests the stop and waits for every spawned worker to return, leaving
  /// State::kStopped. Calling it again does nothing more. Owner thread only.
  void StopAndJoin();

 private:
  // Runs body, recording the failure it returns or the exception escaping
  // it. Admission is Spawn's and Run's: a worker spawned before the stop still
  // runs, and returns at its loop's first check.
  void Supervise(std::string_view thread, const std::function<WorkerResult()>& body);
  void RecordFailure(std::string_view thread, failure::Failure failure);

  // Null outside tests.
  failure::Faults* faults_ = nullptr;
  std::atomic<State> state_{State::kRunning};
  static_assert(std::atomic<State>::is_always_lock_free, "RequestStop must be safe from a signal handler");
  mutable std::mutex failure_mutex_;
  std::optional<failure::Failure> failure_;
  // Owner thread only: Spawn and StopAndJoin.
  std::vector<std::thread> workers_;
};

}  // namespace augusta::supervisor

#endif  // AUGUSTA_SUPERVISOR_H_
