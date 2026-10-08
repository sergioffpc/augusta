#include "host_metrics.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>
#include <prometheus/metric_type.h>

#include "augusta/ballistics.h"
#include "command_queue.h"
#include "heartbeat.h"
#include "host_log.h"
#include "lock_free_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "recording.h"
#include "wire.h"

namespace augusta::server {

namespace {

using prometheus::ClientMetric;
using prometheus::MetricFamily;
using prometheus::MetricType;
using Labels = std::vector<ClientMetric::Label>;

// The catalogue's tick buckets, 1 to 100 ms: 16.7 ms is a tick at 60 Hz.
constexpr std::initializer_list<double> kTickBuckets = {0.001,  0.002, 0.004, 0.008, 0.012,
                                                        0.0167, 0.020, 0.033, 0.050, 0.100};
// From a short match to an hour.
constexpr std::initializer_list<double> kMatchBuckets = {30, 60, 120, 180, 300, 600, 900, 1200, 1800, 3600};
// Around a datagram's size, up to a few.
constexpr std::initializer_list<double> kUpdateBuckets = {64, 128, 256, 512, 1024, 1200, 1500, 2048, 4096};
// Up to the 250 ms cap (simulation::kMaxShootersDelay), which a capped round falls in.
constexpr std::initializer_list<double> kShootersDelayBuckets = {0.025, 0.050, 0.075, 0.100,
                                                                 0.125, 0.150, 0.200, 0.250};

// Each label below is empty for a value no counter has, which
// LabelledExactly checks.

constexpr std::string_view JoinRefusalLabel(JoinRefusal reason) {
  switch (reason) {
    case JoinRefusal::kVersionMismatch:
      return "version_mismatch";
    case JoinRefusal::kLobbyFull:
      return "lobby_full";
    case JoinRefusal::kUnknownCharacter:
      return "unknown_character";
    case JoinRefusal::kMatchInProgress:
      return "match_in_progress";
    case JoinRefusal::kPackMismatch:
      return "pack_mismatch";
  }
  return {};
}

constexpr std::string_view LeavingLabel(Leaving how) {
  switch (how) {
    case Leaving::kLeft:
      return "left";
    case Leaving::kTimedOut:
      return "timeout";
    case Leaving::kMisbehaving:
      return "misbehaving";
  }
  return {};
}

constexpr std::string_view MisbehaviourLabel(PeerRejection kind) {
  switch (kind) {
    case PeerRejection::kUndecodable:
      return "undecodable";
    case PeerRejection::kNotAClientMessage:
      return "not_a_client_message";
    case PeerRejection::kNonFiniteCommand:
      return "non_finite_command";
    case PeerRejection::kOutOfRangeCommand:
      return "out_of_range_command";
    case PeerRejection::kCommandsBeforeJoining:
      return "commands_before_joining";
    case PeerRejection::kStaleCommand:
    case PeerRejection::kCommandsOutsideMatch:
    case PeerRejection::kStaleReady:
    case PeerRejection::kJoinRefused:
      // Routine: not misbehaviour, so no counter has them.
      break;
  }
  return {};
}

constexpr std::string_view MessageTypeLabel(MessageType type) {
  switch (type) {
    case MessageType::kJoinRequest:
      return "join_request";
    case MessageType::kJoinAccepted:
      return "join_accepted";
    case MessageType::kJoinRefused:
      return "join_refused";
    case MessageType::kCommands:
      return "commands";
    case MessageType::kAuthoritativeState:
      return "authoritative_state";
    case MessageType::kLobby:
      return "lobby";
    case MessageType::kReady:
      return "ready";
    case MessageType::kMatchStart:
      return "match_start";
    case MessageType::kMatchEnd:
      return "match_end";
    case MessageType::kShot:
      return "shot";
    case MessageType::kHitConfirmation:
      return "hit_confirmation";
    case MessageType::kDeath:
      return "death";
  }
  return {};
}

constexpr std::string_view RejectionLabel(Rejection rejection) {
  switch (rejection) {
    case Rejection::kStale:
      return "stale";
    case Rejection::kNonFinite:
      return "non_finite";
    case Rejection::kOutOfRange:
      return "out_of_range";
  }
  return {};
}

constexpr std::string_view BodyPartLabel(ballistics::BodyPart part) {
  switch (part) {
    case ballistics::BodyPart::kHead:
      return "head";
    case ballistics::BodyPart::kTorso:
      return "torso";
    case ballistics::BodyPart::kLimb:
      return "limb";
  }
  return {};
}

// Each range of counters is exactly the values its label names: an enum value
// added, or a reorder, fails here rather than indexing past a range.
static_assert(LabelledExactly<JoinRefusalCounters>(JoinRefusalLabel));
static_assert(LabelledExactly<LeavingCounters>(LeavingLabel));
static_assert(LabelledExactly<MisbehaviourCounters>(MisbehaviourLabel));
static_assert(LabelledExactly<MessageCounters>(MessageTypeLabel));
static_assert(LabelledExactly<RejectionCounters>(RejectionLabel));
static_assert(LabelledExactly<BodyPartCounters>(BodyPartLabel));

// What Judge counts as misbehaviour is what MisbehaviourCounters has a counter for.
consteval bool MisbehaviourIsLabelled() {
  for (unsigned value = 0; value <= std::numeric_limits<std::uint8_t>::max(); ++value) {
    const auto rejection = static_cast<PeerRejection>(value);
    if (IsMisbehaviour(rejection) == MisbehaviourLabel(rejection).empty()) {
      return false;
    }
  }
  return true;
}
static_assert(MisbehaviourIsLabelled());

MetricFamily Family(std::string name, std::string help, MetricType type, std::vector<ClientMetric> series) {
  return MetricFamily{.name = std::move(name), .help = std::move(help), .type = type, .metric = std::move(series)};
}

MetricFamily CounterFamily(std::string name, std::string help, const Counter& counter) {
  return Family(std::move(name), std::move(help), MetricType::Counter, {CounterSeries(counter)});
}

MetricFamily GaugeFamily(std::string name, std::string help, const Gauge& gauge) {
  return Family(std::move(name), std::move(help), MetricType::Gauge, {GaugeSeries(gauge)});
}

MetricFamily HistogramFamily(std::string name, std::string help, const Histogram& histogram) {
  return Family(std::move(name), std::move(help), MetricType::Histogram, {HistogramSeries(histogram)});
}

// One series of counters per value of its key, labelled name="label(key)" and,
// first, with fixed.
template <typename Counters, typename LabelOf>
void AppendLabelled(std::vector<ClientMetric>& series, const Counters& counters, const std::string& name, LabelOf label,
                    const Labels& fixed = {}) {
  for (const auto key : Counters::Keys()) {
    Labels labels = fixed;
    labels.push_back({.name = name, .value = std::string(label(key))});
    series.push_back(CounterSeries(counters[key], std::move(labels)));
  }
}

template <typename Counters, typename LabelOf>
MetricFamily LabelledFamily(std::string name, std::string help, const Counters& counters, const std::string& label,
                            LabelOf label_of) {
  std::vector<ClientMetric> series;
  AppendLabelled(series, counters, label, label_of);
  return Family(std::move(name), std::move(help), MetricType::Counter, std::move(series));
}

void AppendTick(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  families.push_back(
      HistogramFamily("augustad_tick_duration_seconds", "How long each tick's work took.", metrics.tick_duration));
  families.push_back(CounterFamily("augustad_ticks_total", "Ticks run.", metrics.ticks));
  families.push_back(CounterFamily("augustad_ticks_late_total",
                                   "Ticks that started more than 1 ms after their deadline.", metrics.ticks_late));
  families.push_back(CounterFamily("augustad_tick_overruns_total", "Ticks whose work took longer than a tick.",
                                   metrics.tick_overruns));
  families.push_back(CounterFamily("augustad_tick_resyncs_total",
                                   "Times the tick loop fell too far behind and resynchronised to now.",
                                   metrics.tick_resyncs));
  ClientMetric tick_rate;
  tick_rate.gauge.value = metrics.tick_rate_hz;
  families.push_back(Family("augustad_tick_rate_hertz", "The configured tick rate.", MetricType::Gauge, {tick_rate}));
}

void AppendLobbyAndMatch(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  families.push_back(GaugeFamily("augustad_lobby_players", "Players in the Lobby: 0 while a match is in progress.",
                                 metrics.lobby_players));
  families.push_back(
      GaugeFamily("augustad_match_in_progress", "1 while a match is in progress, else 0.", metrics.match_in_progress));
  families.push_back(GaugeFamily("augustad_match_players_alive", "Players of the match in progress still alive.",
                                 metrics.match_players_alive));
  families.push_back(CounterFamily("augustad_matches_started_total", "Matches started.", metrics.matches_started));
  families.push_back(
      Family("augustad_matches_ended_total", "Matches ended, by outcome.", MetricType::Counter,
             {CounterSeries(metrics.matches_ended_with_winner, {{.name = "outcome", .value = "winner"}}),
              CounterSeries(metrics.matches_ended_drawn, {{.name = "outcome", .value = "draw"}}),
              CounterSeries(metrics.matches_ended_abandoned, {{.name = "outcome", .value = "abandoned"}})}));
  families.push_back(
      HistogramFamily("augustad_match_duration_seconds", "How long each match lasted.", metrics.match_duration));
}

void AppendSessions(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  families.push_back(GaugeFamily("augustad_sessions", "Players joined, in the Lobby or the match.", metrics.sessions));
  std::vector<ClientMetric> joins = {CounterSeries(metrics.joins_admitted, {{.name = "result", .value = "admitted"}})};
  AppendLabelled(joins, metrics.joins_refused, "reason", JoinRefusalLabel, {{.name = "result", .value = "refused"}});
  families.push_back(Family("augustad_joins_total", "Joins, by result and a refusal's reason.", MetricType::Counter,
                            std::move(joins)));
  std::vector<ClientMetric> disconnects;
  AppendLabelled(disconnects, metrics.disconnects_before_admission, "reason", LeavingLabel,
                 {{.name = "phase", .value = "admission"}});
  AppendLabelled(disconnects, metrics.disconnects_from_lobby, "reason", LeavingLabel,
                 {{.name = "phase", .value = "lobby"}});
  AppendLabelled(disconnects, metrics.disconnects_from_match, "reason", LeavingLabel,
                 {{.name = "phase", .value = "match"}});
  families.push_back(Family("augustad_disconnects_total", "Players who left, by how and from where.",
                            MetricType::Counter, std::move(disconnects)));
}

void AppendNetwork(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  families.push_back(
      CounterFamily("augustad_network_sent_bytes_total", "Bytes of the messages sent.", metrics.sent_bytes));
  families.push_back(CounterFamily("augustad_network_received_bytes_total", "Bytes of the payloads received.",
                                   metrics.received_bytes));
  families.push_back(LabelledFamily("augustad_messages_sent_total", "Messages sent, by type.", metrics.messages_sent,
                                    "type", MessageTypeLabel));
  families.push_back(LabelledFamily("augustad_messages_received_total", "Messages received that decoded, by type.",
                                    metrics.messages_received, "type", MessageTypeLabel));
  families.push_back(HistogramFamily("augustad_authoritative_state_update_bytes",
                                     "The size of each Authoritative State update sent.",
                                     metrics.authoritative_state_update_bytes));
  families.push_back(
      CounterFamily("augustad_commands_received_total", "Commands received.", metrics.commands_received));
  std::vector<ClientMetric> discarded;
  AppendLabelled(discarded, metrics.commands_rejected, "reason", RejectionLabel);
  discarded.push_back(CounterSeries(metrics.commands_overflowed, {{.name = "reason", .value = "overflow"}}));
  discarded.push_back(CounterSeries(metrics.commands_outside_match, {{.name = "reason", .value = "outside_match"}}));
  discarded.push_back(CounterSeries(metrics.commands_before_joining, {{.name = "reason", .value = "before_joining"}}));
  families.push_back(Family("augustad_commands_discarded_total", "Commands discarded, by why.", MetricType::Counter,
                            std::move(discarded)));
}

void AppendCombat(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  families.push_back(CounterFamily("augustad_shots_total", "Rounds fired.", metrics.shots));
  families.push_back(LabelledFamily("augustad_hit_confirmations_total", "Hits on players, by body part.",
                                    metrics.hit_confirmations, "body_part", BodyPartLabel));
  families.push_back(HistogramFamily("augustad_shooters_delay_seconds", "The Shooter's delay of each round fired.",
                                     metrics.shooters_delay));
  families.push_back(CounterFamily("augustad_shooters_delay_capped_total",
                                   "Rounds whose Shooter's delay was held at the 250 ms cap.",
                                   metrics.shooters_delay_capped));
  families.push_back(GaugeFamily("augustad_bullets_in_flight", "Bullets still flying after the last tick.",
                                 metrics.bullets_in_flight));
}

void AppendRecording(std::vector<MetricFamily>& families, const HostMetrics& metrics) {
  const std::optional<RecordingState> current = metrics.recording_state.load(std::memory_order_relaxed);
  std::vector<ClientMetric> states;
  for (const RecordingState state : {RecordingState::kEnabled, RecordingState::kDegraded, RecordingState::kStopped}) {
    ClientMetric series;
    series.label = {{.name = "state", .value = std::string(RecordingStateName(state))}};
    series.gauge.value = current == state ? 1.0 : 0.0;
    states.push_back(std::move(series));
  }
  families.push_back(Family("augustad_recording_state",
                            "1 for the Match recording's state, 0 for the others; 0 for all while none is recorded.",
                            MetricType::Gauge, std::move(states)));
}

}  // namespace

HostMetrics::HostMetrics(std::uint8_t tick_rate_hz)
    : tick_rate_hz(tick_rate_hz),
      tick_duration(kTickBuckets),
      match_duration(kMatchBuckets),
      authoritative_state_update_bytes(kUpdateBuckets),
      shooters_delay(kShootersDelayBuckets) {}

std::vector<prometheus::MetricFamily> HostMetrics::Collect() const {
  std::vector<MetricFamily> families;
  AppendTick(families, *this);
  AppendLobbyAndMatch(families, *this);
  AppendSessions(families, *this);
  families.push_back(LabelledFamily("augustad_misbehaviour_total", "Misbehaviours of peers, by kind.", misbehaviour,
                                    "kind", MisbehaviourLabel));
  AppendNetwork(families, *this);
  AppendCombat(families, *this);
  AppendRecording(families, *this);
  return families;
}

Activity Totals(const HostMetrics& metrics) {
  return Activity{
      .ticks = metrics.ticks.Value(),
      .late = metrics.ticks_late.Value(),
      .overrun = metrics.tick_overruns.Value(),
      .messages = metrics.messages_received.Total(),
      .stale = metrics.commands_rejected[Rejection::kStale].Value() + metrics.commands_outside_match.Value(),
      .dropped = metrics.misbehaviour.Total(),
      .overflow = metrics.commands_overflowed.Value(),
      .misbehaving = metrics.disconnects_before_admission[Leaving::kMisbehaving].Value() +
                     metrics.disconnects_from_lobby[Leaving::kMisbehaving].Value() +
                     metrics.disconnects_from_match[Leaving::kMisbehaving].Value(),
  };
}

void SetRecordingState(HostMetrics& metrics, RecordingState state) {
  metrics.recording_state.store(state, std::memory_order_relaxed);
}

void CountSent(HostMetrics& metrics, std::span<const std::byte> payload) {
  metrics.sent_bytes.Increment(payload.size());
  metrics.messages_sent[TypeOf(payload)].Increment();
}

}  // namespace augusta::server
