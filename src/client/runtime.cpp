#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <nvtx3/nvtx3.hpp>

#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/protocol.h"

namespace augusta::runtime {

namespace {

// renderer doesn't depend on augusta_protocol (see renderer.h's own
// comment on kMaxRemotePlayers) - this is the one place both are visible to
// check the two haven't drifted apart.
static_assert(renderer::kMaxRemotePlayers >= protocol::kMaxPlayers,
              "the renderer must be able to draw every possible player");

// Maps one interpolated remote player into a renderer-drawable instance of
// its character's mesh, which ClientRuntime uploads via SetCharacterMesh in
// the Lobby (ADR-0042/ADR-0043). height_scale reflects stance the same way
// the procedural placeholder box this replaced did (issue #82's "in the
// right stance" acceptance criterion): the capsule's own authored height is
// the standing height, so a lower stance scales it down by the ratio of
// physics.cpp's own capsule constants (kCapsuleRadius/kStandingHeight/
// kCrouchingHeight/kProneHeight, physics.cpp lines 82-85) rather than
// reusing them - those are physics.cpp-internal by design, and this mapping
// is still placeholder-only (no skeletal animation yet).
renderer::RemotePlayer ToRenderer(const presentation::RemotePlayer& remote) {
  constexpr float kCapsuleRadius = 0.3F;
  constexpr float kStandingHeight = 1.5F;
  constexpr float kCrouchingHeight = 0.7F;
  constexpr float kProneHeight = 0.1F;
  constexpr float kStandingTotalHeight = kStandingHeight + (2.0F * kCapsuleRadius);

  float cylinder_height = kStandingHeight;
  switch (remote.body.stance) {
    case physics::Stance::kStanding:
      cylinder_height = kStandingHeight;
      break;
    case physics::Stance::kCrouching:
      cylinder_height = kCrouchingHeight;
      break;
    case physics::Stance::kProne:
      cylinder_height = kProneHeight;
      break;
  }
  const float total_height = cylinder_height + (2.0F * kCapsuleRadius);
  return {.position = remote.body.position,
          .height_scale = total_height / kStandingTotalHeight,
          .character = remote.character};
}

// Maps this frame's presentation::Camera into what Renderer::SetCamera
// takes - same decoupling reason as the RemotePlayer overload above.
renderer::Camera ToRenderer(const presentation::Camera& camera) {
  return {.position = camera.position, .rotation = camera.rotation};
}

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
  // Main/Render thread only: loads a character's mesh, which characters'
  // meshes the renderer has (for the life of the process), and the newest
  // Roster version Ready was reported for.
  CharacterMeshLoader load_character_mesh;
  std::set<std::uint8_t> loaded_characters;
  std::optional<std::uint32_t> ready_version;
  input::Input input;
  audio::Engine audio;
  // Emplaced by the constructor once the map is loaded into its PredictionWorld.
  std::optional<harness::Session> session;
  presentation::World presentation;
  renderer::Renderer renderer;

  std::atomic<bool> running{false};
  std::thread prediction_thread;
  std::thread network_thread;

  // What the Prediction thread's last tick left: the predicted state and where
  // the player looked for that tick's command, which the camera turns by.
  struct LatestTick {
    prediction::State state;
    math::Quat view_rotation{1.0F, 0.0F, 0.0F, 0.0F};
  };

  // Guards latest_tick: written once per Prediction tick,
  // read once per Main/Render frame. prediction::State is empty today
  // (see prediction.h) - a plain mutex-guarded copy is more than fast
  // enough; revisit (e.g. double-buffering) only if profiling says
  // otherwise once it holds real payload.
  std::mutex latest_tick_mutex;
  LatestTick latest_tick;

  // Connection numbers for the renderer's debug HUD: written by the Network
  // I/O thread (PublishHudNetStats), read by the Main/Render thread once per
  // frame. They should be consistent with each other, so - like
  // latest_tick above - a mutex-guarded copy.
  std::mutex hud_net_mutex;
  std::optional<renderer::DebugHudNetStats> latest_hud_net;

