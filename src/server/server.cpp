#include "server.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/heartbeat.h"
#include "augusta/host.h"
#include "augusta/logging.h"
#include "augusta/policy.h"
#include "augusta/policy_loader.h"
#include "augusta/replay_server.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"
#if AUGUSTA_DEBUG_TOOLS
#include "augusta/link.h"
#include "augusta/pcapng_file_writer.h"
#include "augusta/version.h"
#endif
#include "metrics.h"
#include "metrics_connection_health.h"

namespace augusta::server {

// What a ServerRuntime runs, whichever server it is.
struct ServerRuntime::Impl {
  Impl() = default;
  virtual ~Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  [[nodiscard]] virtual std::optional<failure::Failure> Run() = 0;
  virtual void Stop() = 0;
};

namespace {

// One tick of what a runtime runs: a live Host's of delta_time seconds, or a
// replay server's, whose every Replay ticks at the rate both run at.
void TickOnce(host::Host& host, float delta_time) { static_cast<void>(host.Tick(delta_time)); }
void TickOnce(host::ReplayServer& server, float /*delta_time*/) { server.Tick(); }

// The failure a strict capture lost a record on (Host::CaptureFailure), and
// the same once every capture is finished (Host::FinishCapture); a replay
// server captures nothing.
std::optional<failure::Failure> CaptureFailureOf(const host::Host& host) { return host.CaptureFailure(); }
std::optional<failure::Failure> CaptureFailureOf(const host::ReplayServer& /*server*/) { return std::nullopt; }
std::optional<failure::Failure> FinishCaptureOf(host::Host& host) { return host.FinishCapture(); }
std::optional<failure::Failure> FinishCaptureOf(host::ReplayServer& /*server*/) { return std::nullopt; }

#if AUGUSTA_DEBUG_TOOLS
// config, its transport tapped by tap.
template <typename Config>
Config Tapped(Config config, link::Tap* tap) {
  config.transport.tap = tap;
  return config;
}
#endif

}  // namespace

// The threads, the metrics endpoint and the supervisor around Served, a Host
// or a ReplayServer: the same for both, which differ only in what a tick does.
template <typename Served>
struct ServerRuntime::Running final : ServerRuntime::Impl {
  // The Simulation thread's fixed tick rate in Hz (NFR-01 asks it to sustain
  // 60 Hz, no missed ticks).
  std::uint8_t tick_rate_hz;
  std::uint16_t metrics_port;
#if AUGUSTA_DEBUG_TOOLS
  // This run's pcapng, if asked for: declared before served, whose transport
  // reports to it until it goes.
  std::unique_ptr<pcapng::FileWriter> pcapng;
#endif
  Served served;
  // When the Simulation thread last finished a tick, which the metrics
  // endpoint's /livez reads (ADR-0049). Run() starts it at its own start.
  std::atomic<tick::Clock::time_point> last_tick_end;
  // Every client's Connection health, which the Network I/O thread records
  // and the metrics endpoint collects.
  ConnectionHealth connection_health;
  // Null until Run() starts it, and if it could not start. Declared after
  // last_tick_end and connection_health, which it reads.
  std::unique_ptr<MetricsEndpoint> metrics;
  // What the supervisor asks when no test has given it faults: nothing is
  // ever armed in it.
  failure::Faults no_faults;
  // The two threads' stop request and first failure (ADR-0005). Declared after
  // served, so it stops and joins the Network I/O thread before served goes.
  supervisor::Supervisor workers;

  template <typename Config, typename Policy>
  Running(const Config& config, scenario::Scenario scenario, Policy policy, const RuntimeOptions& options)
      : tick_rate_hz(config.tick_rate_hz),
        metrics_port(options.metrics_port),
#if AUGUSTA_DEBUG_TOOLS
        pcapng(pcapng::RunWriter(options.network_pcapng, "augustad", EngineVersion())),
        served(Tapped(config, pcapng.get()), std::move(scenario), std::move(policy)),
#else
        served(config, std::move(scenario), std::move(policy)),
#endif
        workers(options.faults != nullptr ? *options.faults : no_faults) {
  }

  // The endpoint is not a supervised worker (ADR-0049): a server whose endpoint
  // can't start keeps running without it, and in the cluster its liveness
  // probe then fails. Its failure is the subsystem's (ADR-0033), reported here,
  // where it is recovered, as the endpoint reports one it meets later.
  void StartMetrics() {
    auto started = failure::Guard(failure::Code::kMetricsEndpointFailed, [this] {
      return std::make_unique<MetricsEndpoint>(metrics_port, last_tick_end,
                                               ServerMetrics{served.Metrics(), connection_health});
    });
    if (!started) {
      started.error().context.push_back({.key = "port", .value = std::to_string(metrics_port)});
      LOG_ERROR("subsystem=metrics event=unavailable {}", failure::DescribeFailure(started.error()));
      return;
    }
    metrics = *std::move(started);
    LOG_INFO("subsystem=serverruntime event=metrics_serving port={}", metrics_port);
  }

