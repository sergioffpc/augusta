#include "augusta/client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nvtx3/nvtx3.hpp>

#include "augusta/audio.h"
#include "augusta/client_character_loader.h"
#include "augusta/client_content.h"
#include "augusta/client_phases.h"
#include "augusta/client_run.h"
#include "augusta/command.h"
#include "augusta/cues.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/input.h"
#include "augusta/interpolation.h"
#include "augusta/link.h"
#include "augusta/local_view.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/prediction.h"
#include "augusta/presentation.h"
#include "augusta/reenactment.h"
#include "augusta/renderer.h"
#include "augusta/runner.h"
#include "augusta/tick.h"
#include "augusta/window.h"
#include "augusta/window_events.h"
#include "frame_mapping.h"
#include "lobby_readiness.h"
#include "net_stats.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/debug_index.h"
#include "augusta/debug_interaction.h"
#include "augusta/debug_panels.h"
#include "augusta/hud.h"
#include "augusta/physics_overlays.h"
#include "debug_index_entries.h"
#include "profiling_panel.h"
#endif

namespace augusta::client {

namespace {

#if AUGUSTA_DEBUG_TOOLS
// The NVTX range, in the default domain, that `nsys profile
// --capture-range=nvtx --nvtx-capture=augusta.capture` records alone.
constexpr const char* kCaptureRange = "augusta.capture";

// What config runs the client as, for which debug tools it carries.
ClientMode ModeOf(const RuntimeConfig& config) {
  if (config.replay.has_value()) {
    return ClientMode::kReplayViewer;
  }
  return config.reenactment.has_value() ? ClientMode::kCapturedPlayer : ClientMode::kPlayer;
}

// The Debug index's entries for config's mode, as config starts them.
std::vector<debug::IndexEntry> DebugIndexEntries(const RuntimeConfig& config) {
  return DebugIndexEntries(
      DebugIndexStart{
          .hud = config.debug_hud,
          .profiling_panel = config.debug_profiling_panel,
          .physics_panel = config.debug_physics_panel,
      },
      ModeOf(config));
}

// Which Physics overlays start on: none for a Replay viewer, whose Debug index
// lists no Physics panel to switch them (ADR-0075).
presentation::PhysicsOverlaysStart PhysicsOverlaysStartOf(const RuntimeConfig& config) {
  if (ModeOf(config) == ClientMode::kReplayViewer) {
    return {};
  }
  return {.collision = config.debug_physics_collision, .server_bodies = config.debug_physics_server_bodies};
}
#endif

// The window the client opens, titled as the game.
window::Config WindowConfig() {
  window::Config config;
  config.title = "augusta";
  return config;
}

// A Debug build runs under Vulkan's validation layer (ADR-0059) wherever the
// loader finds it through VK_ADD_LAYER_PATH.
renderer::Config RendererConfig() {
  renderer::Config config;
#ifndef NDEBUG
  config.validation = renderer::Validation::kIfAvailable;
#endif
  return config;
}

// Stops the Runner's threads and joins both, on scope exit - including
// when unwinding past Run() due to an exception from the Main/Render loop
// body. This is the only place thread cleanup happens; ~ClientRuntime relies
// on Run() having already run it (see that destructor's own doc comment in
// augusta/client.h).
struct RunnerJoiner {
  std::optional<harness::Runner>& runner;

  ~RunnerJoiner() { runner.reset(); }
};

}  // namespace

struct ClientRuntime::Impl {
  RuntimeConfig config;
  // Main/Render thread only: loads a character, and what to load in the Lobby
  // before reporting Ready.
  CharacterLoader load_character;
  LobbyReadiness lobby_readiness;
  input::Input input;
  audio::Engine audio;
  // `session=` on every line from the admission WaitForAdmission sees, and
  // `match=` from each Match start the Session tells of to its end: declared
  // before the Session, so they end after it, its last lines included.
  std::optional<SessionLogContext> session_log;
  MatchLogContext match_log;
  // Emplaced by the constructor once the map is loaded into its PredictionWorld.
  std::optional<harness::Session> session;
  presentation::World presentation;
  // Main/Render thread only. The window is declared first so that it outlives
  // the Renderer, which draws into it.
  window::Window window;
  renderer::Renderer renderer;

