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
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/interpolation.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"
#include "augusta/runner.h"
#include "augusta/tick.h"
#include "character_loader.h"
#include "content.h"
#include "frame_mapping.h"
#include "lobby_readiness.h"
#include "net_stats.h"

namespace augusta::client {

namespace {

// Stops the Runner's threads and joins both, on scope exit - including
// when unwinding past Run() due to an exception from the Main/Render loop
// body. This is the only place thread cleanup happens; ~ClientRuntime relies
// on Run() having already run it (see that destructor's own doc comment in
// runtime.h).
struct RunnerJoiner {
  std::optional<harness::Runner>& runner;

  ~RunnerJoiner() { runner.reset(); }
};

}  // namespace

std::string DescribeRunFailure(const RunFailure& failure) {
  if (const auto* session = std::get_if<harness::Failure>(&failure)) {
    return harness::DescribeFailure(*session);
  }
  if (const auto* worker = std::get_if<failure::Failure>(&failure)) {
    return failure::DescribeFailure(*worker);
  }
  return DescribeCharacterError(std::get<CharacterError>(failure));
}

struct ClientRuntime::Impl {
  RuntimeConfig config;
  // Main/Render thread only: loads a character, and what to load in the Lobby
  // before reporting Ready.
  CharacterLoader load_character;
  LobbyReadiness lobby_readiness;
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

  // Main/Render thread only: the Server view's Authoritative State and Match
  // start as presentation's types, converted when they change and lent to
  // every render frame in between (NextFrameInput).
  ConvertedServerView converted_view;

  // What the last render frame showed the other players at, or nullopt while
  // it showed none: written once per Main/Render frame, read once per
  // Prediction tick, which reports it with the tick's Command (ADR-0044).
  std::mutex seen_time_mutex;
  std::optional<presentation::SeenTime> seen_time;

  // Connection numbers for the renderer's debug HUD: written by the Network
  // I/O thread (SampleNetworkStats), read by the Main/Render thread once per
  // frame. They should be consistent with each other, so - like
  // latest_tick above - a mutex-guarded copy.
  std::mutex hud_net_mutex;
  std::optional<renderer::DebugHudNetStats> latest_hud_net;

  // Network I/O thread only: what the HUD shows of the connection, and the
  // counters Nsight Systems plots of it, sampled once per round of the
  // Runner's network work.
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
    const auto added = map::AddCollision(world, map.collision).and_then([&] {
      return map::AddCollision(presentation, map.collision);
    });
    if (!added) {
      throw std::runtime_error(
          std::format("ClientRuntime: map collision rejected: {}", physics::DescribeCollisionMeshError(added.error())));
    }
    session.emplace(
        harness::SessionConfig{.server = cfg.server, .client_pack = cfg.client_pack, .character = cfg.character},
        std::move(world));
  }

  // Prediction thread only: the state of the tick before the newest, nullopt
  // before the first tick, which has none before it to blend from.
  std::optional<prediction::State> previous_tick;

  // The Runner's Command for each tick: the player's input, with what the last
  // render frame showed the other players at. Prediction thread.
  command::Command NextCommand() { return WithSeenTime(input.Sample(), GetSeenTime()); }

