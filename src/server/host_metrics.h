#ifndef AUGUSTA_SERVER_HOST_METRICS_H_
#define AUGUSTA_SERVER_HOST_METRICS_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

#include <prometheus/collectable.h>
#include <prometheus/metric_family.h>

#include "augusta/ballistics.h"
#include "command_queue.h"
#include "heartbeat.h"
#include "host_log.h"
#include "match.h"
#include "misbehaviour.h"

/// \file
/// What the server counts about itself, for the metrics endpoint (metrics.h)
/// to expose: ADR-0049's catalogue of the Tick, Lobby and Match, Sessions,
/// Misbehaviour, Network and Combat families, each named and labelled as it says.
/// The Host owns one, and its Network I/O and Simulation threads write each value
/// in place where the event happens; the endpoint's thread only collects. Every
/// counter, gauge and histogram here is a lock-free atomic, so neither thread
/// ever waits on a scrape, as prometheus-cpp's own histogram (behind a mutex)
/// would have it wait. The heartbeat line (heartbeat.h) reads its totals from the
/// same counters (Totals), so the line and the series count each event once.
/// Every label takes its values from a closed set, one of the server's own enums
/// where it has one, never from text a peer sent. The Process family is the endpoint's; Connection health is
/// not counted here.
namespace augusta::server {

// What lets neither the tick nor the Network I/O thread ever wait on a scrape.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free);

/// A count that only grows. Lock-free, from any thread.
class Counter {
 public:
  void Increment(std::uint64_t by = 1) { value_.fetch_add(by, std::memory_order_relaxed); }
  [[nodiscard]] std::uint64_t Value() const { return value_.load(std::memory_order_relaxed); }

 private:
  std::atomic<std::uint64_t> value_{0};
};

/// A value that goes up and down. Lock-free, from any thread.
class Gauge {
 public:
  void Set(double value) { value_.store(value, std::memory_order_relaxed); }
  [[nodiscard]] double Value() const { return value_.load(std::memory_order_relaxed); }

 private:
  std::atomic<double> value_{0.0};
};

/// Observations counted into fixed buckets. Lock-free, from any thread.
class Histogram {
 public:
  /// What a Histogram has counted, as the exposition gives it.
  struct Snapshot {
    /// How many observations each bucket holds: those no greater than its bound,
    /// then every one, for the +Inf bucket.
    std::vector<std::uint64_t> cumulative_counts;
    double sum = 0.0;
  };

  /// bounds are the buckets' upper bounds, ascending; a last bucket, +Inf, holds the rest.
  Histogram(std::initializer_list<double> bounds);

  void Observe(double value);

  /// What it has counted. Taken without stopping the writers, so an observation
  /// in progress may be in the counts and not yet in the sum.
  [[nodiscard]] Snapshot Read() const;

  [[nodiscard]] std::span<const double> Bounds() const { return bounds_; }

 private:
  std::vector<double> bounds_;
  // Per bucket, not cumulative: one atomic increment per observation.
  std::vector<std::atomic<std::uint64_t>> counts_;
  std::atomic<double> sum_{0.0};
};

/// One Counter per value of Key, an enum whose values run from First to Last.
template <typename Key, Key First, Key Last>
class EnumCounters {
 public:
  [[nodiscard]] Counter& operator[](Key key) { return counters_.at(Index(key)); }
  [[nodiscard]] const Counter& operator[](Key key) const { return counters_.at(Index(key)); }

  /// The sum over every value.
  [[nodiscard]] std::uint64_t Total() const {
    std::uint64_t total = 0;
    for (const Counter& counter : counters_) {
      total += counter.Value();
    }
    return total;
  }

  /// Every value of Key, in order.
  [[nodiscard]] static std::vector<Key> Keys() {
    std::vector<Key> keys;
    for (std::size_t i = 0; i < kCount; ++i) {
      keys.push_back(static_cast<Key>(static_cast<std::size_t>(std::to_underlying(First)) + i));
    }
    return keys;
  }

 private:
  static constexpr std::size_t kCount =
      static_cast<std::size_t>(std::to_underlying(Last)) - static_cast<std::size_t>(std::to_underlying(First)) + 1;

  static std::size_t Index(Key key) {
    return static_cast<std::size_t>(std::to_underlying(key)) - static_cast<std::size_t>(std::to_underlying(First));
  }

  std::array<Counter, kCount> counters_;
};

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
/// The misbehaviours only: the first of PeerRejection's values (misbehaviour.h).
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
  /// dropped as the oldest of a full queue; and those of a player not in a match.
  RejectionCounters commands_rejected;
  Counter commands_overflowed;
  Counter commands_outside_match;

  // Combat, written by the Simulation thread.
  Counter shots;
  BodyPartCounters hit_confirmations;
  Histogram shooters_delay;
  /// Rounds whose Shooter's delay the cap held (simulation::kMaxShootersDelay).
  Counter shooters_delay_capped;
};

/// The heartbeat's running totals (heartbeat.h), read from metrics' counters:
/// the heartbeat line counts nothing of its own.
[[nodiscard]] Activity Totals(const HostMetrics& metrics);

/// Counts payload, an encoded message the server is sending, by its type and size.
void CountSent(HostMetrics& metrics, std::span<const std::byte> payload);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_HOST_METRICS_H_
