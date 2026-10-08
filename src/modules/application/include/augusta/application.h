#ifndef AUGUSTA_APPLICATION_H_
#define AUGUSTA_APPLICATION_H_

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "augusta/failure.h"

/// \file
/// The application boundary augustac's and augustad's main() run behind
/// (ADR-0033): it takes an executable from its process-wide initialization,
/// through constructing its runtime, to running it, and ends with one
/// classified Outcome whatever failed and however - a typed failure returned,
/// or a dependency's exception, which goes no further than the phase it escaped.
/// Conclude then writes the executable's one terminal log event and picks its
/// exit status. What each phase does, and how a failure is classified within
/// it, is the executable's (src/client, src/server); the runtime supervisor
/// (supervisor.h) is what hands a runtime's first cause back to it. Reading the
/// command line and config file comes first, outside the Lifecycle, since the
/// log level is set from it; its failure is concluded the same way.
namespace augusta::application {

/// How an executable ended: nullopt when it stopped as asked (the window was
/// closed, a SIGTERM), else the one failure that ended it, with its Code as
/// given - a runtime's first cause keeps the one the supervisor recorded.
using Outcome = std::optional<failure::Failure>;

/// The exit status of an executable that stopped as asked.
inline constexpr int kStoppedExitStatus = 0;
/// The exit status of one that ended on a failure, whatever its Disposition:
/// the terminal event's code, not the status, says which.
inline constexpr int kFailedExitStatus = 1;

/// An executable's phases after its config is read, each run once, in order,
/// on the thread that calls Execute.
template <typename Runtime>
struct Lifecycle {
  /// Process-wide dependencies every runtime needs (the transport), set up
  /// before any runtime exists.
  std::function<std::expected<void, failure::Failure>()> initialize;
  /// Loads what the runtime is made from and constructs it; starts no thread.
  std::function<std::expected<std::unique_ptr<Runtime>, failure::Failure>()> construct;
  /// Runs the runtime until it stops: returns only once it has stopped and
  /// joined its workers, with its first cause, or nullopt if it was asked to stop.
  std::function<Outcome(Runtime&)> run;
};

namespace detail {

// What one phase returned, or the failure an exception escaping it became,
// under code and with the phase named in its context.
template <typename Body>
auto Phase(std::string_view phase, failure::Code code, const Body& body) -> std::invoke_result_t<const Body&> {
  auto guarded = failure::Guard(code, body);
  if (!guarded) {
    guarded.error().context.push_back({.key = "phase", .value = std::string(phase)});
    return std::invoke_result_t<const Body&>(std::unexpect, std::move(guarded.error()));
  }
  return *std::move(guarded);
}

}  // namespace detail

/// Runs lifecycle's phases in order until one fails, and returns how the
/// executable ended. An exception escaping initialize or construct becomes
/// failure::Code::kDependencyInitFailed, and one escaping run - the calling
/// thread is then the runtime's last worker - kWorkerFailed, each with
/// `phase=` naming where it escaped. The runtime is released before this
/// returns, so the outcome is reported after everything it owned has gone.
/// Never throws.
template <typename Runtime>
[[nodiscard]] Outcome Execute(const Lifecycle<Runtime>& lifecycle) {
  if (auto initialized = detail::Phase("initialize", failure::Code::kDependencyInitFailed, lifecycle.initialize);
      !initialized) {
    return std::move(initialized.error());
  }
  auto runtime = detail::Phase("construct", failure::Code::kDependencyInitFailed, lifecycle.construct);
  if (!runtime) {
    return std::move(runtime.error());
  }
  const auto run = [&]() -> std::expected<Outcome, failure::Failure> { return lifecycle.run(**runtime); };
  auto ran = detail::Phase("run", failure::Code::kWorkerFailed, run);
  runtime->reset();
  return ran ? *std::move(ran) : Outcome(std::move(ran.error()));
}

/// Writes the executable's one terminal log event for outcome (ADR-0029) and
/// returns the exit status main() returns: `subsystem=<executable>
/// event=terminal_failure <the failure> exit_status=1` at CRIT for a failure,
/// `event=exiting exit_status=0` at INFO otherwise. The only line the
/// application boundary writes; executable is the subsystem, e.g. "server".
[[nodiscard]] int Conclude(std::string_view executable, const Outcome& outcome);

}  // namespace augusta::application

#endif  // AUGUSTA_APPLICATION_H_
