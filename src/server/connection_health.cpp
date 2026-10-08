#include "connection_health.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <prometheus/client_metric.h>
#include <prometheus/metric_family.h>
#include <prometheus/metric_type.h>

#include "augusta/networking.h"
#include "augusta/primitives.h"
#include "connection_sample.h"
#include "lock_free_metrics.h"
#include "match.h"

namespace augusta::server {
namespace {

using prometheus::ClientMetric;
using prometheus::MetricFamily;
using prometheus::MetricType;

constexpr double kMillisecondsPerSecond = 1e3;
constexpr double kMicrosecondsPerSecond = 1e6;
constexpr double kNotMeasured = std::numeric_limits<double>::quiet_NaN();

// Quality's buckets are finest near 1, where a healthy connection sits.
constexpr std::array kRttBucketsSeconds{0.005, 0.01, 0.02, 0.04, 0.06, 0.08, 0.1, 0.15, 0.2, 0.3, 0.5, 1.0};
constexpr std::array kQualityBuckets{0.5, 0.8, 0.9, 0.95, 0.98, 0.99, 0.995, 0.999, 1.0};
constexpr std::array kJitterBucketsSeconds{0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2};

// No more Sessions than the Lobby holds are live at once.
constexpr std::size_t kSlotCount = primitives::kMaxPlayers;

// Decision: what the transport has measured, in base units. Quality and jitter
// read negative until it has (networking::ConnectionStats); nullopt then.
std::optional<double> Measured(double value, double per_base_unit) {
  return value >= 0.0 ? std::optional<double>(value / per_base_unit) : std::nullopt;
}

std::optional<double> RttSeconds(const networking::ConnectionStats& stats) {
  return Measured(stats.ping_ms, kMillisecondsPerSecond);
}

std::optional<double> QualityRatio(float quality) { return Measured(quality, 1.0); }

std::optional<double> JitterSeconds(const networking::ConnectionStats& stats) {
  return Measured(stats.max_jitter_us, kMicrosecondsPerSecond);
}

void Observe(Histogram& histogram, const std::optional<double>& value) {
  if (value.has_value()) {
    histogram.Observe(*value);
  }
}

// Each of a Session's gauges.
enum SessionValue : std::uint8_t {
  kRtt,
  kQualityLocal,
  kQualityRemote,
  kJitter,
  kInBytes,
  kOutBytes,
  kPendingBytes,
  kSessionValueCount,
};

// One Session's gauges, each kNotMeasured until first measured.
struct SessionGauges {
  SessionId session{};
  std::array<double, kSessionValueCount> values{};
};

// value, unless measured replaces it.
void Update(double& value, const std::optional<double>& measured) {
  if (measured.has_value()) {
    value = *measured;
  }
}

// Every Session's gauges, one slot each, as the writer last set them.
using SessionSlots = std::array<std::optional<SessionGauges>, kSlotCount>;

// Every Session's gauges, published whole by one writer and read by any thread
// (ADR-0049): a sequence lock, so the writer never waits, and a reader that
// overlaps a publish reads again. One lock over every Session, not one each, so
// a read never shows a Session that one Record ended alongside one that the
// next Record added.
class PublishedGauges {
 public:
  // From the one writer.
  void Publish(const SessionSlots& sessions) {
    const std::uint64_t version = version_.load(std::memory_order_relaxed);
    version_.store(version + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      slots_[slot].Store(sessions[slot]);
    }
    version_.store(version + 2, std::memory_order_release);
  }