  // The Runner's word on each tick: publishes it, with the one before it, for
  // render frames to blend. Prediction thread.
  void PublishTick(const harness::PredictedTick& tick) {
    {
      const std::lock_guard<std::mutex> lock(latest_tick_mutex);
      latest_tick = {.previous = previous_tick.value_or(tick.state),
                     .latest = tick.state,
                     .start = tick.due,
                     .duration = tick.duration};
    }
    previous_tick = tick.state;
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

  // Why the run must end, if a worker or the session has failed. A worker has
  // logged its own failure where it failed; the session's is logged here.
  std::optional<RunFailure> GetRunFailure() {
    if (auto worker_failure = runner->Failure(); worker_failure.has_value()) {
      return std::move(*worker_failure);
    }
    if (const auto session_failure = session->GetFailure(); session_failure.has_value()) {
      LE("subsystem=clientruntime event=session_failed reason=\"{}\"", harness::DescribeFailure(*session_failure));
      return *session_failure;
    }
    return std::nullopt;
  }

  // Keeps the window responsive until the server admits this client, then
  // hands PresentationWorld the parameters it sent and its tick: what tracers
  // fly by and ADS zooms to. So every render frame is drawn admitted. Returns
  // why the run failed first, if it did; returns nothing if the window closed
  // first. Main/Render thread only.
  std::optional<RunFailure> WaitForAdmission() {
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    while (!renderer.ShouldClose()) {
      if (auto failure = GetRunFailure(); failure.has_value()) {
        return failure;
      }
      const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
      if (const std::optional<harness::Admission>& accepted = view->accepted; accepted.has_value()) {
        presentation.SetParameters(accepted->parameters, 1.0F / static_cast<float>(accepted->tick_rate_hz));
        return std::nullopt;
      }
      renderer.PumpEvents();
      std::this_thread::sleep_for(kPollInterval);
    }
    return std::nullopt;
  }

  // What this render frame is shown from: the latest Prediction ticks blended
  // by how far through the tick it is, where the player aims now, and what the
  // session has received, converted into presentation's own types here, at
  // ClientRuntime's edge (see frame_mapping.h). Takes the
  // Shots, Hit confirmations and Deaths received since the last frame.
  //
  // Everything the server has said is read from one Server view, so the
  // state, the bodies it names and the match it belongs to are of one moment
  // (ADR-0005), however the Network I/O thread interleaves with this one; the
  // events taken are of that view's match too. The snapshot and characters
  // are lent from converted_view, so the frame must run before the next call.
  presentation::FrameInput NextFrameInput() {
    const LatestTick latest = GetLatestTick();
    const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
    converted_view.Update(*view);
    presentation::FrameInput frame{
        .ticks = {.previous = latest.previous,
                  .latest = latest.latest,
                  .fraction = tick::FractionElapsed(latest.start, latest.duration, tick::Clock::now())},
        .aim = ToPresentation(input.CurrentAim()),
        .fire = input.IsHeld(input::Control::kFire),
        .local_entity = std::nullopt,
        .snapshot = converted_view.Snapshot(),
        .characters = converted_view.Characters(),
        .shots = {},
        .hit_confirmations = static_cast<std::uint32_t>(session->TakeHitConfirmations(*view).size()),
        .deaths = {},
        .health = view->authoritative.transform([](const harness::AuthoritativeState& state) { return state.health; }),
        .match_end = MatchEndOf(*view),
    };
    frame.local_entity = view->OwnEntity().transform([](harness::EntityId entity) { return ToPresentation(entity); });
    for (const harness::Shot& shot : session->TakeShots(*view)) {
      frame.shots.push_back(ToPresentation(shot));
    }
    for (const harness::Death& death : session->TakeDeaths(*view)) {
      frame.deaths.push_back(ToPresentation(death.victim));
    }
    return frame;
  }

  // Hands the renderer what the next RenderFrame draws of this frame, without
  // drawing it. The local player's own position isn't drawn yet (renderer.h) -
  // only remote players, each as its character. In the Lobby there are none,
  // so the map is drawn empty.
  void StageRenderFrame(const presentation::State& frame_state) {
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

  // The Prediction and Network I/O threads (ADR-0005), emplaced by Run().
  // Last, so its threads are joined before anything they use goes.
  std::optional<harness::Runner> runner;

  void SetSeenTime(const std::optional<presentation::SeenTime>& time) {
    const std::lock_guard<std::mutex> lock(seen_time_mutex);
    seen_time = time;
  }

  std::optional<presentation::SeenTime> GetSeenTime() {
    const std::lock_guard<std::mutex> lock(seen_time_mutex);
    return seen_time;
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
  impl.runner.emplace(*impl.session,
                      harness::RunnerHooks{
                          .next_command = [&impl] { return impl.NextCommand(); },
                          .on_tick = [&impl](const harness::PredictedTick& tick) { impl.PublishTick(tick); },
                          .on_network_round = [&impl] { impl.SampleNetworkStats(); },
                      });
  const RunnerJoiner joiner{.runner = impl.runner};

  std::optional<RunFailure> failure = impl.WaitForAdmission();
  if (failure.has_value()) {
    return failure;
  }

  bool cursor_locked = impl_->input.CursorCaptured();
  impl_->renderer.SetCursorLocked(cursor_locked);
  LI("subsystem=clientruntime event=loop_starting loop=render");
  while (!impl_->renderer.ShouldClose()) {
    failure = impl_->GetRunFailure();
    if (failure.has_value()) {
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
    const presentation::State frame_state = impl_->presentation.RunFrame(impl_->NextFrameInput());
    impl_->SetSeenTime(frame_state.seen_time);
    impl_->StageRenderFrame(frame_state);
    impl_->renderer.RenderFrame();
  }
  LI("subsystem=clientruntime event=loop_stopping loop=render");
  return failure;
}

}  // namespace augusta::client
