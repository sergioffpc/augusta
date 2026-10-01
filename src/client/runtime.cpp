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

#include "augusta/audio.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/effects.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/parameters.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/tick.h"

namespace augusta::runtime {

namespace {

// Maps one interpolated remote player into a renderer-drawable instance of
// its character's mesh, which ClientRuntime uploads via SetCharacterMesh in
// the Lobby (ADR-0042/ADR-0043), turned where it faces. The mesh is drawn as
// authored, standing: its stance shows once animation poses it.
renderer::RemotePlayer ToRenderer(const presentation::RemotePlayer& remote) {
  return {.position = remote.body.position, .yaw = remote.body.yaw, .character = remote.character};
}

// Maps this frame's presentation::Camera into what Renderer::SetCamera
// takes - same decoupling reason as the RemotePlayer overload above.
renderer::Camera ToRenderer(const presentation::Camera& camera) {
  return {.position = camera.position, .rotation = camera.rotation, .vertical_fov = camera.vertical_fov};
}

// Maps effects that show for lifetime seconds into glows, faded by their age.
std::vector<renderer::Glow> ToRenderer(const std::vector<presentation::Effect>& effects, float lifetime) {
  std::vector<renderer::Glow> glows;
  glows.reserve(effects.size());
  for (const presentation::Effect& effect : effects) {
    glows.push_back({.position = effect.position, .fade = 1.0F - (effect.age / lifetime)});
  }
  return glows;
}

// This frame's tracers, impacts and muzzle flashes, as Renderer::SetCombatEffects takes them.
renderer::CombatEffects CombatEffectsOf(const presentation::State& state) {
  renderer::CombatEffects effects{
      .tracers = {},
      .impacts = ToRenderer(state.impacts, presentation::kImpactSeconds),
      .muzzle_flashes = ToRenderer(state.muzzle_flashes, presentation::kMuzzleFlashSeconds),
  };
  effects.tracers.reserve(state.tracers.size());
  for (const presentation::Tracer& tracer : state.tracers) {
    effects.tracers.push_back({.head = tracer.head, .tail = tracer.tail});
  }
  return effects;
}

// Maps the harness's Entity ID into presentation's own - the same number,
// converted here at ClientRuntime's edge the way each peer converts the
// protocol at its own (ADR-0038), so presentation does not depend on the
// harness.
presentation::EntityId ToPresentation(harness::EntityId entity) {
  return static_cast<presentation::EntityId>(std::to_underlying(entity));
}

// An Authoritative State update as presentation's WorldSnapshot: the same tick,
// the duration of a tick at the server's tick_rate_hz, and every body.
presentation::WorldSnapshot ToPresentation(const harness::AuthoritativeState& state, std::uint8_t tick_rate_hz) {
  presentation::WorldSnapshot snapshot{
      .tick = state.tick,
      .tick_duration = 1.0 / static_cast<double>(tick_rate_hz),
      .bodies = {},
  };
  snapshot.bodies.reserve(state.bodies.size());
  for (const harness::EntityBody& body : state.bodies) {
    snapshot.bodies.push_back({.entity = ToPresentation(body.entity), .state = body.body, .yaw = body.yaw});
  }
  return snapshot;
}

// session's newest Authoritative State update as presentation's
// WorldSnapshot, or nullopt outside a match. An Authoritative State only
// follows Join accepted, which told the tick rate.
std::optional<presentation::WorldSnapshot> SnapshotOf(const harness::Session& session) {
  const std::optional<std::uint8_t> tick_rate_hz = session.GetTickRate();
  return session.GetAuthoritativeState().and_then([tick_rate_hz](const harness::AuthoritativeState& state) {
    return tick_rate_hz.transform([&state](std::uint8_t rate) { return ToPresentation(state, rate); });
  });
}

// Maps a Shot as the harness received it into presentation's own, which draws
// it: who fired it and where from and for; its tick is the server's business.
presentation::Shot ToPresentation(const harness::Shot& shot) {
  return {.shooter = ToPresentation(shot.shooter), .origin = shot.origin, .yaw = shot.yaw, .pitch = shot.pitch};
}

presentation::Aim ToPresentation(const input::Aim& aim) { return {.yaw = aim.yaw, .pitch = aim.pitch, .ads = aim.ads}; }

// Every player's character as Match start named it, in Session order, for
// PresentationWorld::RunFrame; empty before the first match. Only the
// characters: presentation needs nothing else of Match start.
std::vector<presentation::PlayerCharacter> CharactersOf(const std::optional<harness::MatchStart>& match_start) {
  std::vector<presentation::PlayerCharacter> characters;
  if (match_start.has_value()) {
    std::vector<harness::MatchPlayer> players = match_start->players;
    std::ranges::sort(players, {}, &harness::MatchPlayer::session);
    characters.reserve(players.size());
    for (const harness::MatchPlayer& player : players) {
      characters.push_back({.entity = ToPresentation(player.entity), .character = player.character});
    }
  }
  return characters;
}

// command as sampled against view, what the last render frame showed the other
// players at: the view it reports to the server, which judges its shots against
// the players as they were then (ADR-0044). With no view, it reports none.
command::Command WithView(command::Command command, const std::optional<presentation::ShownView>& view) {
  if (view.has_value()) {
    command.view_tick = view->tick;
    command.view_fraction = view->fraction;
  }
  return command;
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
  // Main/Render thread only: loads a character, which characters the renderer
  // and PresentationWorld have (for the life of the process), the newest
  // Roster version Ready was reported for, and whether PresentationWorld has
  // the server's parameters.
  CharacterLoader load_character;
  std::set<std::uint8_t> loaded_characters;
  std::optional<std::uint32_t> ready_version;
  bool presentation_has_parameters = false;
  input::Input input;
  audio::Engine audio;
  // Emplaced by the constructor once the map is loaded into its PredictionWorld.
  std::optional<harness::Session> session;
  presentation::World presentation;
  renderer::Renderer renderer;

  std::atomic<bool> running{false};
  std::thread prediction_thread;
  std::thread network_thread;

  // What the Prediction thread's last tick left: the predicted states before
  // and after it, and when it was due and for how long, so a render frame
  // blends the two by how far through the tick it is.
  struct LatestTick {
    prediction::State previous;
    prediction::State latest;
    tick::Clock::time_point start;
    tick::Clock::duration duration{};
  };

  // Guards latest_tick: written once per Prediction tick,
  // read once per Main/Render frame. prediction::State is empty today
  // (see prediction.h) - a plain mutex-guarded copy is more than fast
  // enough; revisit (e.g. double-buffering) only if profiling says
  // otherwise once it holds real payload.
  std::mutex latest_tick_mutex;
  LatestTick latest_tick;

  // What the last render frame showed the other players at, or nullopt while
  // it showed none: written once per Main/Render frame, read once per
  // Prediction tick, which reports it with the tick's Command (ADR-0044).
  std::mutex shown_view_mutex;
  std::optional<presentation::ShownView> shown_view;

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

  Impl(const Config& cfg, const Map& map, const math::Vec3& eye, const audio::CueSounds& cue_sounds,
       CharacterLoader loader)
      : config(cfg),
        load_character(std::move(loader)),
        input(cfg.input),
        presentation(audio, cue_sounds, eye),
        renderer(cfg.renderer, input) {
    // The map goes in before the Session takes the world over: a body that has
    // already ticked has been predicted without it, and reconciliation cannot
    // account for that.
    // No rules of its own: the Session starts the prediction under the
    // server's once it has joined, so the two cannot drift.
    // The Map goes into PresentationWorld too, for the tracers to meet.
    prediction::World world;
    for (const physics::CollisionMesh& mesh : map.collision) {
      auto added = world.AddCollisionMesh(mesh).and_then([&] { return presentation.AddCollisionMesh(mesh); });
      if (!added) {
        throw std::runtime_error(std::format("ClientRuntime: map collision rejected: {}",
                                             physics::DescribeCollisionMeshError(added.error())));
      }
    }
    session.emplace(
        harness::SessionConfig{.server = cfg.server, .client_pack = cfg.client_pack, .character = cfg.character},
        std::move(world));
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

  // How long the next Prediction tick lasts: the server's tick, paced by how
  // many of this client's commands the server last said it held (tick.h), so
  // the client sends them at the rate the server consumes them.
  tick::Clock::duration NextTickDuration(tick::Clock::duration nominal) const {
    const std::optional<harness::AuthoritativeState> state = session->GetAuthoritativeState();
    return state.has_value() ? tick::PacedTickDuration(nominal, state->queued_commands) : nominal;
  }

  // Prediction thread body (ADR-0005): loop sampling local input and ticking
  // PredictionWorld on a fixed schedule (tick.h), at the server's tick rate
  // once it has joined, each tick paced to keep the server's queue of this
  // client's commands short. Runs until running is cleared by ThreadJoiner.
  void PredictionThreadMain() {
    const auto tick_rate_hz = WaitForTickRate();
    if (!tick_rate_hz.has_value()) {
      return;
    }
    const auto delta_time = std::chrono::duration<float>(1.0F / *tick_rate_hz);
    const auto nominal_tick = std::chrono::duration_cast<tick::Clock::duration>(delta_time);
    PredictionActivity activity;
    tick::Clock::time_point deadline = tick::Clock::now();
    // The first tick has none before it to blend from.
    std::optional<prediction::State> previous;
    while (running.load(std::memory_order_relaxed)) {
      const nvtx3::scoped_range range{"Prediction Tick"};
      const tick::Clock::time_point tick_start = tick::Clock::now();

      const command::Command command = WithView(input.Sample(), GetShownView());
      const prediction::State state = session->Tick(command, delta_time.count());
      activity.Record(state, tick_start);

      // The tick spans its schedule, not its wake-ups, so frames blend evenly.
      const tick::Clock::time_point due = deadline;
      deadline = tick::NextDeadline(deadline, NextTickDuration(nominal_tick), tick::Clock::now());
      {
        std::lock_guard<std::mutex> lock(latest_tick_mutex);
        latest_tick = {.previous = previous.value_or(state), .latest = state, .start = due, .duration = deadline - due};
      }
      previous = state;

      std::this_thread::sleep_until(deadline);
    }
  }

  // Network I/O thread body (ADR-0005): connects once, then pumps the
  // connection until running is cleared by ThreadJoiner, waiting
  // kNetworkRoundWait between rounds rather than spinning a core. The
  // transport has no wait on incoming work, so that wait bounds how late a
  // received message is handled, and how long stopping takes.
  void NetworkThreadMain() {
    constexpr auto kNetworkRoundWait = std::chrono::milliseconds(1);
    session->Connect();
    while (running.load(std::memory_order_relaxed)) {
      {
        const nvtx3::scoped_range range{"Network PumpEvents"};
        session->PumpEvents();
        SampleNetworkStats();
        session->ExchangeMessages();
      }
      std::this_thread::sleep_for(kNetworkRoundWait);
    }
    session->Disconnect();
  }

  // In the Lobby, once per Roster version: uploads the mesh of every other
  // player's character not loaded yet and hands PresentationWorld its eye, then
  // reports Ready for that Roster (ADR-0043). Nothing is loaded during a match.
  // Main/Render thread only, as the upload is. Returns why a character could
  // not be loaded, if one could not.
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
      auto loaded = load_character(character);
      if (!loaded.has_value()) {
        return loaded.error();
      }
      renderer.SetCharacterMesh(character, loaded->mesh);
      presentation.SetCharacterEye(character, loaded->eye);
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

  // Once the server has admitted this client, hands PresentationWorld the
  // parameters it sent and its tick, once: what tracers fly by and ADS zooms to.
  void SharePresentationParameters() {
    if (presentation_has_parameters) {
      return;
    }
    const std::optional<parameters::Parameters> parameters = session->GetParameters();
    const std::optional<std::uint8_t> tick_rate_hz = session->GetTickRate();
    if (parameters.has_value() && tick_rate_hz.has_value()) {
      presentation.SetParameters(*parameters, 1.0F / static_cast<float>(*tick_rate_hz));
      presentation_has_parameters = true;
    }
  }

  // What this render frame is shown from: the latest Prediction ticks blended
  // by how far through the tick it is, where the player aims now, and what the
  // session has received, converted into presentation's own types here, at
  // ClientRuntime's edge (see SnapshotOf and CharactersOf above). Takes the
  // Shots, Hit confirmations and Deaths received since the last frame.
  //
  // Two independent Session getters, not one view - safe here because
  // harness::Session keeps an Authoritative State only once the Match start
  // that names this client's body has been published (see harness.cpp's
  // ServerView), so a GetAuthoritativeState() read before GetEntityId(), as
  // below, can never race ahead of a GetEntityId() that is still nullopt.
  presentation::FrameInput NextFrameInput() {
    const LatestTick latest = GetLatestTick();
    presentation::FrameInput frame{
        .ticks = {.previous = latest.previous,
                  .latest = latest.latest,
                  .fraction = tick::FractionElapsed(latest.start, latest.duration, tick::Clock::now())},
        .aim = ToPresentation(input.CurrentAim()),
        .fire = input.IsHeld(input::Control::kFire),
        .local_entity = std::nullopt,
        .snapshot = SnapshotOf(*session),
        .characters = CharactersOf(session->GetMatchStart()),
        .shots = {},
        .hit_confirmations = static_cast<std::uint32_t>(session->TakeHitConfirmations().size()),
        .deaths = {},
    };
    frame.local_entity =
        session->GetEntityId().transform([](harness::EntityId entity) { return ToPresentation(entity); });
    for (const harness::Shot& shot : session->TakeShots()) {
      frame.shots.push_back(ToPresentation(shot));
    }
    for (const harness::Death& death : session->TakeDeaths()) {
      frame.deaths.push_back(ToPresentation(death.victim));
    }
    return frame;
  }

  // Hands the renderer what this frame shows. The local player's own position
  // isn't drawn yet (renderer.h) - only remote players, each as its character.
  // In the Lobby there are none, so the map is drawn empty.
  void Show(const presentation::State& frame_state) {
    renderer.SetCamera(ToRenderer(frame_state.camera));
    std::vector<renderer::RemotePlayer> remote_boxes;
    remote_boxes.reserve(frame_state.remote_players.size());
    for (const auto& remote : frame_state.remote_players) {
      remote_boxes.push_back(ToRenderer(remote));
    }
    renderer.SetRemotePlayers(remote_boxes);
    // After SetCamera: the effects are turned to face it.
    renderer.SetCombatEffects(CombatEffectsOf(frame_state));
    renderer.SetOverlay({.crosshair = frame_state.crosshair, .hit_marker = frame_state.hit_marker});
  }

  void SetShownView(const std::optional<presentation::ShownView>& view) {
    const std::lock_guard<std::mutex> lock(shown_view_mutex);
    shown_view = view;
  }

  std::optional<presentation::ShownView> GetShownView() {
    const std::lock_guard<std::mutex> lock(shown_view_mutex);
    return shown_view;
  }
};

ClientRuntime::ClientRuntime(const Config& config, Map map, const renderer::Scene& scene, const math::Vec3& eye,
                             const audio::CueSounds& cue_sounds, CharacterLoader load_character)
    : impl_(std::make_unique<Impl>(config, map, eye, cue_sounds, std::move(load_character))) {
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
    impl_->SharePresentationParameters();
    const presentation::State frame_state = impl_->presentation.RunFrame(impl_->NextFrameInput());
    impl_->SetShownView(frame_state.view);
    impl_->Show(frame_state);
    impl_->renderer.RenderFrame();
  }
  LI("subsystem=clientruntime event=loop_stopping loop=render");
  return failure;
}

}  // namespace augusta::runtime
