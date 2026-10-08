#include "host_metrics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
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

// What the Host counts, as the metrics endpoint collects it: the catalogue's
// names, types and labels (ADR-0049), and the heartbeat's totals read from the
// same counters. Counting through the Host is session_test.cpp's.
namespace {

using augusta::server::Activity;
using augusta::server::Histogram;
using augusta::server::HostMetrics;
using augusta::server::JoinRefusal;
using augusta::server::Leaving;
using augusta::server::PeerRejection;
using augusta::server::RecordingState;
using augusta::server::Rejection;
using augusta::server::Totals;
using prometheus::ClientMetric;
using prometheus::MetricFamily;
using prometheus::MetricType;

constexpr std::uint8_t kTickRate = 60;

// The family named name, which a test fails without.
const MetricFamily& Family(const std::vector<MetricFamily>& families, const std::string& name) {
  const auto found = std::ranges::find(families, name, &MetricFamily::name);
  if (found == families.end()) {
    ADD_FAILURE() << "no family " << name;
    static const MetricFamily kNone;
    return kNone;
  }
  return *found;
}

using Labels = std::map<std::string, std::string>;

Labels LabelsOf(const ClientMetric& metric) {
  Labels labels;
  for (const ClientMetric::Label& label : metric.label) {
    labels.emplace(label.name, label.value);
  }
  return labels;
}

// The series of family labelled exactly labels, which a test fails without.
const ClientMetric& Series(const MetricFamily& family, const Labels& labels) {
  const auto found = std::ranges::find_if(family.metric, [&](const ClientMetric& m) { return LabelsOf(m) == labels; });
  if (found == family.metric.end()) {
    ADD_FAILURE() << "no series of " << family.name << " with those labels";
    static const ClientMetric kNone;
    return kNone;
  }
  return *found;
}

// The values family's series take for the label name.
std::set<std::string> ValuesOf(const MetricFamily& family, const std::string& name) {
  std::set<std::string> values;
  for (const ClientMetric& metric : family.metric) {
    if (const auto labels = LabelsOf(metric); labels.contains(name)) {
      values.insert(labels.at(name));
    }
  }
  return values;
}

// Requirements: NFR-07
TEST(HostMetricsTest, EveryMetricOfTheCatalogueIsCollectedWithItsType) {
  const HostMetrics metrics(kTickRate);
  const std::vector<MetricFamily> families = metrics.Collect();

  const std::vector<std::pair<std::string, MetricType>> catalogue = {
      {"augustad_tick_duration_seconds", MetricType::Histogram},
      {"augustad_ticks_total", MetricType::Counter},
      {"augustad_ticks_late_total", MetricType::Counter},
      {"augustad_tick_overruns_total", MetricType::Counter},
      {"augustad_tick_resyncs_total", MetricType::Counter},
      {"augustad_tick_rate_hertz", MetricType::Gauge},
      {"augustad_lobby_players", MetricType::Gauge},
      {"augustad_match_in_progress", MetricType::Gauge},
      {"augustad_match_players_alive", MetricType::Gauge},
      {"augustad_matches_started_total", MetricType::Counter},
      {"augustad_matches_ended_total", MetricType::Counter},
      {"augustad_match_duration_seconds", MetricType::Histogram},
      {"augustad_sessions", MetricType::Gauge},
      {"augustad_joins_total", MetricType::Counter},
      {"augustad_disconnects_total", MetricType::Counter},
      {"augustad_misbehaviour_total", MetricType::Counter},
      {"augustad_network_sent_bytes_total", MetricType::Counter},
      {"augustad_network_received_bytes_total", MetricType::Counter},
      {"augustad_messages_sent_total", MetricType::Counter},
      {"augustad_messages_received_total", MetricType::Counter},
      {"augustad_authoritative_state_update_bytes", MetricType::Histogram},
      {"augustad_commands_received_total", MetricType::Counter},
      {"augustad_commands_discarded_total", MetricType::Counter},
      {"augustad_shots_total", MetricType::Counter},
      {"augustad_hit_confirmations_total", MetricType::Counter},
      {"augustad_shooters_delay_seconds", MetricType::Histogram},
      {"augustad_shooters_delay_capped_total", MetricType::Counter},
      {"augustad_bullets_in_flight", MetricType::Gauge},
      {"augustad_recording_state", MetricType::Gauge},
  };
  for (const auto& [name, type] : catalogue) {
    EXPECT_EQ(Family(families, name).type, type) << name;
    EXPECT_FALSE(Family(families, name).help.empty()) << name;
  }
  EXPECT_EQ(families.size(), catalogue.size());
}

// Requirements: NFR-07
// The value of each augustad_recording_state series, by its state label.
std::map<std::string, double> RecordingStates(const HostMetrics& metrics) {
  std::map<std::string, double> states;
  for (const ClientMetric& series : Family(metrics.Collect(), "augustad_recording_state").metric) {
    states.emplace(LabelsOf(series).at("state"), series.gauge.value);
  }
  return states;
}

TEST(HostMetricsTest, NoRecordingStateIsSetWhileNothingIsRecorded) {
  const HostMetrics metrics(kTickRate);
  EXPECT_EQ(RecordingStates(metrics),
            (std::map<std::string, double>{{"enabled", 0.0}, {"degraded", 0.0}, {"stopped", 0.0}}));
}

TEST(HostMetricsTest, OnlyTheRecordingsCurrentStateIsSet) {
  HostMetrics metrics(kTickRate);
  augusta::server::SetRecordingState(metrics, RecordingState::kEnabled);
  augusta::server::SetRecordingState(metrics, RecordingState::kDegraded);
  EXPECT_EQ(RecordingStates(metrics),
            (std::map<std::string, double>{{"enabled", 0.0}, {"degraded", 1.0}, {"stopped", 0.0}}));
}

TEST(HostMetricsTest, TheTickRateIsTheConfiguredOne) {
  const HostMetrics metrics(kTickRate);

  const std::vector<MetricFamily> families = metrics.Collect();
  EXPECT_EQ(Series(Family(families, "augustad_tick_rate_hertz"), {}).gauge.value, kTickRate);
}

// Requirements: NFR-07
TEST(HostMetricsTest, TheTickDurationHasTheCataloguesBucketsInSeconds) {
  const HostMetrics metrics(kTickRate);

  const std::vector<MetricFamily> families = metrics.Collect();
  const ClientMetric& duration = Series(Family(families, "augustad_tick_duration_seconds"), {});

  std::vector<double> bounds;
  for (const ClientMetric::Bucket& bucket : duration.histogram.bucket) {
    bounds.push_back(bucket.upper_bound);
  }
  ASSERT_EQ(bounds.size(), 11U);
  const std::vector<double> expected = {0.001, 0.002, 0.004, 0.008, 0.012, 0.0167, 0.020, 0.033, 0.050, 0.100};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_DOUBLE_EQ(bounds[i], expected[i]) << i;
  }
}