  // Network I/O thread only. GetConnectionStats() reports jitter as a high-water mark
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
  // is used instead of skipping the sample while GetConnectionStats() returns
  // std::nullopt (not yet kConnected), so the timeline shows an explicit
  // gap rather than a misleading flat line at whatever value came before.
  nvtx3::counter<double> net_ping_ms{"network.ping_ms", "Round-trip time to server"};
  nvtx3::counter<double> net_quality_local{"network.quality_local", "Local packet delivery quality (0-1)"};
  nvtx3::counter<double> net_quality_remote{"network.quality_remote", "Remote-reported packet delivery quality (0-1)"};
  nvtx3::counter<double> net_in_bytes_per_sec{"network.in_bytes_per_sec", "Inbound throughput"};
  nvtx3::counter<double> net_out_bytes_per_sec{"network.out_bytes_per_sec", "Outbound throughput"};
  nvtx3::counter<double> net_max_jitter_us{"network.max_jitter_us",
                                           "Worst jitter since last GetConnectionStats() call"};
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
    const std::optional<networking::ConnectionStats> stats = session->GetConnectionStats();
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

  Impl(const Config& cfg, const Map& map, CharacterMeshLoader loader)
      : config(cfg),
        load_character_mesh(std::move(loader)),
        input(cfg.input),
        presentation(audio),
        renderer(cfg.renderer, input) {
    // The map goes in before the Session takes the world over: a body that has
    // already ticked has been predicted without it, and reconciliation cannot
    // account for that.
    // No rules of its own: the Session starts the prediction under the
    // server's once it has joined, so the two cannot drift.
    prediction::World world;
    for (const physics::CollisionMesh& mesh : map.collision) {
      if (const auto added = world.AddCollisionMesh(mesh); !added) {
        throw std::runtime_error(std::format("ClientRuntime: map collision rejected: {}",
                                             physics::DescribeCollisionMeshError(added.error())));
      }
    }
    session.emplace(harness::SessionConfig{.server = cfg.server, .character = cfg.character}, std::move(world));
  }