  // A Captured player's run (ADR-0050), if this client is one: NextCommand is
  // the Prediction thread's, the rest the Main/Render thread's.
  std::optional<harness::Reenactment> reenactment;
  // The connection's round trip in ticks, written by the Network I/O thread
  // and read by the Prediction and Main/Render threads, for the reenactment's pacing.
  std::atomic<tick::Tick> round_trip_ticks{0};
  // The last Command the reenactment sent, which the view aims by: written by
  // the Prediction thread, read once per Main/Render frame.
  std::mutex reenacted_mutex;
  command::Command reenacted;
  // Main/Render thread only: the Deaths the server told and the newest server
  // tick seen, for what the run logs when it ends.
  std::vector<harness::ObservedDeath> observed_deaths;
  tick::Tick last_tick = 0;

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

  // Main/Render thread only: the Server view's recent Authoritative State updates and Match
  // start as presentation's types, converted when they change and lent to
  // every render frame in between (NextFrameInput); a Replay viewer's views
  // of the newest tick, lent to the frame they are converted for; and the
  // characters a Replay viewer has loaded.
  ConvertedServerView converted_view;
  std::vector<presentation::PlayerView> replay_views;
  std::set<std::string, std::less<>> replay_characters;

  // What the last render frame showed the other players at, or nullopt while
  // it showed none: written once per Main/Render frame, read once per
  // Prediction tick, which reports it with the tick's Command (ADR-0044).
  std::mutex seen_time_mutex;
  std::optional<presentation::SeenTime> seen_time;

  // Network I/O thread only: the counters Nsight Systems plots of the
  // connection, sampled once per round of the Runner's network work.
  NetStatsCounters net_stats_counters;

#if AUGUSTA_DEBUG_TOOLS
  // Connection numbers for the debug HUD: written by the Network
  // I/O thread (SampleNetworkStats), read by the Main/Render thread once per
  // frame. They should be consistent with each other, so - like
  // latest_tick above - a mutex-guarded copy.
  std::mutex hud_net_mutex;
  std::optional<hud::NetStats> latest_hud_net;

  // Network I/O thread only: what the HUD shows of the connection.
  HudNetStats hud_net_stats;
  // Main/Render thread only: the frames the HUD's FPS averages.
  hud::FrameTimeWindow frame_times;
  // Main/Render thread only: where each frame's window events go, F1 entering
  // and leaving Debug interaction, and the Debug index's switches, which last
  // until the client exits.
  debug::DebugInteraction debug_interaction{{.toggle = input::kReservedKey, .leave = input::kReleaseCursorKey}};
  debug::DebugIndex debug_index{DebugIndexEntries(config)};
  // Main/Render thread only: the Profiling panel, after the Renderer, whose
  // Aftermath status it shows, and the capture window it has open, if any.
  ProfilingPanel profiling_panel{renderer.GpuCrashDumps()};
  std::optional<nvtx3::unique_range> capture_range;
  // Main/Render thread only: which Physics Debug overlays are on, drawn each
  // frame whether the Physics panel is shown or not.
  presentation::PhysicsOverlays physics_overlays{PhysicsOverlaysStartOf(config)};

  std::optional<hud::NetStats> GetLatestHudNet() {
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    return latest_hud_net;
  }

  // Main/Render thread only: hands the Renderer this frame's HUD lines while
  // the Debug index has the HUD on, and none while it is off.
  void UpdateDebugHud() {
    // Each frame ends the last one for the FPS the HUD reads, shown or not, so
    // the HUD switched on reads the frames before it.
    frame_times.Tick(hud::FrameTimeWindow::Clock::now());
    if (!debug_index.IsOn(kDebugHudEntry)) {
      renderer.SetTextLines({});
      return;
    }
    renderer.SetTextLines(hud::Lines(frame_times.Average(), GetLatestHudNet()));
  }

  // Main/Render thread only: the frame's Debug panels, the Debug index in
  // Debug interaction only, then each panel the index has on, in or out of it.
  [[nodiscard]] debug::Panels DebugPanels() const {
    debug::Panels panels;
    if (debug_interaction.In()) {
      panels.push_back(debug_index.BuildPanel());
    }
    if (debug_index.IsOn(kProfilingPanelId)) {
      panels.push_back(profiling_panel.BuildPanel());
    }
    if (debug_index.IsOn(presentation::kPhysicsPanelId)) {
      panels.push_back(physics_overlays.BuildPanel());
    }
    return panels;
  }