// Requirements: NFR-07
TEST(HostMetricsTest, AHistogramCountsEachObservationInEveryBucketThatHoldsIt) {
  Histogram histogram({1.0, 2.0, 4.0});

  histogram.Observe(0.5);
  histogram.Observe(2.0);
  histogram.Observe(3.0);
  histogram.Observe(9.0);
  const Histogram::Snapshot snapshot = histogram.Read();

  EXPECT_EQ(snapshot.cumulative_counts, (std::vector<std::uint64_t>{1, 2, 3, 4}));
  EXPECT_DOUBLE_EQ(snapshot.sum, 14.5);
}

// Requirements: NFR-07
TEST(HostMetricsTest, JoinsAreLabelledByResultAndARefusalByItsReason) {
  HostMetrics metrics(kTickRate);
  metrics.joins_admitted.Increment();
  metrics.joins_refused[JoinRefusal::kLobbyFull].Increment();

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& joins = Family(families, "augustad_joins_total");

  EXPECT_EQ(Series(joins, {{"result", "admitted"}}).counter.value, 1.0);
  EXPECT_EQ(Series(joins, {{"result", "refused"}, {"reason", "lobby_full"}}).counter.value, 1.0);
  EXPECT_EQ(ValuesOf(joins, "reason"), (std::set<std::string>{"version_mismatch", "lobby_full", "unknown_character",
                                                              "match_in_progress", "pack_mismatch"}));
}

// Requirements: NFR-07
TEST(HostMetricsTest, DisconnectsAreLabelledByHowThePlayerLeftAndFromWhere) {
  HostMetrics metrics(kTickRate);
  metrics.disconnects_from_match[Leaving::kTimedOut].Increment();

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& disconnects = Family(families, "augustad_disconnects_total");

  EXPECT_EQ(Series(disconnects, {{"reason", "timeout"}, {"phase", "match"}}).counter.value, 1.0);
  EXPECT_EQ(Series(disconnects, {{"reason", "timeout"}, {"phase", "lobby"}}).counter.value, 0.0);
  EXPECT_EQ(ValuesOf(disconnects, "reason"), (std::set<std::string>{"left", "timeout", "misbehaving"}));
  EXPECT_EQ(ValuesOf(disconnects, "phase"), (std::set<std::string>{"admission", "lobby", "match"}));
}

// Requirements: NFR-07
TEST(HostMetricsTest, MisbehaviourIsLabelledByItsKindAndOnlyMisbehaviourHasOne) {
  HostMetrics metrics(kTickRate);
  metrics.misbehaviour[PeerRejection::kUndecodable].Increment(3);

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& misbehaviour = Family(families, "augustad_misbehaviour_total");

  EXPECT_EQ(Series(misbehaviour, {{"kind", "undecodable"}}).counter.value, 3.0);
  EXPECT_EQ(ValuesOf(misbehaviour, "kind"),
            (std::set<std::string>{"undecodable", "not_a_client_message", "non_finite_command", "out_of_range_command",
                                   "commands_before_joining"}));
}