  // The tick rate the server sent when it admitted this client, or nullopt if
  // running was cleared first. The tick rate is the server's (ADR-0039), so
  // nothing is predicted before it is known.
  std::optional<float> WaitForTickRate() {
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    while (running.load(std::memory_order_relaxed)) {
      if (const auto rate = session->GetTickRate()) {
        return rate;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    return std::nullopt;
  }

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
        LD("subsystem=clientruntime event=heartbeat ticks={} corrections={} correction_m={:.3f}", ticks_, corrections_,
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

  // Prediction thread body (ADR-0005): fixed-rate loop sampling local
  // input and ticking PredictionWorld, at the server's tick rate once it has
  // joined. Runs until running is cleared by ThreadJoiner.
  void PredictionThreadMain() {
    const auto tick_rate_hz = WaitForTickRate();
    if (!tick_rate_hz.has_value()) {
      return;
    }
    const auto tick_duration = std::chrono::duration<float>(1.0F / *tick_rate_hz);
    PredictionActivity activity;
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Prediction Tick"};
      const auto tick_start = std::chrono::steady_clock::now();

      input::Command command = input.Sample();
      prediction::State state = session->Tick(command, tick_duration.count());
      activity.Record(state, tick_start);

      {
        std::lock_guard<std::mutex> lock(latest_tick_mutex);
        latest_tick = {.state = state, .view_rotation = input::ViewRotation(command.yaw, command.pitch)};
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

  // In the Lobby, once per Roster version: uploads the mesh of every other
  // player's character not loaded yet, then reports Ready for that Roster
  // (ADR-0043). Nothing is loaded during a match. Main/Render thread only, as
  // the upload is. Returns why a mesh could not be loaded, if one could not.
  std::optional<client::SceneError> GetReadyForLobby() {
    const std::optional<harness::Lobby> lobby = session->GetLobby();
    if (session->GetPhase() != harness::Phase::kLobby || !lobby.has_value() || lobby->version == ready_version) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> others;
    for (const harness::RosterEntry& entry : lobby->roster) {
      if (entry.session != session->GetSessionId()) {
        others.push_back(entry.character);
      }
    }
    for (const std::uint8_t character : client::CharactersToLoad(others, loaded_characters)) {
      auto mesh = load_character_mesh(character);
      if (!mesh.has_value()) {
        return mesh.error();
      }
      renderer.SetCharacterMesh(character, *mesh);
      loaded_characters.insert(character);
      LI("subsystem=clientruntime event=character_loaded character={}", character);
    }
    session->ReportReady(lobby->version);
    ready_version = lobby->version;
    return std::nullopt;
  }

  LatestTick GetLatestTick() {
    std::lock_guard<std::mutex> lock(latest_tick_mutex);
    return latest_tick;
  }
};

ClientRuntime::ClientRuntime(const Config& config, Map map, const renderer::Scene& scene,
                             CharacterMeshLoader load_character_mesh)
    : impl_(std::make_unique<Impl>(config, map, std::move(load_character_mesh))) {
  impl_->renderer.SetScene(scene);
}

ClientRuntime::~ClientRuntime() = default;

std::optional<Failure> ClientRuntime::Run() {
  impl_->running.store(true, std::memory_order_relaxed);
  impl_->prediction_thread = std::thread([this] { impl_->PredictionThreadMain(); });
  impl_->network_thread = std::thread([this] { impl_->NetworkThreadMain(); });
  ThreadJoiner joiner{.running = impl_->running,
                      .prediction_thread = impl_->prediction_thread,
                      .network_thread = impl_->network_thread};

  bool cursor_locked = impl_->input.CursorCaptured();
  impl_->renderer.SetCursorLocked(cursor_locked);
  LI("subsystem=clientruntime event=loop_starting loop=render");
  std::optional<Failure> failure;
  while (!impl_->renderer.ShouldClose()) {
    if (const auto session_failure = impl_->session->GetFailure(); session_failure.has_value()) {
      LE("subsystem=clientruntime event=session_failed reason=\"{}\"", harness::DescribeFailure(*session_failure));
      failure = *session_failure;
      break;
    }
    if (const auto load_failure = impl_->GetReadyForLobby(); load_failure.has_value()) {
      LE("subsystem=clientruntime event=character_load_failed reason=\"{}\"",
         client::DescribeSceneError(*load_failure));
      failure = *load_failure;
      break;
    }
    const nvtx3::scoped_range range{"Main/Render Frame"};
    impl_->renderer.PumpEvents();
    // Escape releases the cursor and a click captures it again (Input decides).
    if (const bool captured = impl_->input.CursorCaptured(); captured != cursor_locked) {
      impl_->renderer.SetCursorLocked(captured);
      cursor_locked = captured;
    }
    impl_->renderer.SetDebugHudStats({.net = impl_->GetLatestHudNet()});
    // Two independent Session getters, not one snapshot - safe here because
    // the server always sends JoinAccepted before this client's player can
    // appear in any Authoritative State (harness::Session publishes the two
    // as separate, ordered updates - see harness.cpp's ServerView), so a
    // GetAuthoritativeState() that already has this session's player can
    // never race ahead of a GetSessionId() that is still nullopt.
    const Impl::LatestTick latest = impl_->GetLatestTick();
    presentation::State frame_state =
        impl_->presentation.RunFrame(latest.state, latest.view_rotation, impl_->session->GetSessionId(),
                                     impl_->session->GetAuthoritativeState(), impl_->session->GetMatchStart());
    impl_->renderer.SetCamera(ToRenderer(frame_state.camera));
    // The local player's own position isn't drawn yet (renderer.h) - only
    // remote players, each as its character. In the Lobby there are none, so
    // the map is drawn empty.
    std::vector<renderer::RemotePlayer> remote_boxes;
    remote_boxes.reserve(frame_state.remote_players.size());
    for (const auto& remote : frame_state.remote_players) {
      remote_boxes.push_back(ToRenderer(remote));
    }
    impl_->renderer.SetRemotePlayers(remote_boxes);
    impl_->renderer.RenderFrame();
  }
  LI("subsystem=clientruntime event=loop_stopping loop=render");
  return failure;
}

}  // namespace augusta::runtime
