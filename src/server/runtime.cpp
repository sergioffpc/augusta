#include "runtime.h"

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "augusta/logging.h"
#include "augusta/scripting.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"
#include "host.h"

namespace augusta::server {

struct ServerRuntime::Impl {
  RuntimeConfig config;
  Host host;
  // The two threads' stop request and first failure (ADR-0005). Declared after
  // host, so it stops and joins the Network I/O thread before host goes.
  supervisor::Supervisor workers;

  Impl(const RuntimeConfig& cfg, Map map, scripting::Engine policy)
      : config(cfg),
        host(
            HostConfig{
                .tick_rate_hz = cfg.tick_rate_hz,
                .parameters = cfg.parameters,
                .listen = cfg.listen,
            },
            std::move(map), std::move(policy)) {}

  // Network I/O thread body (ADR-0005): pumps the connection until a stop is
  // requested, waiting kNetworkRoundWait between rounds rather than spinning a
  // core. The transport has no wait on incoming work, so that wait bounds how
  // late a received message is handled, and how long stopping takes.
  void NetworkThreadMain() {
    constexpr auto kNetworkRoundWait = std::chrono::milliseconds(1);
    while (!workers.StopRequested()) {
      host.PumpNetwork(std::chrono::steady_clock::now());
      std::this_thread::sleep_for(kNetworkRoundWait);
    }
  }

  // Simulation thread body (ADR-0005): ticks Host on its fixed schedule until a
  // stop is requested.
  void SimulationLoop() {
    const auto delta_time = std::chrono::duration<float>(1.0F / config.tick_rate_hz);
    const auto tick_duration = std::chrono::duration_cast<tick::Clock::duration>(delta_time);
    LI("subsystem=serverruntime event=loop_starting loop=simulation");
    tick::Clock::time_point deadline = tick::Clock::now();
    while (!workers.StopRequested()) {
      const tick::Clock::time_point tick_start = tick::Clock::now();

      host.Tick(delta_time.count());

      const tick::Clock::time_point tick_end = tick::Clock::now();
      host.RecordTiming(tick::Measure(deadline, tick_duration, tick_start, tick_end));
      deadline = tick::NextDeadline(deadline, tick_duration, tick_end);
      std::this_thread::sleep_until(deadline);
    }
    LI("subsystem=serverruntime event=loop_stopping loop=simulation");
  }
};

ServerRuntime::ServerRuntime(const RuntimeConfig& config, Map map, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(config, std::move(map), std::move(policy))) {}

ServerRuntime::~ServerRuntime() = default;

std::optional<supervisor::WorkerFailure> ServerRuntime::Run() {
  Impl& impl = *impl_;
  impl.workers.Spawn("network", [&impl] { impl.NetworkThreadMain(); });
  impl.workers.Run("simulation", [&impl] { impl.SimulationLoop(); });
  impl.workers.StopAndJoin();
  return impl.workers.Failure();
}

void ServerRuntime::Stop() { impl_->workers.RequestStop(); }

}  // namespace augusta::server