// Requirements: NFR-07
TEST(HostMetricsTest, DiscardedCommandsAreLabelledByWhyTheyWereTurnedAway) {
  HostMetrics metrics(kTickRate);
  metrics.commands_rejected[Rejection::kOutOfRange].Increment();
  metrics.commands_overflowed.Increment(2);

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& discarded = Family(families, "augustad_commands_discarded_total");

  EXPECT_EQ(Series(discarded, {{"reason", "out_of_range"}}).counter.value, 1.0);
  EXPECT_EQ(Series(discarded, {{"reason", "overflow"}}).counter.value, 2.0);
  EXPECT_EQ(ValuesOf(discarded, "reason"), (std::set<std::string>{"stale", "non_finite", "out_of_range", "overflow",
                                                                  "outside_match", "before_joining"}));
}

// Requirements: NFR-07
TEST(HostMetricsTest, MessagesAreLabelledByTheirType) {
  HostMetrics metrics(kTickRate);
  metrics.messages_sent[augusta::server::MessageType::kAuthoritativeState].Increment();

  const std::vector<MetricFamily> families = metrics.Collect();

  EXPECT_EQ(Series(Family(families, "augustad_messages_sent_total"), {{"type", "authoritative_state"}}).counter.value,
            1.0);
  const std::set<std::string> types = {
      "join_request", "join_accepted", "join_refused", "commands", "authoritative_state", "lobby",
      "ready",        "match_start",   "match_end",    "shot",     "hit_confirmation",    "death"};
  EXPECT_EQ(ValuesOf(Family(families, "augustad_messages_sent_total"), "type"), types);
  EXPECT_EQ(ValuesOf(Family(families, "augustad_messages_received_total"), "type"), types);
}

// Requirements: NFR-07
TEST(HostMetricsTest, MatchesEndedAreLabelledByOutcome) {
  HostMetrics metrics(kTickRate);
  metrics.matches_ended_with_winner.Increment();

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& ended = Family(families, "augustad_matches_ended_total");

  EXPECT_EQ(Series(ended, {{"outcome", "winner"}}).counter.value, 1.0);
  EXPECT_EQ(ValuesOf(ended, "outcome"), (std::set<std::string>{"winner", "draw", "abandoned"}));
}

// Requirements: NFR-07
TEST(HostMetricsTest, HitConfirmationsAreLabelledByBodyPart) {
  HostMetrics metrics(kTickRate);
  metrics.hit_confirmations[augusta::ballistics::BodyPart::kHead].Increment();

  const std::vector<MetricFamily> families = metrics.Collect();
  const MetricFamily& hits = Family(families, "augustad_hit_confirmations_total");

  EXPECT_EQ(Series(hits, {{"body_part", "head"}}).counter.value, 1.0);
  EXPECT_EQ(ValuesOf(hits, "body_part"), (std::set<std::string>{"head", "torso", "limb"}));
}

// The heartbeat line and the series read one set of counters.
// Requirements: NFR-07
TEST(HostMetricsTest, TheHeartbeatsTotalsAreReadFromTheSameCounters) {
  HostMetrics metrics(kTickRate);
  metrics.ticks.Increment(5);
  metrics.ticks_late.Increment(2);
  metrics.tick_overruns.Increment(1);
  metrics.messages_received[augusta::server::MessageType::kCommands].Increment(3);
  metrics.messages_received[augusta::server::MessageType::kReady].Increment(1);
  metrics.commands_rejected[Rejection::kStale].Increment(4);
  metrics.commands_outside_match.Increment(2);
  metrics.commands_rejected[Rejection::kNonFinite].Increment(1);
  metrics.misbehaviour[PeerRejection::kNonFiniteCommand].Increment(1);
  metrics.misbehaviour[PeerRejection::kUndecodable].Increment(6);
  metrics.commands_overflowed.Increment(7);
  metrics.disconnects_before_admission[Leaving::kMisbehaving].Increment(1);
  metrics.disconnects_from_lobby[Leaving::kMisbehaving].Increment(1);
  metrics.disconnects_from_match[Leaving::kMisbehaving].Increment(1);
  metrics.disconnects_from_match[Leaving::kLeft].Increment(1);

  const Activity totals = Totals(metrics);

  EXPECT_EQ(totals.ticks, 5U);
  EXPECT_EQ(totals.late, 2U);
  EXPECT_EQ(totals.overrun, 1U);
  EXPECT_EQ(totals.messages, 4U);
  EXPECT_EQ(totals.stale, 6U);
  EXPECT_EQ(totals.dropped, 7U);
  EXPECT_EQ(totals.overflow, 7U);
  EXPECT_EQ(totals.misbehaving, 3U);
}

}  // namespace