  // Main/Render thread only: opens or closes the capture window's NVTX range
  // as the Profiling panel says.
  void ApplyCaptureChange(CaptureChange change) {
    switch (change) {
      case CaptureChange::kNone:
        break;
      case CaptureChange::kOpens:
        capture_range.emplace(kCaptureRange);
        LOG_INFO("subsystem=clientruntime event=profiling_capture_opened");
        break;
      case CaptureChange::kCloses:
        capture_range.reset();
        LOG_INFO("subsystem=clientruntime event=profiling_capture_closed");
        break;
    }
  }

  // Main/Render thread only, once per frame after its Main/Render Frame range
  // closes: the frame has ended for the Profiling panel's capture window.
  void TickProfilingPanel() { ApplyCaptureChange(profiling_panel.Tick()); }

  // Main/Render thread only: routes each control event the last RenderFrame's
  // panels gave to the area whose panel it names.
  void RouteDebugControlEvents() {
    for (const debug::ControlEvent& event : renderer.TakeDebugControlEvents()) {
      if (event.panel == debug::kDebugIndexPanelId) {
        debug_index.Update(event);
      } else if (event.panel == kProfilingPanelId) {
        ApplyCaptureChange(profiling_panel.Update(event));
      } else if (event.panel == presentation::kPhysicsPanelId) {
        physics_overlays.Update(event);
      }
    }
  }
#endif

  // Main/Render thread only: hands a frame's window events to Input, and in a
  // Development build those of Debug interaction to the Debug panels instead.
  void HandleWindowEvents(std::span<const window::Event> events) {
#if AUGUSTA_DEBUG_TOOLS
    const debug::Routing routing = debug_interaction.Route(events);
    input.Handle(routing.game);
    if (routing.entered) {
      input.LetGo();
    }
    if (routing.left_by_leave_key) {
      input.ReleaseCursor();
    }
    renderer.HandleDebugPanelInput(routing.panels);
#else
    input.Handle(events);
#endif
  }

  // Main/Render thread only: whether the window's cursor is locked for
  // mouselook, nullopt until the first UpdateCursorLock.
  std::optional<bool> cursor_locked;

  // Main/Render thread only: locks the window's cursor for mouselook while
  // Input captures it - Escape releases it and a click captures it again, as
  // Input decides - and in a Development build out of Debug interaction only.
  void UpdateCursorLock() {
#if AUGUSTA_DEBUG_TOOLS
    const bool locked = debug_interaction.CursorLocked(input.CursorCaptured());
#else
    const bool locked = input.CursorCaptured();
#endif
    if (locked != cursor_locked) {
      window.SetCursorLocked(locked);
      cursor_locked = locked;
    }
  }

  void SampleNetworkStats() {
    const std::optional<link::ConnectionStats> stats = session->GetConnectionStats();
    if (const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
        stats.has_value() && view->accepted.has_value()) {
      round_trip_ticks.store(harness::RoundTripTicks(stats->ping_ms, view->accepted->tick_rate_hz));
    }
    net_stats_counters.Sample(stats);
#if AUGUSTA_DEBUG_TOOLS
    // Whether the HUD is on or not: the Debug index can switch it on at any frame.
    const std::optional<hud::NetStats> net = hud_net_stats.Update(stats, std::chrono::steady_clock::now());
    const std::lock_guard<std::mutex> lock(hud_net_mutex);
    latest_hud_net = net;
#endif
  }

  Impl(const RuntimeConfig& cfg, const Map& map, const math::Vec3& eye, const audio::CueSounds& cue_sounds,
       CharacterLoader loader)
      : config(cfg),
        load_character(std::move(loader)),
        input(cfg.input),
        presentation(audio, cue_sounds, eye),
        window(WindowConfig()),
        renderer(window, RendererConfig()) {
    if (cfg.reenactment.has_value()) {
      reenactment.emplace(*cfg.reenactment);
    }
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
      throw failure::ClassifiedFailure(failure::Failure{
          .code = failure::Code::kInvalidContent,
          .context = {},
          .detail = std::format("map collision rejected: {}", physics::DescribeCollisionMeshError(added.error()))});
    }
    // A Captured player asks to join at its captured spawn (ADR-0050).
    session.emplace(
        harness::SessionConfig{
            .server = link::Endpoint{.address = cfg.server_address},
            .client_pack = cfg.client_pack,
            .character = cfg.character,
            .replay = cfg.replay,
            .spawn = cfg.reenactment.transform([](const harness::Script& script) { return script.spawn; }),
            .run = cfg.run,
            .on_match = [this](const boost::uuids::uuid& match) { match_log.Hold(match); }},
        std::move(world));
  }

