#include "connection_health.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>
#include <prometheus/registry.h>

#include "augusta/networking.h"
#include "match.h"

// What ConnectionHealth records, read back from the registry as the metrics
// endpoint serves it.
namespace {

using augusta::networking::ConnectionStats;
using augusta::server::ConnectionHealth;
using augusta::server::ConnectionSample;
using augusta::server::SessionId;

constexpr SessionId kSession{7};
constexpr SessionId kOtherSession{8};

// A connection the transport has fully measured.
ConnectionStats Measured() {
  return ConnectionStats{.ping_ms = 40,
                         .quality_local = 0.99F,
                         .quality_remote = 0.98F,
                         .in_bytes_per_sec = 1200.0F,
                         .out_bytes_per_sec = 9600.0F,
                         .max_jitter_us = 2000,
                         .pending_bytes = 300};
}

// What the transport reports right after connecting.
ConnectionStats NotMeasuredYet() {
  ConnectionStats stats = Measured();
  stats.quality_local = -1.0F;
  stats.quality_remote = -1.0F;
  stats.max_jitter_us = -1;
  return stats;
}

bool HasLabel(const prometheus::ClientMetric& metric, const std::string& name, const std::string& value) {
  return std::ranges::any_of(metric.label, [&](const prometheus::ClientMetric::Label& label) {
    return label.name == name && label.value == value;
  });
}

class ConnectionHealthTest : public ::testing::Test {
 protected:
  // The metric of family name whose labels include name=value, if any.
  std::optional<prometheus::ClientMetric> Find(const std::string& family, const std::string& label = "",
                                               const std::string& value = "") const {
    for (const prometheus::MetricFamily& collected : registry_.Collect()) {
      if (collected.name != family) {
        continue;
      }
      for (const prometheus::ClientMetric& metric : collected.metric) {
        if (label.empty() || HasLabel(metric, label, value)) {
          return metric;
        }
      }
    }
    return std::nullopt;
  }

  std::optional<double> Gauge(const std::string& family, SessionId session, const std::string& direction = "") const {
    for (const prometheus::MetricFamily& collected : registry_.Collect()) {
      if (collected.name != family) {
        continue;
      }
      for (const prometheus::ClientMetric& metric : collected.metric) {
        if (HasLabel(metric, "session_id", std::to_string(static_cast<std::uint32_t>(session))) &&
            (direction.empty() || HasLabel(metric, "direction", direction))) {
          return metric.gauge.value;
        }
      }
    }
    return std::nullopt;
  }

  std::uint64_t HistogramCount(const std::string& family, const std::string& direction = "") const {
    const auto metric = direction.empty() ? Find(family) : Find(family, "direction", direction);
    return metric.has_value() ? metric->histogram.sample_count : 0;
  }

  // Every metric labelled with session's ID, in any family.
  int GaugesOf(SessionId session) const {
    int count = 0;
    for (const prometheus::MetricFamily& collected : registry_.Collect()) {
      for (const prometheus::ClientMetric& metric : collected.metric) {
        count += HasLabel(metric, "session_id", std::to_string(static_cast<std::uint32_t>(session))) ? 1 : 0;
      }
    }
    return count;
  }