  // The runtime failure the server met on either thread, if any, for the
  // worker that takes it to stop on (ADR-0033): its local transport's, without
  // which the runtime cannot go on, or a message or record it could not
  // encode, a broken invariant. Each is taken once, so one worker reports it.
  supervisor::WorkerResult ServedResult() {
    if (std::optional<failure::Failure> failed = served.TakeTransportFailure()) {
      return std::unexpected(std::move(*failed));
    }
    if (std::optional<failure::Failure> broken = served.TakeInvariantFailure()) {
      return std::unexpected(std::move(*broken));
    }
    return {};
  }

  // Network I/O thread body (ADR-0005): pumps the connection, and once a
  // heartbeat interval samples every client's Connection health (ADR-0049),
  // until a stop is requested or the server meets a runtime failure, waiting
  // kNetworkRoundWait between rounds rather than spinning a core. The
  // transport has no wait on incoming work, so that wait bounds how late a
  // received message is handled, and how long stopping takes.
  supervisor::WorkerResult NetworkThreadMain() {
    constexpr auto kNetworkRoundWait = std::chrono::milliseconds(1);
    std::chrono::steady_clock::time_point next_sample = std::chrono::steady_clock::now() + host::kHeartbeatInterval;
    while (!workers.StopRequested()) {
      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      served.PumpNetwork(now);
      if (supervisor::WorkerResult failed = ServedResult(); !failed.has_value()) {
        return failed;
      }
      if (now >= next_sample) {
        connection_health.Record(served.SampleConnections());
        next_sample = now + host::kHeartbeatInterval;
      }
      std::this_thread::sleep_for(kNetworkRoundWait);
    }
    return {};
  }

  // The Simulation thread's ticks (ADR-0005): ticks the server on its fixed schedule
  // until a stop is requested or it meets a runtime failure, or until a strict
  // capture has lost a record, which is the runtime's failure: no tick runs
  // once it is known (ADR-0033, ADR-0050). Its writer finds a loss after the
  // tick that lost it, so a few ticks may run, uncaptured, before it is.
  supervisor::WorkerResult TickUntilStopped() {
    const auto delta_time = std::chrono::duration<float>(1.0F / tick_rate_hz);
    const auto tick_duration = std::chrono::duration_cast<tick::Clock::duration>(delta_time);
    LOG_INFO("subsystem=serverruntime event=loop_starting loop=simulation");
    tick::Clock::time_point deadline = tick::Clock::now();
    while (!workers.StopRequested()) {
      if (std::optional<failure::Failure> lost = CaptureFailureOf(served)) {
        return std::unexpected(*std::move(lost));
      }
      const tick::Clock::time_point tick_start = tick::Clock::now();

      TickOnce(served, delta_time.count());
      if (supervisor::WorkerResult failed = ServedResult(); !failed.has_value()) {
        return failed;
      }

      const tick::Clock::time_point tick_end = tick::Clock::now();
      served.RecordTiming(tick::Measure(deadline, tick_duration, tick_start, tick_end));
      last_tick_end.store(tick_end, std::memory_order_relaxed);
      deadline = tick::NextDeadline(deadline, tick_duration, tick_end);
      std::this_thread::sleep_until(deadline);
    }
    LOG_INFO("subsystem=serverruntime event=loop_stopping loop=simulation");
    return {};
  }

  // Simulation thread body: TickUntilStopped, then, however it ended, the
  // captures are finished (ADR-0050), so the metrics endpoint, which outlives
  // the workers, reads them stopped. A strict capture's loss its writer finds
  // only then still fails a run that would otherwise have succeeded.
  supervisor::WorkerResult SimulationLoop() {
    supervisor::WorkerResult result = TickUntilStopped();
    std::optional<failure::Failure> lost = FinishCaptureOf(served);
    if (result.has_value() && lost.has_value()) {
      return std::unexpected(*std::move(lost));
    }
    return result;
  }

  std::optional<failure::Failure> Run() override {
    last_tick_end.store(tick::Clock::now(), std::memory_order_relaxed);
    StartMetrics();
    workers.Spawn("network", [this] { return NetworkThreadMain(); });
    workers.Run("simulation", [this] { return SimulationLoop(); });
    workers.StopAndJoin();
    // A Simulation loop that never ran - a worker failed, or a stop came,
    // before it started - left the captures running: they are finished here,
    // still before Run returns. After one that ran, this changes nothing.
    std::optional<failure::Failure> lost = FinishCaptureOf(served);
    std::optional<failure::Failure> failed = workers.Failure();
    return failed.has_value() ? failed : lost;
  }

  void Stop() override { workers.RequestStop(); }
};

ServerRuntime::ServerRuntime(const host::HostConfig& config, scenario::Scenario scenario, policy::Policy policy,
                             const RuntimeOptions& options)
    : impl_(std::make_unique<Running<host::Host>>(config, std::move(scenario), std::move(policy), options)) {}

ServerRuntime::ServerRuntime(const host::ReplayServerConfig& config, scenario::Scenario scenario,
                             policy::PolicyMaker policy, const RuntimeOptions& options)
    : impl_(std::make_unique<Running<host::ReplayServer>>(config, std::move(scenario), std::move(policy), options)) {}

ServerRuntime::~ServerRuntime() = default;

std::optional<failure::Failure> ServerRuntime::Run() { return impl_->Run(); }

void ServerRuntime::Stop() { impl_->Stop(); }

}  // namespace augusta::server
