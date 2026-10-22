#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>

#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/link_types.h"

// The server's metrics of a Development build's messages (ADR-0049,
// ADR-0074), as host_metrics_test.cpp has the rest's.
namespace {

using augusta::host::HostMetrics;
using augusta::host::MessageType;
using prometheus::ClientMetric;
using prometheus::MetricFamily;

constexpr std::uint8_t kTickRate = 60;

// The value of family's series whose type label is type, or -1 if it has none.
double CountOf(const std::vector<MetricFamily>& families, const char* family, const char* type) {
  const auto found = std::ranges::find(families, family, &MetricFamily::name);
  if (found == families.end()) {
    return -1.0;
  }
  for (const ClientMetric& series : found->metric) {
    if (std::ranges::any_of(series.label, [type](const ClientMetric::Label& label) {
          return label.name == "type" && label.value == type;
        })) {
      return series.counter.value;
    }
  }
  return -1.0;
}

// Requirements: NFR-07
TEST(HostMetricsDebugTest, ADebugSubscribeIsCountedUnderItsOwnType) {
  HostMetrics metrics(kTickRate);
  metrics.messages_received[MessageType::kDebugSubscribe].Increment(2);

  const std::vector<MetricFamily> families = metrics.Collect();

  EXPECT_EQ(CountOf(families, "augustad_messages_received_total", "debug_subscribe"), 2.0);
  EXPECT_EQ(CountOf(families, "augustad_messages_sent_total", "debug_subscribe"), 0.0);
}

// Requirements: NFR-07
TEST(HostMetricsDebugTest, EachShotJudgementSentIsCountedUnderItsOwnType) {
  HostMetrics metrics(kTickRate);
  const augusta::link::Outgoing judgement = augusta::link::Encode(augusta::link::ShotJudgement{}).value();

  augusta::host::CountSent(metrics, judgement);
  augusta::host::CountSent(metrics, judgement);

  const std::vector<MetricFamily> families = metrics.Collect();
  EXPECT_EQ(CountOf(families, "augustad_messages_sent_total", "shot_judgement"), 2.0);
  EXPECT_EQ(CountOf(families, "augustad_messages_received_total", "shot_judgement"), 0.0);
}

}  // namespace
