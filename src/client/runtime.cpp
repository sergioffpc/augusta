#include "runtime.h"

#include <nvtx3/nvtx3.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

#include "augusta/logging.h"

namespace augusta::runtime {

namespace {

// Stops Impl's background threads and joins both, on scope exit -
// including when unwinding past Run() due to an exception from the
// Main/Render loop body. This is the only place thread cleanup happens;
// ~ClientRuntime relies on Run() having already run it (see that
// destructor's own doc comment in runtime.h).
struct ThreadJoiner {
  std::atomic<bool>& running;
  std::thread& simulation_thread;
  std::thread& network_thread;

  ~ThreadJoiner() {
    running.store(false, std::memory_order_relaxed);
    if (simulation_thread.joinable()) {
      simulation_thread.join();
    }
    if (network_thread.joinable()) {
      network_thread.join();
    }
  }
};

}  // namespace

struct ClientRuntime::Impl {
  Config config;
  input::Input input;
  audio::Engine audio;
  networking::Client network;
  prediction::World prediction;
  presentation::World presentation;
  renderer::Renderer renderer;

  std::atomic<bool> running{false};
  std::thread simulation_thread;
  std::thread network_thread;

  // Guards latest_prediction_state: written once per Simulation tick,
  // read once per Main/Render frame. prediction::State is empty today
  // (see prediction.h) - a plain mutex-guarded copy is more than fast
  // enough; revisit (e.g. double-buffering) only if profiling says
  // otherwise once it holds real payload.
  std::mutex prediction_state_mutex;
  prediction::State latest_prediction_state;

  explicit Impl(const Config& cfg)
      : config(cfg), input(cfg.input), prediction(cfg.stamina), presentation(audio), renderer(cfg.renderer, input) {}

  // Simulation thread body (ADR-0005): fixed-rate loop sampling local
  // input and ticking PredictionWorld. Runs until running is cleared by
  // ThreadJoiner.
  void SimulationThreadMain() {
    const auto tick_duration = std::chrono::duration<float>(1.0F / config.tick_rate_hz);
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Simulation Tick"};
      const auto tick_start = std::chrono::steady_clock::now();

      input::Command command = input.Sample();
      // TODO(sergioffpc): no authoritative state to reconcile against
      // yet - deserializing one from network.ReceiveMessages() needs
      // the Networking Protocol (ADR-0007), not designed yet. See this
      // module's header comment.
      prediction::State state = prediction.Tick(command, std::nullopt, tick_duration.count());

      {
        std::lock_guard<std::mutex> lock(prediction_state_mutex);
        latest_prediction_state = state;
      }

      // TODO(sergioffpc): serialize command and network.Send(...) it -
      // same Networking Protocol gap as above.

      std::this_thread::sleep_until(tick_start +
                                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
    }
  }

  // Network I/O thread body (ADR-0005): connects once, then pumps the
  // connection until running is cleared by ThreadJoiner.
  void NetworkThreadMain() {
    network.Connect(config.server);
    bool sent_hello = false;
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Network PumpEvents"};
      network.PumpEvents();

      // TODO(sergioffpc): M1 spike only (issue #31) - a literal hello
      // proving the transport round-trips a message at all. Replace
      // with real Command encoding once the Networking Protocol
      // (ADR-0007) exists; see this module's header comment.
      if (!sent_hello && network.GetState() == networking::ConnectionState::kConnected) {
        constexpr std::string_view kHello = "hello from augustac";
        const auto* bytes = reinterpret_cast<const std::byte*>(kHello.data());
        network.Send(networking::Payload(bytes, bytes + kHello.size()));
        sent_hello = true;
      }
      for ([[maybe_unused]] const networking::Payload& payload : network.ReceiveMessages()) {
        LT("subsystem=clientruntime event=received bytes={}", payload.size());
      }
    }
    network.Disconnect();
  }

  prediction::State GetLatestPredictionState() {
    std::lock_guard<std::mutex> lock(prediction_state_mutex);
    return latest_prediction_state;
  }
};

ClientRuntime::ClientRuntime(const Config& config) : impl_(std::make_unique<Impl>(config)) {}

ClientRuntime::~ClientRuntime() = default;

void ClientRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->simulation_thread = std::thread([this] { impl_->SimulationThreadMain(); });
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running,
                      .simulation_thread = impl_->simulation_thread,
                      .network_thread = impl_->network_thread};

  LI("subsystem=clientruntime event=loop_starting loop=render");
  while (!impl_->renderer.ShouldClose()) {
    const nvtx3::scoped_range range{"Main/Render Frame"};
    impl_->renderer.PumpEvents();
    presentation::State frame_state = impl_->presentation.RunFrame(impl_->GetLatestPredictionState());
    // TODO(sergioffpc): renderer.RenderFrame() doesn't consume
    // Presentation State yet - see renderer.h's own note on this.
    static_cast<void>(frame_state);
    impl_->renderer.RenderFrame();
  }
  LI("subsystem=clientruntime event=loop_stopping loop=render");
}

}  // namespace augusta::runtime
