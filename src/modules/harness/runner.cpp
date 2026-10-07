#include "augusta/runner.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>

#include <nvtx3/nvtx3.hpp>

#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/prediction.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"

namespace augusta::harness {

namespace {

// What the prediction did since its last heartbeat line: once a second, one
// line of it, where a line per tick would bury the one that matters.
// Prediction thread only.
class PredictionActivity {
 public:
  void Record(const prediction::State& state, std::chrono::steady_clock::time_point now) {
    ++ticks_;
    // Reconciliation makes at most one jump per tick, so the change in the
    // running total is that tick's jump.
    const float jump = math::Length(state.total_correction - last_total_correction_);
    last_total_correction_ = state.total_correction;
    if (jump > 0.0F) {
      ++corrections_;
      correction_m_ += jump;
    }
    if (now - since_ >= kInterval) {
      LD("subsystem=harness event=heartbeat ticks={} corrections={} correction_m={:.3f}", ticks_, corrections_,
         correction_m_);
      ticks_ = 0;
      corrections_ = 0;
      correction_m_ = 0.0F;
      since_ = now;
    }
  }

 private:
  static constexpr std::chrono::seconds kInterval{1};
  std::chrono::steady_clock::time_point since_ = std::chrono::steady_clock::now();
  math::Vec3 last_total_correction_{};
  std::uint32_t ticks_ = 0;
  std::uint32_t corrections_ = 0;
  float correction_m_ = 0.0F;
};

// How long the next Tick lasts: the server's tick, paced by how many of the
// client's commands the server last said it held (tick.h).
tick::Clock::duration NextTickDuration(const Session& session, tick::Clock::duration nominal) {
  const std::optional<AuthoritativeState> state = session.GetAuthoritativeState();
  return state.has_value() ? tick::PacedTickDuration(nominal, state->queued_commands) : nominal;
}

}  // namespace

Runner::Runner(Session& session, RunnerHooks hooks) : session_(session), hooks_(std::move(hooks)) {
  workers_.Spawn("prediction", [this] { PredictionThreadMain(); });
  workers_.Spawn("network", [this] { NetworkThreadMain(); });
}

Runner::~Runner() { workers_.StopAndJoin(); }

std::optional<failure::Failure> Runner::Failure() const { return workers_.Failure(); }

std::optional<float> Runner::WaitForTickRate() {
  constexpr auto kPollInterval = std::chrono::milliseconds(10);
  while (!workers_.StopRequested()) {
    if (const auto rate = session_.GetTickRate()) {
      return rate;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return std::nullopt;
}

void Runner::PredictionThreadMain() {
  const auto tick_rate_hz = WaitForTickRate();
  if (!tick_rate_hz.has_value()) {
    return;
  }
  const auto delta_time = std::chrono::duration<float>(1.0F / *tick_rate_hz);
  const auto nominal_tick = std::chrono::duration_cast<tick::Clock::duration>(delta_time);
  PredictionActivity activity;
  tick::Clock::time_point deadline = tick::Clock::now();
  while (!workers_.StopRequested()) {
    const nvtx3::scoped_range range{"Prediction Tick"};
    const tick::Clock::time_point tick_start = tick::Clock::now();

    const prediction::State state = session_.Tick(hooks_.next_command(), delta_time.count());
    activity.Record(state, tick_start);

    // The tick spans its schedule, not its wake-ups, so a reader blending
    // ticks blends them evenly.
    const tick::Clock::time_point due = deadline;
    deadline = tick::NextDeadline(deadline, NextTickDuration(session_, nominal_tick), tick::Clock::now());
    if (hooks_.on_tick) {
      hooks_.on_tick({.state = state, .due = due, .duration = deadline - due});
    }

    std::this_thread::sleep_until(deadline);
  }
}

// The transport has no wait on incoming work, so the wait between rounds
// bounds how late a received message is handled, and how long stopping takes.
void Runner::NetworkThreadMain() {
  constexpr auto kNetworkRoundWait = std::chrono::milliseconds(1);
  session_.Connect();
  while (!workers_.StopRequested()) {
    {
      const nvtx3::scoped_range range{"Network PumpEvents"};
      session_.PumpEvents();
      session_.ExchangeMessages();
    }
    if (hooks_.on_network_round) {
      hooks_.on_network_round();
    }
    std::this_thread::sleep_for(kNetworkRoundWait);
  }
  session_.Disconnect();
}

}  // namespace augusta::harness