  // Prediction thread only: the state of the tick before the newest, nullopt
  // before the first tick, which has none before it to blend from.
  std::optional<prediction::State> previous_tick;

  // The Runner's Command for each tick: the player's input, with what the last
  // render frame showed the other players at; or a Captured player's, paced
  // with its own Seen time, the input sampled and set aside. Prediction thread.
  // A Captured player skips the tick (nullopt) while enough of its Commands
  // already wait at the server.
  std::optional<command::Command> NextCommand() {
    const command::Command sampled = input.Sample();
    if (!reenactment.has_value()) {
      return WithSeenTime(sampled, GetSeenTime());
    }
    const std::optional<command::Command> next =
        reenactment->NextCommand(*session->GetServerView(), session->NextSequence(), round_trip_ticks.load());
    if (next.has_value()) {
      const std::lock_guard<std::mutex> lock(reenacted_mutex);
      reenacted = *next;
    }
    return next;
  }

  // Where the view aims and whether fire is held this frame: the player's
  // input, or a Captured player's last Command. Main/Render thread.
  std::pair<presentation::Aim, bool> CurrentAim() {
    if (!reenactment.has_value()) {
      return {ToPresentation(input.CurrentAim()), input.IsHeld(input::Control::kFire)};
    }
    const std::lock_guard<std::mutex> lock(reenacted_mutex);
    return {ToPresentation(input::Aim{.yaw = reenacted.yaw, .pitch = reenacted.pitch, .ads = reenacted.ads}),
            reenacted.fire};
  }

  // How a Captured player's run ends as of view, or nullopt while it plays
  // on or this client is none: at its captured Leave, or at the capture's
  // Match end or the server's (ADR-0050). Main/Render thread.
  std::optional<harness::Progress> ReenactmentEnding(const harness::ServerView& view) const {
    if (!reenactment.has_value()) {
      return std::nullopt;
    }
    const harness::Progress progress = reenactment->Check(view, round_trip_ticks.load());
    return progress == harness::Progress::kPlaying ? std::nullopt : std::optional(progress);
  }

  // Keeps what a Captured player's run compares with its capture: the newest
  // server tick of view, and the Deaths of its Match told and not yet taken,
  // each at that tick. Main/Render thread.
  void CollectOutcome(const harness::ServerView& view) {
    if (!reenactment.has_value()) {
      return;
    }
    if (view.authoritative.has_value()) {
      last_tick = std::max(last_tick, view.authoritative->tick);
    }
    for (const harness::Death& death : session->TakeDeaths(view)) {
      RecordDeath(death);
    }
  }

  // Keeps death, told as of the newest server tick seen, for a Captured
  // player's outcome. Main/Render thread.
  void RecordDeath(const harness::Death& death) {
    observed_deaths.push_back(
        harness::ObservedDeath{.tick = last_tick, .victim = death.victim, .killer = death.killer});
  }