  // The gauges of every Session the last publish had, in slot order.
  [[nodiscard]] std::vector<SessionGauges> Read() const {
    std::vector<SessionGauges> sessions;
    sessions.reserve(kSlotCount);
    while (true) {
      const std::uint64_t before = version_.load(std::memory_order_acquire);
      if (before % 2 != 0) {
        std::this_thread::yield();
        continue;
      }
      sessions.clear();
      for (const Slot& slot : slots_) {
        if (std::optional<SessionGauges> gauges = slot.Load()) {
          sessions.push_back(*gauges);
        }
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      if (version_.load(std::memory_order_relaxed) == before) {
        return sessions;
      }
    }
  }

 private:
  // Atomics, though only the version orders them: a read that overlaps a
  // publish is then discarded rather than a data race.
  class Slot {
   public:
    void Store(const std::optional<SessionGauges>& gauges) {
      occupied_.store(gauges.has_value(), std::memory_order_relaxed);
      if (gauges.has_value()) {
        session_.store(std::to_underlying(gauges->session), std::memory_order_relaxed);
        for (std::size_t i = 0; i < values_.size(); ++i) {
          values_[i].store(gauges->values[i], std::memory_order_relaxed);
        }
      }
    }

    [[nodiscard]] std::optional<SessionGauges> Load() const {
      if (!occupied_.load(std::memory_order_relaxed)) {
        return std::nullopt;
      }
      SessionGauges gauges{.session = SessionId{session_.load(std::memory_order_relaxed)}, .values = {}};
      for (std::size_t i = 0; i < values_.size(); ++i) {
        gauges.values[i] = values_[i].load(std::memory_order_relaxed);
      }
      return gauges;
    }

   private:
    std::atomic<bool> occupied_{false};
    std::atomic<std::uint32_t> session_{0};
    std::array<std::atomic<double>, kSessionValueCount> values_{};
  };

  // Odd while a publish is in progress.
  std::atomic<std::uint64_t> version_{0};
  std::array<Slot, kSlotCount> slots_;
};

// One of a gauge family's series per Session: which of its values, and the
// direction label it has, if any.
struct SessionSeries {
  SessionValue value;
  const char* direction = nullptr;
};

MetricFamily HistogramFamily(std::string name, std::string help, std::vector<ClientMetric> series) {
  return MetricFamily{
      .name = std::move(name), .help = std::move(help), .type = MetricType::Histogram, .metric = std::move(series)};
}

// A gauge family with each of series for every Session that has measured it.
MetricFamily SessionFamily(std::string name, std::string help, const std::vector<SessionGauges>& sessions,
                           std::initializer_list<SessionSeries> series) {
  MetricFamily family{.name = std::move(name), .help = std::move(help), .type = MetricType::Gauge, .metric = {}};
  for (const SessionSeries& each : series) {
    for (const SessionGauges& session : sessions) {
      const double value = session.values[each.value];
      if (std::isnan(value)) {
        continue;
      }
      ClientMetric metric;
      metric.label.push_back({.name = "session_id", .value = std::to_string(std::to_underlying(session.session))});
      if (each.direction != nullptr) {
        metric.label.push_back({.name = "direction", .value = each.direction});
      }
      metric.gauge.value = value;
      family.metric.push_back(std::move(metric));
    }
  }
  return family;
}

}  // namespace

struct ConnectionHealth::Impl {
  // Over every connection.
  Histogram rtt{kRttBucketsSeconds};
  Histogram quality_local{kQualityBuckets};
  Histogram quality_remote{kQualityBuckets};
  Histogram jitter{kJitterBucketsSeconds};
  // By Session: the writer's own copy, and what the endpoint reads of it.
  SessionSlots sessions;
  PublishedGauges published;

  void ObserveAll(const networking::ConnectionStats& stats) {
    Observe(rtt, RttSeconds(stats));
    Observe(quality_local, QualityRatio(stats.quality_local));
    Observe(quality_remote, QualityRatio(stats.quality_remote));
    Observe(jitter, JitterSeconds(stats));
  }

  // The slot session's gauges are in, or else a free one; nullopt if every
  // slot is taken, which no more Sessions than the Lobby holds can do.
  [[nodiscard]] std::optional<std::size_t> SlotOf(SessionId session) const {
    std::optional<std::size_t> free;
    for (std::size_t slot = 0; slot < sessions.size(); ++slot) {
      if (!sessions[slot].has_value()) {
        free = free.value_or(slot);
      } else if (sessions[slot]->session == session) {
        return slot;
      }
    }
    return free;
  }

