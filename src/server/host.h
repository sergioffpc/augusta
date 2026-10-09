#ifndef AUGUSTA_SERVER_HOST_H_
#define AUGUSTA_SERVER_HOST_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/physics.h"
#include "augusta/scripting.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "capture_retention.h"
#include "connection_sample.h"
#include "content.h"
#include "host_metrics.h"
#include "match.h"
#include "recording.h"

/// \file
/// augusta::server::Host is the server's network boundary and the
/// authoritative SimulationWorld (ADR-0023) without the threads and the clock:
/// ServerRuntime (src/server) runs PumpNetwork on the Network I/O thread and
/// Tick on the Simulation thread at a fixed rate (ADR-0005), reporting how each
/// Tick kept to its schedule to RecordTiming, while a test calls PumpNetwork and
/// Tick by hand, so a match can be driven tick by tick with no sleeping.
///
/// Admitted players wait in the Lobby, and a match starts on the tick the Lobby
/// is full and everyone is Ready (ADR-0043): only then are bodies simulated and
/// Authoritative States sent, and only to the players in the match. It ends after
/// the tick on which Game policy decides it has (ADR-0023), or once its last
/// player leaves, and its players are back in the Lobby.
///
/// The Network I/O thread's PumpNetwork and the Simulation thread's Tick may
/// run concurrently: what they share (the Lobby, the match and the players'
/// commands) is guarded inside. Both count what they do into the Host's metrics
/// (host_metrics.h, ADR-0049) as they do it, lock-free, and count only what the
/// transport accepted. A peer's malformed input or departure stays that peer's
/// (dropped, judged, disconnected); a failure of the local transport is the
/// runtime's, which Host keeps for a worker to take (TakeTransportFailure) and
/// escalate (ADR-0033).
namespace augusta::server {

/// Everything a Host needs to construct SimulationWorld and start listening.
struct HostConfig {
  /// The rate, in Hz, at which the simulation ticks and every client predicts;
  /// told to each client when it joins. Fixed for the life of the server process.
  std::uint8_t tick_rate_hz = 0;
  /// What the simulation runs on and what each client is told when it joins:
  /// the stamina rules of every player body, shared with PredictionWorld, and
  /// the Player count a match starts with.
  parameters::Parameters parameters{};
  /// Local address to listen on (US-01).
  networking::Endpoint listen{};
  /// Where to write a recording of every tick SimulationWorld runs (ADR-0048),
  /// replacing any file there; empty records none.
  std::filesystem::path recording;
  /// What losing a tick of that recording costs: an optional one degrades
  /// while the Host goes on, a strict one is Host::RecordingFailure.
  RecordingMode recording_mode = RecordingMode::kOptional;
  /// The hash of the server pack the content was loaded from, which a recording and a capture name.
  assets::PackHash server_pack{};
  /// The directory to capture every Match into (ADR-0050), created if
  /// missing; empty captures none.
  std::filesystem::path capture_directory;
  /// What that directory is kept within, oldest capture first; off by default.
  CaptureRetention capture_retention{};
  /// For a test: asked at listener setup, at every send and receive
  /// (networking.h) and at the recording's write and flush, so the transport
  /// or the disk fails there; null otherwise. Must outlive the Host.
  failure::Faults* faults = nullptr;
};

/// The server's listening socket and its SimulationWorld, without threads or a clock.
class Host {
 public:
  /// Constructs SimulationWorld with scenario's collision (throws
  /// std::runtime_error if a map mesh, or a character's hitbox, is not a whole
  /// triangle list) and the scenario's Game policy (none by default), and starts
  /// listening (throws networking::TransportFailure if the address can't be
  /// bound, or std::runtime_error if HostConfig::recording can't be written or
  /// HostConfig::capture_directory can't be created).
  /// Content is loaded from the server pack by the caller (see content.h).
  Host(const HostConfig& config, Scenario scenario, scripting::Engine policy = {});
  ~Host();

  /// Not copyable or movable: owns the listening socket.
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  /// The address it listens on: HostConfig::listen's, with the port it chose if that named port 0.
  [[nodiscard]] networking::Endpoint ListenEndpoint() const;

