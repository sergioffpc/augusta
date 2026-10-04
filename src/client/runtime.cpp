#include "runtime.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <nvtx3/nvtx3.hpp>

#include "augusta/audio.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/interpolation.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"
#include "augusta/supervisor.h"
#include "augusta/tick.h"
#include "character_loader.h"
#include "content.h"
#include "frame_mapping.h"
#include "lobby_readiness.h"
#include "net_stats.h"

namespace augusta::client {

namespace {

// Stops the background threads and joins both, on scope exit - including
// when unwinding past Run() due to an exception from the Main/Render loop
// body. This is the only place thread cleanup happens; ~ClientRuntime relies
// on Run() having already run it (see that destructor's own doc comment in
// runtime.h).
struct WorkerJoiner {
  supervisor::Supervisor& workers;

  ~WorkerJoiner() { workers.StopAndJoin(); }
};

}  // namespace

std::string DescribeRunFailure(const RunFailure& failure) {
  if (const auto* session = std::get_if<harness::Failure>(&failure)) {
    return harness::DescribeFailure(*session);
  }
  if (const auto* worker = std::get_if<supervisor::WorkerFailure>(&failure)) {
    return supervisor::DescribeWorkerFailure(*worker);
  }
  return DescribeCharacterError(std::get<CharacterError>(failure));
}

struct ClientRuntime::Impl {
  RuntimeConfig config;
  // Main/Render thread only: loads a character, what to load in the Lobby
  // before reporting Ready, and whether PresentationWorld has the server's
  // parameters.
  CharacterLoader load_character;
  LobbyReadiness lobby_readiness;
  bool presentation_has_parameters = false;
  input::Input input;
  audio::Engine audio;
  // Emplaced by the constructor once the map is loaded into its PredictionWorld.
  std::optional<harness::Session> session;
  presentation::World presentation;
  renderer::Renderer renderer;

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
  // I/O thread (SampleNetworkStats), read by the Main/Render thread once per
  // frame. They should be consistent with each other, so - like
  // latest_tick above - a mutex-guarded copy.
  std::mutex hud_net_mutex;
  std::optional<renderer::DebugHudNetStats> latest_hud_net;

  // Network I/O thread only: what the HUD shows of the connection, and the
  // counters Nsight Systems plots of it, sampled once per NetworkThreadMain
  // loop iteration.
  HudNetStats hud_net_stats;
  NetStatsCounters net_stats_counters;

  std::optional<renderer::DebugHudNetStats> GetLatestHudNet() {
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    return latest_hud_net;
  }

  void SampleNetworkStats() {
    const std::optional<networking::ConnectionStats> stats = session->GetConnectionStats();
    net_stats_counters.Sample(stats);
    const std::optional<renderer::DebugHudNetStats> net = hud_net_stats.Update(stats, std::chrono::steady_clock::now());
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    latest_hud_net = net;
  }

