#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <nvtx3/nvtx3.hpp>

#include "augusta/harness.h"
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
  std::thread& prediction_thread;
  std::thread& network_thread;

  ~ThreadJoiner() {
    running.store(false, std::memory_order_relaxed);
    if (prediction_thread.joinable()) {
      prediction_thread.join();
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
  // Emplaced by the constructor once the map is loaded into its PredictionWorld.
  std::optional<harness::Session> session;
  presentation::World presentation;
  renderer::Renderer renderer;

  std::atomic<bool> running{false};
  std::thread prediction_thread;
  std::thread network_thread;

  // Guards latest_prediction_state: written once per Prediction tick,
  // read once per Main/Render frame. prediction::State is empty today
  // (see prediction.h) - a plain mutex-guarded copy is more than fast
  // enough; revisit (e.g. double-buffering) only if profiling says
  // otherwise once it holds real payload.
  std::mutex prediction_state_mutex;
  prediction::State latest_prediction_state;

  // Connection numbers for the renderer's debug HUD: written by the Network
  // I/O thread (PublishHudNetStats), read by the Main/Render thread once per
  // frame. They should be consistent with each other, so - like
  // latest_prediction_state above - a mutex-guarded copy.
  std::mutex hud_net_mutex;
  std::optional<renderer::DebugHudNetStats> latest_hud_net;

  // Network I/O thread only. GetStats() reports jitter as a high-water mark
  // cleared by every read, and that thread reads it far faster than anyone
  // can read a HUD, so the HUD shows the peak over the last kJitterWindow.
  static constexpr std::chrono::seconds kJitterWindow{1};
  std::chrono::steady_clock::time_point jitter_window_start = std::chrono::steady_clock::now();
  std::int32_t jitter_window_max_us = -1;
  std::optional<float> hud_jitter_ms;

  // NVTX counters (nvtx3::counter, third_party/nvtx) mirroring
  // networking::ConnectionStats field-for-field - plotted on the Nsight
  // Systems timeline alongside the Simulation/Network/Render ranges below,
  // sampled once per NetworkThreadMain loop iteration. sample_no_value()
  // is used instead of skipping the sample while GetStats() returns
  // std::nullopt (not yet kConnected), so the timeline shows an explicit
  // gap rather than a misleading flat line at whatever value came before.
  nvtx3::counter<double> net_ping_ms{"network.ping_ms", "Round-trip time to server"};
  nvtx3::counter<double> net_quality_local{"network.quality_local", "Local packet delivery quality (0-1)"};
  nvtx3::counter<double> net_quality_remote{"network.quality_remote", "Remote-reported packet delivery quality (0-1)"};
  nvtx3::counter<double> net_in_bytes_per_sec{"network.in_bytes_per_sec", "Inbound throughput"};
  nvtx3::counter<double> net_out_bytes_per_sec{"network.out_bytes_per_sec", "Outbound throughput"};
  nvtx3::counter<double> net_max_jitter_us{"network.max_jitter_us", "Worst jitter since last GetStats() call"};
  nvtx3::counter<double> net_pending_bytes{"network.pending_bytes", "Bytes queued or in flight"};

  // Packet loss in percent, from the worse of the two directions. Qualities
  // are 0..1 (1 = no loss); negative means not measured yet.
  static std::optional<float> PacketLossPercent(const networking::ConnectionStats& stats) {
    std::optional<float> worst_quality;
    for (const float quality : {stats.quality_local, stats.quality_remote}) {
      if (quality >= 0.0F) {
        worst_quality = worst_quality.has_value() ? std::min(*worst_quality, quality) : quality;
      }
    }
    if (!worst_quality.has_value()) {
      return std::nullopt;
    }
    return (1.0F - std::min(*worst_quality, 1.0F)) * 100.0F;
  }

  void PublishHudNetStats(const std::optional<networking::ConnectionStats>& stats) {
    std::optional<renderer::DebugHudNetStats> net;
    if (stats.has_value()) {
      const auto now = std::chrono::steady_clock::now();
      jitter_window_max_us = std::max(jitter_window_max_us, stats->max_jitter_us);
      if (now - jitter_window_start >= kJitterWindow) {
        constexpr float kMicrosecondsPerMillisecond = 1000.0F;
        hud_jitter_ms =
            jitter_window_max_us >= 0
                ? std::optional<float>(static_cast<float>(jitter_window_max_us) / kMicrosecondsPerMillisecond)
                : std::nullopt;
        jitter_window_max_us = -1;
        jitter_window_start = now;
      }
      net = renderer::DebugHudNetStats{.rtt_ms = stats->ping_ms,
                                       .jitter_ms = hud_jitter_ms,
                                       .loss_percent = PacketLossPercent(*stats),
                                       .in_bytes_per_sec = stats->in_bytes_per_sec,
                                       .out_bytes_per_sec = stats->out_bytes_per_sec};
    } else {
      jitter_window_max_us = -1;
      hud_jitter_ms.reset();
    }
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    latest_hud_net = net;
  }

  std::optional<renderer::DebugHudNetStats> GetLatestHudNet() {
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    return latest_hud_net;
  }

  void SampleNetworkStats() {
    const std::optional<networking::ConnectionStats> stats = session->GetStats();
    PublishHudNetStats(stats);
    if (!stats.has_value()) {
      net_ping_ms.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_quality_local.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_quality_remote.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_in_bytes_per_sec.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_out_bytes_per_sec.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_max_jitter_us.sample_no_value(nvtx3::no_value_reason::unavailable);
      net_pending_bytes.sample_no_value(nvtx3::no_value_reason::unavailable);
      return;
    }
    net_ping_ms.sample(static_cast<double>(stats->ping_ms));
    net_quality_local.sample(static_cast<double>(stats->quality_local));
    net_quality_remote.sample(static_cast<double>(stats->quality_remote));
    net_in_bytes_per_sec.sample(static_cast<double>(stats->in_bytes_per_sec));
    net_out_bytes_per_sec.sample(static_cast<double>(stats->out_bytes_per_sec));
    net_max_jitter_us.sample(static_cast<double>(stats->max_jitter_us));
    net_pending_bytes.sample(static_cast<double>(stats->pending_bytes));
  }

  explicit Impl(const Config& cfg) : config(cfg), input(cfg.input), presentation(audio), renderer(cfg.renderer, input) {
    // The map goes in before the Session takes the world over: a body that has
    // already ticked has been predicted without it, and reconciliation cannot
    // account for that.
    // No rules of its own: the Session starts the prediction under the
    // server's once it has joined, so the two cannot drift.
    prediction::World world;
    for (const physics::CollisionMesh& mesh : cfg.collision) {
      if (const auto added = world.AddCollisionMesh(mesh); !added) {
        throw std::runtime_error(std::format("ClientRuntime: map collision rejected: {}",
                                             physics::DescribeCollisionMeshError(added.error())));
      }
    }
    session.emplace(harness::SessionConfig{.server = cfg.server}, std::move(world));
  }

  // The parameters the server sent when it admitted this client, or nullopt if
  // running was cleared first. The tick rate is the server's (ADR-0039), so
  // nothing is predicted before it is known.
  std::optional<parameters::Parameters> WaitForParameters() {
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    while (running.load(std::memory_order_relaxed)) {
      if (auto parameters = session->GetParameters()) {
        return parameters;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return std::nullopt;
  }

  // Prediction thread body (ADR-0005): fixed-rate loop sampling local
  // input and ticking PredictionWorld, at the server's tick rate once it has
  // joined. Runs until running is cleared by ThreadJoiner.
  void PredictionThreadMain() {
    const auto parameters = WaitForParameters();
    if (!parameters.has_value()) {
      return;
    }
    const auto tick_duration = std::chrono::duration<float>(1.0F / parameters->tick_rate_hz);
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Prediction Tick"};
      const auto tick_start = std::chrono::steady_clock::now();

      input::Command command = input.Sample();
      prediction::State state = session->Tick(command, tick_duration.count());

      {
        std::lock_guard<std::mutex> lock(prediction_state_mutex);
        latest_prediction_state = state;
      }

      std::this_thread::sleep_until(tick_start +
                                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_duration));
    }
  }

  // Network I/O thread body (ADR-0005): connects once, then pumps the
  // connection until running is cleared by ThreadJoiner.
  void NetworkThreadMain() {
    session->Connect();
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Network PumpEvents"};
      session->PumpEvents();
      SampleNetworkStats();
      session->ExchangeMessages();
    }
    session->Disconnect();
  }

  prediction::State GetLatestPredictionState() {
    std::lock_guard<std::mutex> lock(prediction_state_mutex);
    return latest_prediction_state;
  }
};

