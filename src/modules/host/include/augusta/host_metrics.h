#ifndef AUGUSTA_HOST_METRICS_H_
#define AUGUSTA_HOST_METRICS_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <prometheus/collectable.h>
#include <prometheus/metric_family.h>

#include "augusta/body_part.h"
#include "augusta/capture_retention.h"
#include "augusta/capturer.h"
#include "augusta/command_queue.h"
#include "augusta/heartbeat.h"
#include "augusta/host_log.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "augusta/lock_free_metrics.h"
#include "augusta/match.h"
#include "augusta/misbehaviour.h"
#include "augusta/tick.h"

/// \file
/// What the server counts about itself, for the metrics endpoint (metrics.h)
/// to expose: ADR-0049's catalogue of the Tick, Lobby and Match, Sessions,
/// Misbehaviour, Network, Combat and Capture families, each named and labelled
/// as it says.
/// The Host owns one, and its Network I/O and Simulation threads, and its
/// Capturer's writer (through CaptureMetrics), write each value in place where
/// the event happens; the endpoint's thread only collects. Every
/// counter, gauge and histogram here is lock-free (augusta/lock_free_metrics.h), so
/// neither thread ever waits on a scrape. The heartbeat line (augusta/heartbeat.h) reads its totals from the
/// same counters (Totals), so the line and the series count each event once.
/// Every label takes its values from a closed set, one of the server's own enums
/// where it has one, never from text a peer sent; each range of counters is
/// checked against its enum at compile time. The Process family is the
/// endpoint's; Connection health is not counted here.
namespace augusta::host {

/// The Networking Protocol's message types (ADR-0038), as the metrics label
/// them: the Link's (augusta/link_types.h), since the protocol's stay
/// beneath it.
using MessageType = link::MessageType;

using JoinRefusalCounters =
    metrics::EnumCounters<JoinRefusal, JoinRefusal::kVersionMismatch, JoinRefusal::kReenactmentsNotAccepted>;
using LeavingCounters = metrics::EnumCounters<Leaving, Leaving::kLeft, Leaving::kMisbehaving>;
/// The misbehaviours only: the first of PeerRejection's values (augusta/misbehaviour.h),
/// which host_metrics.cpp checks.
using MisbehaviourCounters =
    metrics::EnumCounters<PeerRejection, PeerRejection::kUndecodable, PeerRejection::kCommandsBeforeJoining>;
using MessageCounters = metrics::EnumCounters<MessageType, MessageType::kJoinRequest, link::kLastMessageType>;
using RejectionCounters = metrics::EnumCounters<Rejection, Rejection::kStale, Rejection::kOutOfRange>;
using BodyPartCounters =
    metrics::EnumCounters<body_part::BodyPart, body_part::BodyPart::kHead, body_part::BodyPart::kLimb>;
using CaptureStopCounters =
    metrics::EnumCounters<CaptureStop, CaptureStop::kRecordTooLong, CaptureStop::kRetentionBudget>;

/// Everything ADR-0049's catalogue has the server count but its Process family
/// and Connection health, written in place by the Host's threads, and collected
/// in Prometheus' form by the metrics endpoint's. Durations are in seconds and
/// sizes in bytes, as the metrics are named.
struct HostMetrics final : prometheus::Collectable {
  explicit HostMetrics(std::uint8_t tick_rate_hz);

  /// The families of every metric here, named, typed and labelled as the
  /// catalogue says. From the metrics endpoint's thread.
  [[nodiscard]] std::vector<prometheus::MetricFamily> Collect() const override;

  // Tick, written by the Simulation thread.
  const std::uint8_t tick_rate_hz;
  metrics::Histogram tick_duration;
  metrics::Counter ticks;
  metrics::Counter ticks_late;
  metrics::Counter tick_overruns;
  metrics::Counter tick_resyncs;

  // Lobby and Match.
  metrics::Gauge lobby_players;
  metrics::Gauge match_in_progress;
  metrics::Gauge match_players_alive;
  metrics::Counter matches_started;
  metrics::Counter matches_ended_with_winner;
  metrics::Counter matches_ended_drawn;
  /// Ended because its last player left.
  metrics::Counter matches_ended_abandoned;
  metrics::Histogram match_duration;

  // Sessions.
  metrics::Gauge sessions;
  metrics::Counter joins_admitted;
  JoinRefusalCounters joins_refused;
  /// Peers whose connection ended before they were admitted to the Lobby,
  /// players who left the Lobby, and who left a match in progress, by how.
  LeavingCounters disconnects_before_admission;
  LeavingCounters disconnects_from_lobby;
  LeavingCounters disconnects_from_match;

