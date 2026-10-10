#ifndef AUGUSTA_SERVER_REPLAY_SERVER_H_
#define AUGUSTA_SERVER_REPLAY_SERVER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "augusta/tick.h"
#include "connection_sample.h"
#include "content.h"
#include "host_metrics.h"
#include "match.h"
#include "policy_loader.h"
#include "replay.h"
#include "replay_catalog.h"

/// \file
/// augusta::server::ReplayServer is augustad in the replay mode of ADR-0051:
/// with `replay.captures` set it runs no Lobby and no Match, only Replays. It
/// is the network boundary and the Replays' SimulationWorlds without the
/// threads and the clock, as server::Host is a live server's: ServerRuntime
/// runs PumpNetwork on the Network I/O thread and Tick on the Simulation
/// thread, and a test calls both by hand.
///
/// A connection's first message decides what it gets. A Replay list request
/// is answered with the captures the server replays (ReplayCatalog), and the
/// connection closed. A Replay request is checked as a Join request is - the
/// engine version, then the client pack - then its capture, matched against
/// the listing, then whether `replay.max_viewers` Replays are already running;
/// accepted, it starts a Replay of its own for that viewer from its first tick.
/// Every Join request is refused with _replay server_, by the one path that
/// refuses whatever asks to play here (RefuseToPlay); a Reenact request
/// (ADR-0050) is to be refused by it too.
///
/// A Replay viewer is sent what a Match's Spectator is: Join accepted with the
/// tick rate and Parameters (and session 0, which no player has), the Match
/// start with its first tick, then each tick's Authoritative State, Shots and
/// Deaths, with a Replay view of every player's pitch and ADS (ViewerMessages),
/// and the Match end, after which the server closes the connection. Each
/// Replay logs the first Death or Match end that differs from its capture's
/// (NFR-09). Both threads count what they do into the server's metrics, as
/// Host's do (host_metrics.h, ADR-0049).
namespace augusta::server {

/// Everything a ReplayServer needs beyond its scenario.
struct ReplayServerConfig {
  /// The rate, in Hz, at which every Replay ticks: only captures made at it are replayed.
  std::uint8_t tick_rate_hz = 0;
  /// What every Replay's World runs on and every viewer is told.
  parameters::Parameters parameters{};
  networking::Endpoint listen{};
  /// The hash of the server pack the content was loaded from: only captures made on it are replayed.
  assets::PackHash server_pack{};
  /// The directory of captures to replay, which must exist.
  std::filesystem::path captures;
  /// How many Replays run at once, at least 1.
  std::size_t max_viewers = 1;
  /// For a test: asked at listener setup and at every send and receive; null
  /// otherwise. Must outlive the ReplayServer.
  failure::Faults* faults = nullptr;
};

/// One message a Replay viewer is sent, encoded, and how.
struct ViewerMessage {
  networking::Payload payload;
  networking::Reliability reliability{};
};

/// What a Replay viewer is sent for tick of its Replay, in order: the tick's
/// Authoritative State, with every body and none of a player's own fields,
/// unreliably; every Shot and Death, reliably; its Replay view, unreliably;
/// and, on its last tick, the Match end, reliably. No Hit confirmation: the
/// viewer fired nothing. Every message is encoded before any is returned, so
/// if the protocol cannot carry one, the broken invariant is returned instead
/// (EncodeToSend in wire.h), as ForEachTickMessage does a live tick's.
[[nodiscard]] std::expected<std::vector<ViewerMessage>, failure::Failure> ViewerMessages(const ReplayTick& tick);

/// What of listings a replay server's Replay list names: every one a Replay
/// request can name, the newest the list holds if there are more.
[[nodiscard]] std::vector<ReplayListing> ListedOf(const std::vector<ReplayListing>& listings);

/// A replay server's listening socket and its Replays, without threads or a clock.
class ReplayServer {
 public:
  /// Builds one World of scenario first, so a Map that is rejected throws
  /// std::runtime_error before a socket exists, as Host's constructor does;
  /// throws std::runtime_error too if config.captures is not a directory, and
  /// networking::TransportFailure if the address can't be bound.
  ReplayServer(const ReplayServerConfig& config, Scenario scenario, PolicyMaker policy);
  ~ReplayServer();

  ReplayServer(const ReplayServer&) = delete;
  ReplayServer& operator=(const ReplayServer&) = delete;
  ReplayServer(ReplayServer&&) = delete;
  ReplayServer& operator=(ReplayServer&&) = delete;

  /// The address it listens on, with the port it chose if config.listen named port 0.
  [[nodiscard]] networking::Endpoint ListenEndpoint() const;

  /// One round of the Network I/O thread's work, at now: connections, and the
  /// requests they bring. A Replay is built here, its capture read and its
  /// World made, so the Simulation thread only ticks.
  void PumpNetwork(std::chrono::steady_clock::time_point now);

  /// Runs one tick of every Replay and sends each viewer what it resolved; a
  /// Replay that ends is told its viewer, whose connection is then closed.
  void Tick();

  /// As Host::RecordTiming. From the Simulation thread, after each Tick.
  void RecordTiming(const tick::Timing& timing);

  /// As Host::SampleConnections: no connection here carries a Session.
  [[nodiscard]] std::vector<ConnectionSample> SampleConnections();

  /// How many Replays are running. From any thread.
  [[nodiscard]] std::size_t Viewers() const;

  /// As Host's. From any thread.
  [[nodiscard]] const HostMetrics& Metrics() const;
  [[nodiscard]] std::optional<failure::Failure> TakeTransportFailure();
  [[nodiscard]] std::optional<failure::Failure> TakeInvariantFailure();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_REPLAY_SERVER_H_