ClientRuntime::ClientRuntime(const Config& config, const renderer::Scene& scene)
    : impl_(std::make_unique<Impl>(config)) {
  impl_->renderer.SetScene(scene);
}

ClientRuntime::~ClientRuntime() = default;

std::optional<harness::Failure> ClientRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->prediction_thread = std::thread([this] { impl_->PredictionThreadMain(); });
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running,
                      .prediction_thread = impl_->prediction_thread,
                      .network_thread = impl_->network_thread};

  LI("subsystem=clientruntime event=loop_starting loop=render");
  std::optional<harness::Failure> failure;
  while (!impl_->renderer.ShouldClose()) {
    failure = impl_->session->GetFailure();
    if (failure.has_value()) {
      LE("subsystem=clientruntime event=session_failed reason=\"{}\"", harness::DescribeFailure(*failure));
      break;
    }
    const nvtx3::scoped_range range{"Main/Render Frame"};
    impl_->renderer.PumpEvents();
    impl_->renderer.SetDebugHudStats({.net = impl_->GetLatestHudNet()});
    presentation::State frame_state = impl_->presentation.RunFrame(impl_->GetLatestPredictionState());
    // TODO(sergioffpc): renderer.RenderFrame() doesn't consume
    // Presentation State yet - see renderer.h's own note on this.
    static_cast<void>(frame_state);
    impl_->renderer.RenderFrame();
  }
  LI("subsystem=clientruntime event=loop_stopping loop=render");
  return failure;
}

}  // namespace augusta::runtime