  // Misbehaviour.
  MisbehaviourCounters misbehaviour;

  // Network: what the server sends and receives, as payloads.
  metrics::Counter sent_bytes;
  metrics::Counter received_bytes;
  MessageCounters messages_sent;
  /// Messages that decoded, by type.
  MessageCounters messages_received;
  metrics::Histogram authoritative_state_update_bytes;
  /// Every command of every Commands message.
  metrics::Counter commands_received;
  /// Commands the command queue turned away, by why; those it took in but
  /// dropped as the oldest of a full queue; those of a player not in a match;
  /// and those of a peer that has not joined. Every command counted received
  /// is either taken in or one of these; one a peer sent after the command
  /// that got it disconnected is neither received nor discarded.
  RejectionCounters commands_rejected;
  metrics::Counter commands_overflowed;
  metrics::Counter commands_outside_match;
  metrics::Counter commands_before_joining;

  // Combat, written by the Simulation thread.
  metrics::Counter shots;
  BodyPartCounters hit_confirmations;
  metrics::Histogram shooters_delay;
  /// Rounds whose Shooter's delay the cap held (simulation::kMaxShootersDelay).
  metrics::Counter shooters_delay_capped;
  /// Bullets still flying after the last tick (simulation::State's bullets_in_flight).
  metrics::Gauge bullets_in_flight;

  // Capture, written by the Simulation thread and the Capturer's writer
  // through CaptureMetrics.
  std::atomic<CaptureState> capture_state{CaptureState::kOff};
  static_assert(std::atomic<CaptureState>::is_always_lock_free);
  metrics::Counter captures_started;
  metrics::Counter captures_completed;
  CaptureStopCounters capture_stops;
  metrics::Counter capture_written_bytes;
  metrics::Gauge capture_queue_records;
  metrics::Gauge capture_directory_files;
  metrics::Gauge capture_directory_bytes;
  metrics::Counter capture_retention_deleted;
  metrics::Counter capture_retention_failures;
  /// The capture directory's retention limits (#461), each absent while unset.
  /// Written once, as the Host is built, before the metrics endpoint serves.
  std::optional<std::size_t> capture_retention_max_files;
  std::optional<std::uintmax_t> capture_retention_max_bytes;

  // Replay (ADR-0051), a replay server's only, written by the Simulation
  // thread: how many Replays run, and how long one Replay's tick takes - its
  // SimulationWorld's tick and what it sends its viewer - of the tick the
  // Simulation thread runs every Replay in.
  metrics::Gauge replays;
  metrics::Histogram replay_tick_duration;
  /// The Match captures' health, written by whichever thread it changes on
  /// (CaptureMetrics::OnHealth) and published whole, so a scrape never sees
  /// two states or none; nullopt while nothing is captured.
  std::atomic<std::optional<CaptureHealth>> capture_health;
  static_assert(std::atomic<std::optional<CaptureHealth>>::is_always_lock_free);
};

/// Counts what a Capturer tells it into metrics' Capture family, lock-free,
/// from whichever thread tells it. It must not outlive metrics.
class CaptureMetrics final : public CaptureObserver {
 public:
  explicit CaptureMetrics(HostMetrics& metrics) : metrics_(metrics) {}

  /// Publishes retention's limits, as the Host is built.
  void SetRetention(const CaptureRetention& retention);

  void OnState(CaptureState state) override;
  void OnStarted() override;
  void OnCompleted() override;
  void OnStopped(CaptureStop stop) override;
  void OnWritten(std::size_t bytes) override;
  void OnQueued(std::size_t records) override;
  void OnDirectory(CaptureDirectoryUsage usage) override;
  void OnRetentionDeleted() override;
  void OnRetentionDeleteFailed() override;
  void OnHealth(CaptureHealth health) override;

 private:
  HostMetrics& metrics_;
};

/// Counts a tick the Simulation loop ran with timing into metrics' Tick
/// family, and returns what Network I/O and the ticks did over the heartbeat
/// interval it ends (augusta/heartbeat.h), at now, if it ends one: the line its server
/// writes. How every server counts its ticks.
[[nodiscard]] std::optional<Activity> CountTick(HostMetrics& metrics, Heartbeat& heartbeat, const tick::Timing& timing,
                                                std::chrono::steady_clock::time_point now);

/// The heartbeat's running totals (augusta/heartbeat.h), read from metrics' counters:
/// the heartbeat line counts nothing of its own.
[[nodiscard]] Activity Totals(const HostMetrics& metrics);

/// Counts message, which the server is sending, by its type and size.
void CountSent(HostMetrics& metrics, const link::Outgoing& message);

}  // namespace augusta::host

#endif  // AUGUSTA_HOST_METRICS_H_
