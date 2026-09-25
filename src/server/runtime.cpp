#include "runtime.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include "augusta/logging.h"
#include "augusta/tick.h"
#include "host.h"

namespace augusta::runtime {

namespace {

// Stops Impl's Network I/O thread and joins it, on scope exit -
// including when unwinding past Run() due to an exception from the
// Simulation loop body. This is the only place thread cleanup happens;
// ~ServerRuntime relies on Run() having already run it (see that
// destructor's own doc comment in runtime.h).
struct ThreadJoiner {
  std::atomic<bool>& running;
  std::thread& network_thread;

  ~ThreadJoiner() {
    running.store(false, std::memory_order_relaxed);
    if (network_thread.joinable()) {
      network_thread.join();
    }
  }
};

}  // namespace

struct ServerRuntime::Impl {
  Config config;
  server::Host host;

  std::atomic<bool> running{false};
  std::thread network_thread;

  Impl(const Config& cfg, server::Map map)
      : config(cfg),
        host(
            server::HostConfig{
                .tick_rate_hz = cfg.tick_rate_hz,
                .parameters = cfg.parameters,
                .listen = cfg.listen,
            },
            std::move(map)) {}

  // Network I/O thread body (ADR-0005): pumps the connection until running is
  // cleared by ThreadJoiner or Stop().
  void NetworkThreadMain() {
    while (running.load(std::memory_order_relaxed)) {
      host.PumpNetwork();
    }
  }
};

ServerRuntime::ServerRuntime(const Config& config, server::Map map)
    : impl_(std::make_unique<Impl>(config, std::move(map))) {}

ServerRuntime::~ServerRuntime() = default;

void ServerRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running, .network_thread = impl_->network_thread};

  const auto delta_time = std::chrono::duration<float>(1.0F / impl_->config.tick_rate_hz);
  const auto tick_duration = std::chrono::duration_cast<tick::Clock::duration>(delta_time);
  LI("subsystem=serverruntime event=loop_starting loop=simulation");
  tick::Clock::time_point deadline = tick::Clock::now();
  while (impl_->running.load(std::memory_order_relaxed)) {
    const tick::Clock::time_point tick_start = tick::Clock::now();

    impl_->host.Tick(delta_time.count());

    const tick::Clock::time_point tick_end = tick::Clock::now();
    impl_->host.RecordTiming(tick::Measure(deadline, tick_duration, tick_start, tick_end));
    deadline = tick::NextDeadline(deadline, tick_duration, tick_end);
    std::this_thread::sleep_until(deadline);
  }
  LI("subsystem=serverruntime event=loop_stopping loop=simulation");
}

void ServerRuntime::Stop() { impl_->running.store(false, std::memory_order_relaxed); }

}  // namespace augusta::runtime
