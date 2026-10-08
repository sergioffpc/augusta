#ifndef AUGUSTA_SERVER_HOST_METRICS_H_
#define AUGUSTA_SERVER_HOST_METRICS_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <prometheus/collectable.h>
#include <prometheus/metric_family.h>

#include "augusta/ballistics.h"
#include "command_queue.h"
#include "heartbeat.h"
#include "host_log.h"
#include "lock_free_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "recording.h"

/// \file
/// What the server counts about itself, for the metrics endpoint (metrics.h)
/// to expose: ADR-0049's catalogue of the Tick, Lobby and Match, Sessions,
/// Misbehaviour, Network, Combat and Recording families, each named and labelled
/// as it says.
/// The Host owns one, and its Network I/O and Simulation threads write each value
/// in place where the event happens; the endpoint's thread only collects. Every
/// counter, gauge and histogram here is lock-free (lock_free_metrics.h), so
/// neither thread ever waits on a scrape. The heartbeat line (heartbeat.h) reads its totals from the
/// same counters (Totals), so the line and the series count each event once.
/// Every label takes its values from a closed set, one of the server's own enums
/// where it has one, never from text a peer sent; each range of counters is
/// checked against its enum at compile time. The Process family is the
/// endpoint's; Connection health is not counted here.
namespace augusta::server {

/// The Networking Protocol's message types (ADR-0038), as the metrics label
/// them: the server's own, since the protocol's stay at its edge (wire.h).
enum class MessageType : std::uint8_t {
  kJoinRequest,
  kJoinAccepted,
  kJoinRefused,
  kCommands,
  kAuthoritativeState,
  kLobby,
  kReady,
  kMatchStart,
  kMatchEnd,
  kShot,
  kHitConfirmation,
  kDeath,
};

using JoinRefusalCounters = EnumCounters<JoinRefusal, JoinRefusal::kVersionMismatch, JoinRefusal::kPackMismatch>;
using LeavingCounters = EnumCounters<Leaving, Leaving::kLeft, Leaving::kMisbehaving>;
/// The misbehaviours only: the first of PeerRejection's values (misbehaviour.h),
/// which host_metrics.cpp checks.
using MisbehaviourCounters =
    EnumCounters<PeerRejection, PeerRejection::kUndecodable, PeerRejection::kCommandsBeforeJoining>;
using MessageCounters = EnumCounters<MessageType, MessageType::kJoinRequest, MessageType::kDeath>;
using RejectionCounters = EnumCounters<Rejection, Rejection::kStale, Rejection::kOutOfRange>;
using BodyPartCounters = EnumCounters<ballistics::BodyPart, ballistics::BodyPart::kHead, ballistics::BodyPart::kLimb>;

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
  Histogram tick_duration;
  Counter ticks;
  Counter ticks_late;
  Counter tick_overruns;
  Counter tick_resyncs;

  // Lobby and Match.
  Gauge lobby_players;
  Gauge match_in_progress;
  Gauge match_players_alive;
  Counter matches_started;
  Counter matches_ended_with_winner;
  Counter matches_ended_drawn;
  /// Ended because its last player left.
  Counter matches_ended_abandoned;
  Histogram match_duration;

  // Sessions.
  Gauge sessions;
  Counter joins_admitted;
  JoinRefusalCounters joins_refused;
  /// Peers whose connection ended before they were admitted to the Lobby,
  /// players who left the Lobby, and who left a match in progress, by how.
  LeavingCounters disconnects_before_admission;
  LeavingCounters disconnects_from_lobby;
  LeavingCounters disconnects_from_match;

  // Misbehaviour.
  MisbehaviourCounters misbehaviour;

  // Network: what the server sends and receives, as payloads.
  Counter sent_bytes;
  Counter received_bytes;
  MessageCounters messages_sent;
  /// Messages that decoded, by type.
  MessageCounters messages_received;
  Histogram authoritative_state_update_bytes;
  /// Every command of every Commands message.
  Counter commands_received;
  /// Commands the command queue turned away, by why; those it took in but
  /// dropped as the oldest of a full queue; those of a player not in a match;
  /// and those of a peer that has not joined. Every command counted received
  /// is either taken in or one of these; one a peer sent after the command
  /// that got it disconnected is neither received nor discarded.
  RejectionCounters commands_rejected;
  Counter commands_overflowed;
  Counter commands_outside_match;
  Counter commands_before_joining;

  // Combat, written by the Simulation thread.
  Counter shots;
  BodyPartCounters hit_confirmations;
  Histogram shooters_delay;
  /// Rounds whose Shooter's delay the cap held (simulation::kMaxShootersDelay).
  Counter shooters_delay_capped;
  /// Bullets still flying after the last tick (simulation::State's bullets_in_flight).
  Gauge bullets_in_flight;

  /// The Match recording's state, written by whichever thread it changes on
  /// (SetRecordingState) and published whole, so a scrape never sees two
  /// states or none; nullopt while nothing is recorded.
  std::atomic<std::optional<RecordingState>> recording_state;
  static_assert(std::atomic<std::optional<RecordingState>>::is_always_lock_free);
};

/// The heartbeat's running totals (heartbeat.h), read from metrics' counters:
/// the heartbeat line counts nothing of its own.
[[nodiscard]] Activity Totals(const HostMetrics& metrics);

/// Publishes state as the Match recording's.
void SetRecordingState(HostMetrics& metrics, RecordingState state);

/// Counts payload, an encoded message the server is sending, by its type and size.
void CountSent(HostMetrics& metrics, std::span<const std::byte> payload);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_METRICS_H_