  Impl(const RuntimeConfig& cfg, const Map& map, const math::Vec3& eye, const audio::CueSounds& cue_sounds,
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
  // a stop was requested first. The tick rate is the server's (ADR-0039), so
  // nothing is predicted before it is known.
  std::optional<float> WaitForTickRate() {
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    while (!workers.StopRequested()) {
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
  // client's commands short. Runs until a stop is requested (by WorkerJoiner,
  // or by a failing worker).
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
    while (!workers.StopRequested()) {
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
  // connection until a stop is requested (as for the Prediction thread), waiting
  // kNetworkRoundWait between rounds rather than spinning a core. The
  // transport has no wait on incoming work, so that wait bounds how late a
  // received message is handled, and how long stopping takes.
  void NetworkThreadMain() {
    constexpr auto kNetworkRoundWait = std::chrono::milliseconds(1);
    session->Connect();
    while (!workers.StopRequested()) {
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
  std::optional<CharacterError> GetReadyForLobby() {
    const std::optional<ReadyPlan> plan = lobby_readiness.Plan(*session->GetServerView());
    if (!plan.has_value()) {
      return std::nullopt;
    }
    for (const std::string& character : plan->characters_to_load) {
      auto loaded = load_character(character);
      if (!loaded.has_value()) {
        return loaded.error();
      }
      renderer.SetCharacterMesh(character, loaded->mesh);
      presentation.SetCharacterEye(character, loaded->eye);
      LI("subsystem=clientruntime event=character_loaded character={}", character);
      lobby_readiness.MarkLoaded(character);
    }
    session->ReportReady(plan->version);
    lobby_readiness.MarkReady(plan->version);
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
    if (const std::optional<harness::Admission>& accepted = session->GetServerView()->accepted; accepted.has_value()) {
      presentation.SetParameters(accepted->parameters, 1.0F / static_cast<float>(accepted->tick_rate_hz));
      presentation_has_parameters = true;
    }
  }

  // What this render frame is shown from: the latest Prediction ticks blended
  // by how far through the tick it is, where the player aims now, and what the
  // session has received, converted into presentation's own types here, at
  // ClientRuntime's edge (see frame_mapping.h). Takes the
  // Shots, Hit confirmations and Deaths received since the last frame.
  //
  // Everything the server has said is read from one Server view, so the
  // state, the bodies it names and the match it belongs to are of one moment
  // (ADR-0005), however the Network I/O thread interleaves with this one.
  presentation::FrameInput NextFrameInput() {
    const LatestTick latest = GetLatestTick();
    const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
    presentation::FrameInput frame{
        .ticks = {.previous = latest.previous,
                  .latest = latest.latest,
                  .fraction = tick::FractionElapsed(latest.start, latest.duration, tick::Clock::now())},
        .aim = ToPresentation(input.CurrentAim()),
        .fire = input.IsHeld(input::Control::kFire),
        .local_entity = std::nullopt,
        .snapshot = SnapshotOf(*view),
        .characters = CharactersOf(view->match_start),
        .shots = {},
        .hit_confirmations = static_cast<std::uint32_t>(session->TakeHitConfirmations().size()),
        .deaths = {},
        .health = view->authoritative.transform([](const harness::AuthoritativeState& state) { return state.health; }),
        .match_end = MatchEndOf(*view),
    };
    frame.local_entity = view->OwnEntity().transform([](harness::EntityId entity) { return ToPresentation(entity); });
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

  // The Prediction and Network I/O threads' stop request and first failure
  // (ADR-0005). Last, so its threads are joined before anything they use goes.
  supervisor::Supervisor workers;

  void SetShownView(const std::optional<presentation::ShownView>& view) {
    const std::lock_guard<std::mutex> lock(shown_view_mutex);
    shown_view = view;
  }

  std::optional<presentation::ShownView> GetShownView() {
    const std::lock_guard<std::mutex> lock(shown_view_mutex);
    return shown_view;
  }
};

ClientRuntime::ClientRuntime(const RuntimeConfig& config, Content content)
    : impl_(std::make_unique<Impl>(config, content.map, content.eye, content.cue_sounds,
                                   std::move(content.load_character))) {
  impl_->renderer.SetScene(content.scene);
}

ClientRuntime::~ClientRuntime() = default;

std::optional<RunFailure> ClientRuntime::Run() {
  Impl& impl = *impl_;
  impl.workers.Spawn("prediction", [&impl] { impl.PredictionThreadMain(); });
  impl.workers.Spawn("network", [&impl] { impl.NetworkThreadMain(); });
  const WorkerJoiner joiner{.workers = impl.workers};

  bool cursor_locked = impl_->input.CursorCaptured();
  impl_->renderer.SetCursorLocked(cursor_locked);
  LI("subsystem=clientruntime event=loop_starting loop=render");
  std::optional<RunFailure> failure;
  while (!impl_->renderer.ShouldClose()) {
    // Logged by the worker, where it failed.
    if (auto worker_failure = impl_->workers.Failure(); worker_failure.has_value()) {
      failure = std::move(*worker_failure);
      break;
    }
    if (const auto session_failure = impl_->session->GetFailure(); session_failure.has_value()) {
      LE("subsystem=clientruntime event=session_failed reason=\"{}\"", harness::DescribeFailure(*session_failure));
      failure = *session_failure;
      break;
    }
    if (const auto load_failure = impl_->GetReadyForLobby(); load_failure.has_value()) {
      LE("subsystem=clientruntime event=character_load_failed reason=\"{}\"", DescribeCharacterError(*load_failure));
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

}  // namespace augusta::client
