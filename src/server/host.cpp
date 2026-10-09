#include "host.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/first_failure.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "capture.h"
#include "content.h"
#include "heartbeat.h"
#include "host_impl.h"
#include "host_metrics.h"
#include "match.h"
#include "recording.h"
#include "simulation_mapping.h"
#include "tick_messages.h"
#include "wire.h"

namespace augusta::server {

namespace {

// The authoritative world with the map's collision already in it, recording
// to file if config asks for a recording (ADR-0048), its state counted into
// metrics. Built before the socket exists, so a map that is rejected never
// leaves a bound port behind.
RecordedSimulation BuildRecordedSimulation(const HostConfig& config, const Scenario& scenario, scripting::Engine policy,
                                           std::ofstream& file, HostMetrics& metrics) {
  simulation::World world = BuildSimulation(config.parameters, config.tick_rate_hz, scenario, std::move(policy));
  if (config.recording.empty()) {
    return {std::move(world), std::nullopt};
  }
  file.open(config.recording, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error(std::format("server::Host: cannot write a recording to {}", config.recording.string()));
  }
  LI("subsystem=server event=recording_enabled path={} mode={}", config.recording.string(),
     RecordingModeName(config.recording_mode));
  return {std::move(world),
          Recorder(file,
                   RecordingHeader{.engine_version = std::string(EngineVersion()),
                                   .server_pack = config.server_pack,
                                   .tick_rate_hz = config.tick_rate_hz},
                   RecorderOptions{.mode = config.recording_mode,
                                   .faults = config.faults,
                                   .on_state = [&metrics](RecordingState state) { SetRecordingState(metrics, state); },
                                   .capacity = kRecordQueueCapacity})};
}

// What captures each Match into the directory config names, if it names one
// (ADR-0050), creating it first; none otherwise.
std::unique_ptr<Capturer> BuildCapturer(const HostConfig& config, const assets::PackHash& client_pack) {
  if (config.capture_directory.empty()) {
    return nullptr;
  }
  std::error_code error;
  std::filesystem::create_directories(config.capture_directory, error);
  if (error) {
    throw std::runtime_error(std::format("server::Host: cannot create the capture directory {}: {}",
                                         config.capture_directory.string(), error.message()));
  }
  LI("subsystem=capture event=capture_enabled directory={}", config.capture_directory.string());
  return std::make_unique<Capturer>(config.capture_directory,
                                    CaptureHeader{.engine_version = std::string(EngineVersion()),
                                                  .server_pack = config.server_pack,
                                                  .client_pack = client_pack,
                                                  .tick_rate_hz = config.tick_rate_hz,
                                                  .started = {}},
                                    CaptureOptions{.faults = config.faults, .capacity = kCaptureQueueCapacity});
}

// The ticks of kMatchPause at tick_rate_hz, rounded up so the pause is never shorter.
std::uint32_t PauseTicks(std::uint8_t tick_rate_hz) {
  return static_cast<std::uint32_t>(std::ceil(std::chrono::duration<float>(kMatchPause).count() * tick_rate_hz));
}

// The path of each of characters, in the same order: all Match needs of them.
std::vector<std::string> CharacterPaths(const std::vector<Character>& characters) {
  std::vector<std::string> paths;
  paths.reserve(characters.size());
  for (const Character& character : characters) {
    paths.push_back(character.path);
  }
  return paths;
}

}  // namespace

Host::Impl::Impl(const HostConfig& config, Scenario scenario, scripting::Engine policy)
    : metrics(config.tick_rate_hz),
      simulation(BuildRecordedSimulation(config, scenario, std::move(policy), recording_file, metrics)),
      capturer(BuildCapturer(config, scenario.client_pack)),
      tick_rate_hz(config.tick_rate_hz),
      parameters(config.parameters),
      characters(ToSimulation(scenario.characters)),
      spawn_points(std::move(scenario.spawn_points)),
      network(config.listen, config.faults),
      match(MatchConfig{
          .engine_version = std::string(EngineVersion()),
          .client_pack = scenario.client_pack,
          .characters = CharacterPaths(scenario.characters),
          .player_count = config.parameters.player_count,
          .pause_ticks = PauseTicks(config.tick_rate_hz),
      }) {}

void Host::Impl::Deliver(networking::PeerId peer, const networking::Payload& payload) {
  // Dropped is the peer's outcome: its departure, if it is leaving, arrives as an event.
  if (networking::SendResult sent = SendCounted(network, metrics, peer, payload, networking::Reliability::kReliable);
      !sent.has_value()) {
    transport_failure.Record(std::move(sent.error()));
  }
}

void Host::Impl::Reply(networking::PeerId peer, const std::expected<networking::Payload, failure::Failure>& message) {
  if (!message.has_value()) {
    invariant_failure.Record(message.error());
    return;
  }
  Deliver(peer, *message);
}

void Host::Impl::SendTo(const std::vector<SessionId>& sessions,
                        const std::expected<networking::Payload, failure::Failure>& message) {
  if (!message.has_value()) {
    invariant_failure.Record(message.error());
    return;
  }
  for (const SessionId session : sessions) {
    Deliver(players.at(session).peer, *message);
  }
}

void Host::Impl::SendRoster() {
  const Roster roster = match.GetRoster();
  std::vector<SessionId> sessions;
  sessions.reserve(roster.players.size());
  for (const RosterEntry& entry : roster.players) {
    sessions.push_back(entry.session);
  }
  SendTo(sessions, EncodeToSend(ToWire(roster)));
}

void Host::Impl::SetLobbyGauges() {
  metrics.sessions.Set(static_cast<double>(players.size()));
  metrics.lobby_players.Set(static_cast<double>(match.GetRoster().players.size()));
  metrics.match_in_progress.Set(match.InMatch() ? 1.0 : 0.0);
}

Host::Host(const HostConfig& config, Scenario scenario, scripting::Engine policy)
    : impl_(std::make_unique<Impl>(config, std::move(scenario), std::move(policy))) {}

Host::~Host() = default;

networking::Endpoint Host::ListenEndpoint() const { return impl_->network.LocalEndpoint(); }

std::optional<failure::Failure> Host::TakeInvariantFailure() { return impl_->invariant_failure.Take(); }

void Host::RecordTiming(const tick::Timing& timing) {
  HostMetrics& metrics = impl_->metrics;
  metrics.tick_duration.Observe(std::chrono::duration<double>(timing.duration).count());
  metrics.ticks.Increment();
  metrics.ticks_late.Increment(timing.late ? 1 : 0);
  metrics.tick_overruns.Increment(timing.overrun ? 1 : 0);
  metrics.tick_resyncs.Increment(timing.resynchronised ? 1 : 0);
  const std::optional<Activity> second = impl_->heartbeat.Record(Totals(metrics), std::chrono::steady_clock::now());
  if (!second.has_value()) {
    return;
  }
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  LD("subsystem=serverruntime event=heartbeat tick={} players={} in_match={} ticks={} late={} overrun={} "
     "messages={} stale={} dropped={} overflow={} misbehaving={}",
     impl_->tick.load(), impl_->players.size(), impl_->match.InMatch(), second->ticks, second->late, second->overrun,
     second->messages, second->stale, second->dropped, second->overflow, second->misbehaving);
}

std::size_t Host::QueuedCommands(SessionId session) const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto player = impl_->players.find(session);
  if (!impl_->match.IsPlaying(session) || player == impl_->players.end()) {
    return 0;
  }
  return player->second.commands.Queued();
}

const HostMetrics& Host::Metrics() const { return impl_->metrics; }

std::optional<failure::Failure> Host::RecordingFailure() const {
  std::optional<failure::Failure> lost = impl_->simulation.RecordingFailure();
  if (lost.has_value() && failure::DispositionOf(lost->code) != failure::Disposition::kRuntime) {
    return std::nullopt;
  }
  return lost;
}

std::optional<failure::Failure> Host::FinishRecording() {
  impl_->simulation.WaitUntilRecorded();
  return RecordingFailure();
}

std::optional<failure::Failure> Host::TakeTransportFailure() { return impl_->transport_failure.Take(); }

}  // namespace augusta::server