  // Logs how a Captured player's run ended, and what it saw beside what its
  // capture holds. Main/Render thread.
  void LogOutcome(const harness::ServerView& view, harness::Progress how) const {
    LOG_INFO("subsystem=reenactment event={} player={}", how == harness::Progress::kLeave ? "left" : "ended",
             reenactment->GetScript().player);
    for (const std::string& line : reenactment->Outcome(view, observed_deaths, last_tick, how)) {
      LOG_INFO("subsystem=reenactment {}", line);
    }
  }

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
      LOG_INFO("subsystem=clientruntime event=character_loaded character={}", character);
      lobby_readiness.MarkLoaded(character);
    }
    session->ReportReady(plan->version);
    lobby_readiness.MarkReady(plan->version);
    return std::nullopt;
  }

  // A Replay viewer's characters to draw, once its Match has started: every
  // player's not loaded yet, uploaded and handed to PresentationWorld as
  // GetReadyForLobby does a Lobby's. A viewer is sent no Lobby and reports no
  // Ready (ADR-0051). Main/Render thread only. Returns why a character could
  // not be loaded, if one could not.
  std::optional<CharacterError> LoadReplayCharacters() {
    const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
    if (!view->match_start.has_value()) {
      return std::nullopt;
    }
    for (const harness::MatchPlayer& player : view->match_start->players) {
      if (replay_characters.contains(player.character)) {
        continue;
      }
      auto loaded = load_character(player.character);
      if (!loaded.has_value()) {
        return loaded.error();
      }
      renderer.SetCharacterMesh(player.character, loaded->mesh);
      presentation.SetCharacterEye(player.character, loaded->eye);
      LOG_INFO("subsystem=clientruntime event=character_loaded character={}", player.character);
      replay_characters.insert(player.character);
    }
    return std::nullopt;
  }

  // What to load before the next frame: a player's Lobby, or a Replay viewer's Match.
  std::optional<CharacterError> LoadCharacters() {
    return config.replay.has_value() ? LoadReplayCharacters() : GetReadyForLobby();
  }

  LatestTick GetLatestTick() {
    std::lock_guard<std::mutex> lock(latest_tick_mutex);
    return latest_tick;
  }

  // Why the run must end, if a worker or the session has failed, classified.
  // Not logged here: the application boundary writes the client's one terminal
  // event for it (augusta/application.h).
  std::optional<failure::Failure> GetRunFailure() {
    if (auto worker_failure = runner->Failure(); worker_failure.has_value()) {
      return std::move(*worker_failure);
    }
    if (const auto session_failure = session->GetFailure(); session_failure.has_value()) {
      return ClassifySessionFailure(*session_failure);
    }
    return std::nullopt;
  }

  // Keeps the window responsive until the server admits this client, then
  // has every line carry its Session ID (session_log), and
  // hands PresentationWorld the parameters it sent and its tick: what tracers
  // fly by and ADS zooms to. So every render frame is drawn admitted. Returns
  // why the run failed first, if it did; returns nothing if the window closed
  // first. Main/Render thread only.
  std::optional<failure::Failure> WaitForAdmission() {
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    while (!window.ShouldClose()) {
      if (auto failure = GetRunFailure(); failure.has_value()) {
        return failure;
      }
      const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
      if (const std::optional<harness::Admission>& accepted = view->accepted; accepted.has_value()) {
        session_log.emplace(accepted->session);
        if (reenactment.has_value()) {
          if (const std::optional<std::string> mismatch = reenactment->PlayerCountMismatch(*accepted)) {
            LOG_WARN("subsystem=reenactment {}", *mismatch);
          }
        }
        presentation.SetParameters(accepted->parameters, 1.0F / static_cast<float>(accepted->tick_rate_hz));
        return std::nullopt;
      }
      input.Handle(window.Poll());
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
  // events taken are of that view's match too. The updates and characters
  // are lent from converted_view, so the frame must run before the next call.
  presentation::FrameInput NextFrameInput() {
    const LatestTick latest = GetLatestTick();
    const std::shared_ptr<const harness::ServerView> view = session->GetServerView();
    converted_view.Update(*view);
    const auto [aim, fire] = CurrentAim();
    replay_views = ViewsOf(*view);
    const bool viewer = config.replay.has_value();
    presentation::FrameInput frame{
        .ticks = {.previous = latest.previous,
                  .latest = latest.latest,
                  .fraction = tick::FractionElapsed(latest.start, latest.duration, tick::Clock::now())},
        .aim = aim,
        .fire = fire,
        .local_entity = std::nullopt,
        .updates = converted_view.Updates(),
        .characters = converted_view.Characters(),
        .shots = {},
        .hit_confirmations = static_cast<std::uint32_t>(session->TakeHitConfirmations(*view).size()),
        .deaths = {},
        // A Replay viewer has no health of its own to lose.
        .health = viewer ? std::optional<float>()
                         : view->authoritative.transform(
                               [](const harness::AuthoritativeState& state) { return state.health; }),
        .match_end = MatchEndOf(*view),
        .replay_viewer = viewer,
        .views = replay_views,
    };
    frame.local_entity = view->OwnEntity().transform([](harness::EntityId entity) { return ToPresentation(entity); });
    for (const harness::Shot& shot : session->TakeShots(*view)) {
      frame.shots.push_back(ToPresentation(shot));
    }
    if (reenactment.has_value() && view->authoritative.has_value()) {
      last_tick = std::max(last_tick, view->authoritative->tick);
    }
    for (const harness::Death& death : session->TakeDeaths(*view)) {
      frame.deaths.push_back(ToPresentation(death.victim));
      if (reenactment.has_value()) {
        RecordDeath(death);
      }
    }
    return frame;
  }

  // Hands the renderer what the next RenderFrame draws of this frame, without
  // drawing it. The local player's own position isn't drawn yet (renderer.h) -
  // only remote players, each as its character. In the Lobby there are none,
  // so the map is drawn empty. In a Development build, the Debug overlays are
  // built from what the frame was shown from (input).
  void StageRenderFrame([[maybe_unused]] const presentation::FrameInput& input,
                        const presentation::State& frame_state) {
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
#if AUGUSTA_DEBUG_TOOLS
    // Shapes and panels live one frame (ADR-0075).
    renderer.SetDebugShapes(physics_overlays.BuildShapes(input));
    renderer.SetDebugPanels(DebugPanels());
    // What the Prediction thread's next ticks draw of the client's World,
    // culled around this frame's camera; off while Collision is, so a closed
    // overlay costs nothing.
    if (runner.has_value()) {
      runner->SetVisualization(physics_overlays.Visualization(frame_state.camera.position));
    }
#endif
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

  // Main/Render thread only: one render frame, from the window's events to
  // RenderFrame, inside the Main/Render Frame range.
  void RunRenderFrame() {
    const nvtx3::scoped_range range{"Main/Render Frame"};
    HandleWindowEvents(window.Poll());
    UpdateCursorLock();
#if AUGUSTA_DEBUG_TOOLS
    UpdateDebugHud();
#endif
    const presentation::FrameInput input = NextFrameInput();
    const presentation::State frame_state = presentation.RunFrame(input);
    SetSeenTime(frame_state.seen_time);
    StageRenderFrame(input, frame_state);
    renderer.RenderFrame();
  }
};

ClientRuntime::ClientRuntime(const RuntimeConfig& config, Content content)
    : impl_(std::make_unique<Impl>(config, content.map, content.eye, content.cue_sounds,
                                   std::move(content.load_character))) {
  impl_->renderer.SetScene(content.scene);
}

ClientRuntime::~ClientRuntime() = default;

std::optional<failure::Failure> ClientRuntime::Run() {
  Impl& impl = *impl_;
  impl.runner.emplace(*impl.session,
                      harness::RunnerHooks{
                          .next_command = [&impl] { return impl.NextCommand(); },
                          .on_tick = [&impl](const harness::PredictedTick& tick) { impl.PublishTick(tick); },
                          .on_network_round = [&impl] { impl.SampleNetworkStats(); },
                          .on_failure = {},
                      });
  const RunnerJoiner joiner{.runner = impl.runner};

  std::optional<failure::Failure> failure = impl.WaitForAdmission();
  if (failure.has_value()) {
    return failure;
  }

  window::Window& window = impl_->window;
  impl_->UpdateCursorLock();
  LOG_INFO("subsystem=clientruntime event=loop_starting loop=render");
  while (!window.ShouldClose()) {
    failure = impl_->GetRunFailure();
    if (failure.has_value()) {
      break;
    }
    // A Replay viewer's run is over once its Replay is: the server has closed the connection.
    if (impl_->session->ReplayEnded()) {
      LOG_INFO("subsystem=clientruntime event=replay_ended");
      break;
    }
    if (const auto load_failure = impl_->LoadCharacters(); load_failure.has_value()) {
      failure = ClassifyCharacterError(*load_failure);
      break;
    }
    const std::shared_ptr<const harness::ServerView> view = impl_->session->GetServerView();
    if (const std::optional<harness::Progress> ending = impl_->ReenactmentEnding(*view)) {
      // The Deaths no frame took yet, the ones that ended the Match among them.
      impl_->CollectOutcome(*view);
      impl_->LogOutcome(*view, *ending);
      break;
    }
    impl_->RunRenderFrame();
#if AUGUSTA_DEBUG_TOOLS
    // Between two frames' ranges, so the capture window holds whole frames.
    // The frame ends before the events are routed: a window opened by them
    // starts with the next frame.
    impl_->TickProfilingPanel();
    impl_->RouteDebugControlEvents();
#endif
  }
  LOG_INFO("subsystem=clientruntime event=loop_stopping loop=render");
  return failure;
}

}  // namespace augusta::client