  prometheus::Registry registry_;
  ConnectionHealth health_{registry_};
};

TEST_F(ConnectionHealthTest, RecordsASessionsMeasurementsInBaseUnits) {
  health_.Record({{.session = kSession, .stats = Measured()}});

  EXPECT_DOUBLE_EQ(Gauge("augustad_session_connection_rtt_seconds", kSession).value_or(-1), 0.040);
  EXPECT_NEAR(Gauge("augustad_session_connection_quality_ratio", kSession, "local").value_or(-1), 0.99, 1e-6);
  EXPECT_NEAR(Gauge("augustad_session_connection_quality_ratio", kSession, "remote").value_or(-1), 0.98, 1e-6);
  EXPECT_DOUBLE_EQ(Gauge("augustad_session_connection_jitter_seconds", kSession).value_or(-1), 0.002);
  EXPECT_DOUBLE_EQ(Gauge("augustad_connection_in_bytes_per_second", kSession).value_or(-1), 1200.0);
  EXPECT_DOUBLE_EQ(Gauge("augustad_connection_out_bytes_per_second", kSession).value_or(-1), 9600.0);
  EXPECT_DOUBLE_EQ(Gauge("augustad_connection_pending_bytes", kSession).value_or(-1), 300.0);
}

TEST_F(ConnectionHealthTest, CountsEveryConnectionInTheHistograms) {
  health_.Record({{.session = kSession, .stats = Measured()}, {.session = std::nullopt, .stats = Measured()}});

  EXPECT_EQ(HistogramCount("augustad_connection_rtt_seconds"), 2U);
  EXPECT_EQ(HistogramCount("augustad_connection_quality_ratio", "local"), 2U);
  EXPECT_EQ(HistogramCount("augustad_connection_quality_ratio", "remote"), 2U);
  EXPECT_EQ(HistogramCount("augustad_connection_jitter_seconds"), 2U);
  const auto rtt = Find("augustad_connection_rtt_seconds");
  ASSERT_TRUE(rtt.has_value());
  EXPECT_DOUBLE_EQ(rtt->histogram.sample_sum, 0.080);
}

TEST_F(ConnectionHealthTest, AConnectionWithoutASessionHasNoGauges) {
  health_.Record({{.session = std::nullopt, .stats = Measured()}});

  EXPECT_FALSE(Find("augustad_connection_pending_bytes").has_value());
  EXPECT_FALSE(Find("augustad_session_connection_rtt_seconds").has_value());
}

TEST_F(ConnectionHealthTest, DoesNotRecordWhatIsNotMeasuredYet) {
  health_.Record({{.session = kSession, .stats = NotMeasuredYet()}});

  EXPECT_EQ(HistogramCount("augustad_connection_quality_ratio", "local"), 0U);
  EXPECT_EQ(HistogramCount("augustad_connection_quality_ratio", "remote"), 0U);
  EXPECT_EQ(HistogramCount("augustad_connection_jitter_seconds"), 0U);
  EXPECT_FALSE(Gauge("augustad_session_connection_quality_ratio", kSession, "local").has_value());
  EXPECT_FALSE(Gauge("augustad_session_connection_quality_ratio", kSession, "remote").has_value());
  EXPECT_FALSE(Gauge("augustad_session_connection_jitter_seconds", kSession).has_value());
  // What is measured from the start still is.
  EXPECT_EQ(HistogramCount("augustad_connection_rtt_seconds"), 1U);
  EXPECT_TRUE(Gauge("augustad_session_connection_rtt_seconds", kSession).has_value());
}

TEST_F(ConnectionHealthTest, AGaugeKeepsItsLastMeasurementWhileTheNextIsNotMeasured) {
  health_.Record({{.session = kSession, .stats = Measured()}});
  health_.Record({{.session = kSession, .stats = NotMeasuredYet()}});

  EXPECT_DOUBLE_EQ(Gauge("augustad_session_connection_jitter_seconds", kSession).value_or(-1), 0.002);
  EXPECT_EQ(HistogramCount("augustad_connection_jitter_seconds"), 1U);
}

TEST_F(ConnectionHealthTest, NoGaugeOutlivesItsSession) {
  health_.Record({{.session = kSession, .stats = Measured()}, {.session = kOtherSession, .stats = Measured()}});
  ASSERT_EQ(GaugesOf(kSession), 7);

  health_.Record({{.session = kOtherSession, .stats = Measured()}});

  EXPECT_EQ(GaugesOf(kSession), 0);
  EXPECT_EQ(GaugesOf(kOtherSession), 7);
  // The histograms keep what the ended Session contributed.
  EXPECT_EQ(HistogramCount("augustad_connection_rtt_seconds"), 3U);
}

TEST_F(ConnectionHealthTest, LabelsAreOnlySessionIdAndDirection) {
  health_.Record({{.session = kSession, .stats = Measured()}, {.session = std::nullopt, .stats = Measured()}});

  for (const prometheus::MetricFamily& collected : registry_.Collect()) {
    for (const prometheus::ClientMetric& metric : collected.metric) {
      for (const prometheus::ClientMetric::Label& label : metric.label) {
        EXPECT_TRUE(label.name == "session_id" || label.name == "direction")
            << collected.name << " is labelled " << label.name;
      }
    }
  }
}

}  // namespace