  void SetGauges(SessionId session, const networking::ConnectionStats& stats) {
    const std::optional<std::size_t> slot = SlotOf(session);
    if (!slot.has_value()) {
      return;
    }
    SessionGauges gauges{.session = session, .values = {}};
    if (sessions[*slot].has_value()) {
      gauges = *sessions[*slot];
    } else {
      gauges.values.fill(kNotMeasured);
    }
    Update(gauges.values[kRtt], RttSeconds(stats));
    Update(gauges.values[kQualityLocal], QualityRatio(stats.quality_local));
    Update(gauges.values[kQualityRemote], QualityRatio(stats.quality_remote));
    Update(gauges.values[kJitter], JitterSeconds(stats));
    Update(gauges.values[kInBytes], stats.in_bytes_per_sec);
    Update(gauges.values[kOutBytes], stats.out_bytes_per_sec);
    Update(gauges.values[kPendingBytes], stats.pending_bytes);
    sessions[*slot] = gauges;
  }

  // Removes the gauges of every Session not in samples: it has ended.
  void RemoveEnded(const std::vector<ConnectionSample>& samples) {
    for (std::optional<SessionGauges>& gauges : sessions) {
      if (gauges.has_value() && std::ranges::none_of(samples, [&gauges](const ConnectionSample& sample) {
            return sample.session == gauges->session;
          })) {
        gauges.reset();
      }
    }
  }

  void Publish() { published.Publish(sessions); }
};

ConnectionHealth::ConnectionHealth() : impl_(std::make_unique<Impl>()) {}

ConnectionHealth::~ConnectionHealth() = default;

void ConnectionHealth::Record(const std::vector<ConnectionSample>& samples) {
  // First, so an ended Session's slot is free for a new one.
  impl_->RemoveEnded(samples);
  for (const ConnectionSample& sample : samples) {
    impl_->ObserveAll(sample.stats);
    if (sample.session.has_value()) {
      impl_->SetGauges(*sample.session, sample.stats);
    }
  }
  impl_->Publish();
}

std::vector<MetricFamily> ConnectionHealth::Collect() const {
  const std::vector<SessionGauges> sessions = impl_->published.Read();
  return {
      HistogramFamily("augustad_connection_rtt_seconds",
                      "Every connection's round-trip time, sampled once a heartbeat interval.",
                      {HistogramSeries(impl_->rtt)}),
      HistogramFamily(
          "augustad_connection_quality_ratio",
          "Every connection's share of packets delivered, as measured here (local) and by the client (remote).",
          {HistogramSeries(impl_->quality_local, {{.name = "direction", .value = "local"}}),
           HistogramSeries(impl_->quality_remote, {{.name = "direction", .value = "remote"}})}),
      HistogramFamily("augustad_connection_jitter_seconds",
                      "Every connection's worst jitter over each heartbeat interval.",
                      {HistogramSeries(impl_->jitter)}),
      SessionFamily("augustad_session_connection_rtt_seconds", "The round-trip time of each Session's connection.",
                    sessions, {{.value = kRtt}}),
      SessionFamily("augustad_session_connection_quality_ratio",
                    "The share of packets each Session's connection delivers, each way.", sessions,
                    {{.value = kQualityLocal, .direction = "local"}, {.value = kQualityRemote, .direction = "remote"}}),
      SessionFamily("augustad_session_connection_jitter_seconds",
                    "The worst jitter of each Session's connection over each heartbeat interval.", sessions,
                    {{.value = kJitter}}),
      SessionFamily("augustad_connection_in_bytes_per_second", "What each Session's connection receives.", sessions,
                    {{.value = kInBytes}}),
      SessionFamily("augustad_connection_out_bytes_per_second", "What each Session's connection sends.", sessions,
                    {{.value = kOutBytes}}),
      SessionFamily("augustad_connection_pending_bytes",
                    "What each Session's connection has queued, or in flight unacknowledged.", sessions,
                    {{.value = kPendingBytes}}),
  };
}

}  // namespace augusta::server
