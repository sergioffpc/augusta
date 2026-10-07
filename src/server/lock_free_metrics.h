#ifndef AUGUSTA_SERVER_LOCK_FREE_METRICS_H_
#define AUGUSTA_SERVER_LOCK_FREE_METRICS_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <prometheus/client_metric.h>

/// \file
/// The kinds of metric the server's threads write in place (ADR-0049): a
/// counter, a gauge and a histogram, each a lock-free atomic, so a thread that
/// writes one never waits on the metrics endpoint's thread reading it, as it
/// would on prometheus-cpp's own histogram, which observes behind a mutex. Each
/// is turned into prometheus-cpp's form only when collected, by the series
/// functions here. Knows nothing of what is counted: HostMetrics
/// (host_metrics.h) and whoever else counts build on it.
namespace augusta::server {

// What lets no writer ever wait on a reader.
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
  explicit Histogram(std::span<const double> bounds);

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
/// Whoever labels them checks the range at compile time (LabelledExactly).
template <typename Key, Key First, Key Last>
class EnumCounters {
 public:
  using KeyType = Key;
  static constexpr Key kFirst = First;
  static constexpr Key kLast = Last;
  static_assert(std::to_underlying(First) <= std::to_underlying(Last));

  /// key must lie in the range.
  [[nodiscard]] Counter& operator[](Key key) { return counters_[Index(key)]; }
  [[nodiscard]] const Counter& operator[](Key key) const { return counters_[Index(key)]; }

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

/// Whether label, which gives each value of a one-byte enum its label and an
/// empty one to a value it does not count, labels exactly the range of
/// Counters (an EnumCounters) among every value the enum can hold. For a
/// static_assert beside the labels: a value added to the enum or a reorder
/// that the range does not follow then fails the build.
template <typename Counters, typename LabelOf>
consteval bool LabelledExactly(LabelOf label) {
  using Key = typename Counters::KeyType;
  static_assert(sizeof(Key) == 1);
  for (unsigned value = 0; value <= std::numeric_limits<std::uint8_t>::max(); ++value) {
    const bool in_range = value >= static_cast<unsigned>(std::to_underlying(Counters::kFirst)) &&
                          value <= static_cast<unsigned>(std::to_underlying(Counters::kLast));
    if (std::string_view(label(static_cast<Key>(value))).empty() == in_range) {
      return false;
    }
  }
  return true;
}

/// counter as one series of a counter family, labelled labels.
[[nodiscard]] prometheus::ClientMetric CounterSeries(const Counter& counter,
                                                     std::vector<prometheus::ClientMetric::Label> labels = {});

/// gauge as one series of a gauge family, labelled labels.
[[nodiscard]] prometheus::ClientMetric GaugeSeries(const Gauge& gauge,
                                                   std::vector<prometheus::ClientMetric::Label> labels = {});

/// histogram as one series of a histogram family, labelled labels.
[[nodiscard]] prometheus::ClientMetric HistogramSeries(const Histogram& histogram,
                                                       std::vector<prometheus::ClientMetric::Label> labels = {});

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_LOCK_FREE_METRICS_H_