  /// The first message or record the Host could not encode, as the broken
  /// invariant it is (failure::Code::kInvariantViolated, ADR-0033): what it was
  /// is sent to no one and recorded nowhere, and the runtime must stop.
  /// PumpNetwork and Tick may each find one, so the thread that runs each asks
  /// after it. Given once, as TakeTransportFailure is, so the runtime reports
  /// it once; nullopt before one and after it has been taken. From any thread.
  [[nodiscard]] std::optional<failure::Failure> TakeInvariantFailure();

  /// Does one round of the Network I/O thread's work, at now: connection events
  /// and received messages. A peer that keeps sending what no honest client
  /// sends (MisbehaviourTracker), or is not admitted to the Lobby within
  /// kAdmissionDeadline of connecting (AdmissionDeadlines), is disconnected,
  /// and leaves as if it had left.
  void PumpNetwork(std::chrono::steady_clock::time_point now);

  /// The transport's measurements of every open connection, each with the
  /// Session it carries if its client has joined: what ConnectionHealth
  /// (connection_health.h) records. Each call clears the transport's worst-jitter mark, so one
  /// caller samples, once a heartbeat interval (ADR-0049): the Network I/O
  /// thread.
  [[nodiscard]] std::vector<ConnectionSample> SampleConnections();

  /// Runs one fixed tick of SimulationWorld on one command per player in the
  /// match, sends each of them its update and, reliably (ADR-0044), every Shot
  /// and Death of the tick and the Hit confirmations of its own hits, logs the
  /// tick's hits and deaths, and returns what the tick resolved and what Game
  /// policy decided on it. Starts a match first if the
  /// Lobby is full and Ready and the pause after the last one
  /// (server::kMatchPause, counted in these ticks) has passed. Then takes the
  /// actions Game policy took on the tick, typed and validated by
  /// SimulationWorld (simulation::TickResult): a Match end ends the match
  /// after the tick, as EndMatch does, with policy's winner or as a draw.
  simulation::TickResult Tick(float delta_time);

  /// Counts the Tick just run, with how it kept to the Simulation loop's
  /// schedule, into the metrics, and writes the once-a-second heartbeat line
  /// (ADR-0029) when it is due. From the Simulation thread, after each Tick; a
  /// test that has no schedule need not call it.
  void RecordTiming(const tick::Timing& timing);

  /// Ends the match in progress as a draw, if one is: its players are sent
  /// Match end and are back in the Lobby, and their bodies and the bullets still
  /// in flight leave the simulation on the next Tick. A test's way to end a
  /// match; Game policy's is the Match end of a tick's state. From the
  /// Simulation thread, between Ticks.
  void EndMatch();

  /// How many commands the player of session has queued for the coming Ticks, 0
  /// if it is not in the match. With one queued, the next Tick moves it by a
  /// command it sent rather than holding its last movement: a test's way to
  /// tick only once what it sent has arrived. From any thread.
  [[nodiscard]] std::size_t QueuedCommands(SessionId session) const;

  /// What it has counted (ADR-0049), for the metrics endpoint to collect and a
  /// test to read. From any thread; it lives as long as the Host.
  [[nodiscard]] const HostMetrics& Metrics() const;

  /// The failure a strict recording lost a tick on
  /// (failure::Code::kStrictRecordingFailed), which the runtime must stop on
  /// before it ticks again; nullopt while it has lost none, or when the
  /// recording is optional, whose loss only degrades it. From any thread.
  [[nodiscard]] std::optional<failure::Failure> RecordingFailure() const;

  /// RecordingFailure once every tick run so far is written, waiting for the
  /// recording's writer: the last word on whether a strict recording is whole,
  /// for the runtime to ask after its last tick. Its writer finds a loss up to
  /// kRecordQueueCapacity ticks after the tick it lost, which RecordingFailure
  /// alone misses at a stop. From the Simulation thread, between Ticks.
  [[nodiscard]] std::optional<failure::Failure> FinishRecording();

  /// The first failure of the local transport PumpNetwork or Tick met (a send,
  /// or a receive, it refused: failure::Code::kTransportSendFailed,
  /// kTransportReceiveFailed), once; nullopt before one and after it has been
  /// taken. A runtime failure: the worker that takes it returns it to the
  /// supervisor, which stops the runtime. Never a peer's doing - a peer's
  /// malformed input or departure is handled as that peer's. From any thread.
  [[nodiscard]] std::optional<failure::Failure> TakeTransportFailure();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_H_
