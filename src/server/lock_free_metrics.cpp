#include "lock_free_metrics.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <prometheus/client_metric.h>

namespace augusta::server {

Histogram::Histogram(std::initializer_list<double> bounds) : Histogram(std::span<const double>(bounds)) {}

Histogram::Histogram(std::span<const double> bounds)
    : bounds_(bounds.begin(), bounds.end()), counts_(bounds.size() + 1) {}

void Histogram::Observe(double value) {
  // The first bucket whose bound is no less than value; past the last, +Inf.
  const auto bucket = std::ranges::lower_bound(bounds_, value) - bounds_.begin();
  counts_[static_cast<std::size_t>(bucket)].fetch_add(1, std::memory_order_relaxed);
  sum_.fetch_add(value, std::memory_order_relaxed);
}

Histogram::Snapshot Histogram::Read() const {
  Snapshot snapshot;
  snapshot.cumulative_counts.reserve(counts_.size());
  std::uint64_t cumulative = 0;
  for (const std::atomic<std::uint64_t>& count : counts_) {
    cumulative += count.load(std::memory_order_relaxed);
    snapshot.cumulative_counts.push_back(cumulative);
  }
  snapshot.sum = sum_.load(std::memory_order_relaxed);
  return snapshot;
}

prometheus::ClientMetric CounterSeries(const Counter& counter, std::vector<prometheus::ClientMetric::Label> labels) {
  prometheus::ClientMetric series;
  series.label = std::move(labels);
  series.counter.value = static_cast<double>(counter.Value());
  return series;
}

prometheus::ClientMetric GaugeSeries(const Gauge& gauge, std::vector<prometheus::ClientMetric::Label> labels) {
  prometheus::ClientMetric series;
  series.label = std::move(labels);
  series.gauge.value = gauge.Value();
  return series;
}

prometheus::ClientMetric HistogramSeries(const Histogram& histogram,
                                         std::vector<prometheus::ClientMetric::Label> labels) {
  const Histogram::Snapshot snapshot = histogram.Read();
  const std::span<const double> bounds = histogram.Bounds();
  prometheus::ClientMetric series;
  series.label = std::move(labels);
  series.histogram.sample_count = snapshot.cumulative_counts.back();
  series.histogram.sample_sum = snapshot.sum;
  for (std::size_t i = 0; i < snapshot.cumulative_counts.size(); ++i) {
    series.histogram.bucket.push_back(prometheus::ClientMetric::Bucket{
        .cumulative_count = snapshot.cumulative_counts[i],
        .upper_bound = i < bounds.size() ? bounds[i] : std::numeric_limits<double>::infinity(),
    });
  }
  return series;
}

}  // namespace augusta::server
