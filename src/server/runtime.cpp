#include "runtime.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "augusta/logging.h"
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

  explicit Impl(const Config& cfg)
      : config(cfg),
        host(server::HostConfig{
            .parameters = cfg.parameters,
            .script_path = cfg.script_path,
            .listen = cfg.listen,
            .collision = cfg.collision,
            .spawn_points = cfg.spawn_points,
        }) {}

  // Network I/O thread body (ADR-0005): pumps the connection until running is
  // cleared by ThreadJoiner or Stop().
  void NetworkThreadMain() {
    while (running.load(std::memory_order_relaxed)) {
      host.PumpNetwork();
    }
  }
};

ServerRuntime::ServerRuntime(const Config& config) : impl_(std::make_unique<Impl>(config)) {}

ServerRuntime::~ServerRuntime() = default;

void ServerRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running, .network_thread = impl_->network_thread};

  const auto tick_duration = std::chrono::duration<float>(1.0F / impl_->config.tick_rate_hz);
  LI("subsystem=serverruntime event=loop_starting loop=simulation");
  while (impl_->running.load(std::memory_order_relaxed)) {
    const auto tick_start = std::chrono::steady_clock::now();

    impl_->host.Tick(tick_duration.count());

    std::this_thread::sleep_until(tick_start +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
  }
  LI("subsystem=serverruntime event=loop_stopping loop=simulation");
}

void ServerRuntime::Stop() { impl_->running.store(false, std::memory_order_relaxed); }

}  // namespace augusta::runtime
