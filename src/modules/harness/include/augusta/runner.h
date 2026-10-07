#ifndef AUGUSTA_RUNNER_H_
#define AUGUSTA_RUNNER_H_

#include <functional>
#include <optional>

#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/prediction.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"

/// \file
/// Runs a harness::Session in real time, on the Prediction and Network I/O
/// threads of ADR-0005: anything that plays live - the real client, a future
/// autonomous agent, a load test's clients - runs its Session under one, and
/// supplies only the Command for each Tick. A test that ticks by hand drives
/// the Session itself, without one.
///
/// The Network I/O thread connects, then does one round of the Session's
/// network work (PumpEvents, ExchangeMessages) about every millisecond until
/// the stop. The Prediction thread waits for the server to admit the client,
/// then Ticks the Session at the server's tick rate on a fixed schedule
/// (augusta::tick), each Tick paced by how many of the client's commands the
/// server last said it held, so the client sends them at the rate the server
/// consumes them.
namespace augusta::harness {

/// One Tick a Runner ran: the Prediction State it left, when it was due, and
/// how long until the next is due.
struct PredictedTick {
  prediction::State state;
  tick::Clock::time_point due;
  tick::Clock::duration duration{};
};

/// What the caller of a Runner supplies, and what it is told, each on the
/// thread it is called from.
struct RunnerHooks {
  /// The Command for the coming Tick. Prediction thread.
  std::function<command::Command()> next_command;
  /// Each Tick, once it has run. Prediction thread; may be empty.
  std::function<void(const PredictedTick&)> on_tick;
  /// Each round of network work, once it is done. Network I/O thread, so the
  /// Session's Network I/O calls (GetConnectionStats) are safe; may be empty.
  std::function<void()> on_network_round;
};

/// The Prediction and Network I/O threads one Session runs on.
class Runner {
 public:
  /// Starts both threads on session, which must outlive the Runner and be
  /// driven by nothing else while it runs.
  Runner(Session& session, RunnerHooks hooks);
  /// Stops both threads and joins them; the Network I/O thread disconnects
  /// the Session first.
  ~Runner();

  /// Not copyable or movable: its threads hold a reference to it.
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;
  Runner(Runner&&) = delete;
  Runner& operator=(Runner&&) = delete;

  /// The first cause a thread stopped on (an exception from a hook's
  /// included), or nullopt while none has; the other thread is stopped too.
  /// Safe from any thread.
  [[nodiscard]] std::optional<failure::Failure> Failure() const;

 private:
  void PredictionThreadMain();
  void NetworkThreadMain();
  // The server's tick rate once it has admitted the client, or nullopt if
  // the stop came first.
  std::optional<float> WaitForTickRate();

  Session& session_;
  RunnerHooks hooks_;
  // Last, so its threads are joined before anything they use goes.
  supervisor::Supervisor workers_;
};

}  // namespace augusta::harness

#endif  // AUGUSTA_RUNNER_H_
