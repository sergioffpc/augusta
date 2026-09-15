#include "runtime.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "augusta/input.h"
#include "augusta/logging.h"

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
  networking::Server network;
  simulation::World simulation;

  std::atomic<bool> running{false};
  std::thread network_thread;

  // Guards latest_commands: written by the Network I/O thread as client
  // commands arrive, read once per Simulation tick. Always empty today
  // - see this module's header comment on the Networking Protocol gap.
  std::mutex commands_mutex;
  std::vector<input::Command> latest_commands;

  explicit Impl(const Config& cfg) : config(cfg), network(cfg.listen), simulation(cfg.stamina, cfg.script_path) {}

  // Network I/O thread body (ADR-0005): accepts connecting peers and
  // pumps the connection until running is cleared by ThreadJoiner or
  // Stop().
  void NetworkThreadMain() {
    while (running.load(std::memory_order_relaxed)) {
      for (const networking::PeerEvent& event : network.PumpEvents()) {
        if (event.type == networking::PeerEventType::kConnectRequested) {
          // TODO(sergioffpc): run Input Validation/any join policy
          // (US-15) before accepting - not yet a module of its own, see
          // this module's header comment. Accepts unconditionally for
          // now.
          network.Accept(event.peer);
        }
      }
      // TODO(sergioffpc): decode each received PeerMessage's Payload
      // into an input::Command and store it into latest_commands - the
      // Networking Protocol (ADR-0007) isn't designed yet.
      static_cast<void>(network.ReceiveMessages());
    }
  }

  std::vector<input::Command> GetLatestCommands() {
    std::lock_guard<std::mutex> lock(commands_mutex);
    return latest_commands;
  }
};

ServerRuntime::ServerRuntime(const Config& config) : impl_(std::make_unique<Impl>(config)) {}

ServerRuntime::~ServerRuntime() = default;

void ServerRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running, .network_thread = impl_->network_thread};

  const auto tick_duration = std::chrono::duration<float>(1.0F / impl_->config.tick_rate_hz);
  INFO("ServerRuntime: Simulation loop starting");
  while (impl_->running.load(std::memory_order_relaxed)) {
    const auto tick_start = std::chrono::steady_clock::now();

    std::vector<input::Command> commands = impl_->GetLatestCommands();
    simulation::State state = impl_->simulation.Tick(commands, tick_duration.count());
    // TODO(sergioffpc): hand state to augusta::replication (scaffolded,
    // not implemented) to encode and network.Broadcast - see this
    // module's header comment.
    static_cast<void>(state);

    std::this_thread::sleep_until(tick_start +
                                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
  }
  INFO("ServerRuntime: Simulation loop stopping");
}

void ServerRuntime::Stop() { impl_->running.store(false, std::memory_order_relaxed); }

}  // namespace augusta::runtime
