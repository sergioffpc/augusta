#include "connection_health.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/labels.h>
#include <prometheus/registry.h>

#include "augusta/networking.h"
#include "match.h"

namespace augusta::server {
namespace {

using prometheus::Family;
using prometheus::Gauge;
using prometheus::Histogram;

constexpr double kMillisecondsPerSecond = 1e3;
constexpr double kMicrosecondsPerSecond = 1e6;

// Quality's buckets are finest near 1, where a healthy connection sits.
constexpr std::array kRttBucketsSeconds{0.005, 0.01, 0.02, 0.04, 0.06, 0.08, 0.1, 0.15, 0.2, 0.3, 0.5, 1.0};
constexpr std::array kQualityBuckets{0.5, 0.8, 0.9, 0.95, 0.98, 0.99, 0.995, 0.999, 1.0};
constexpr std::array kJitterBucketsSeconds{0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2};

Histogram::BucketBoundaries Buckets(std::span<const double> bounds) { return {bounds.begin(), bounds.end()}; }

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

Family<Histogram>& HistogramFamily(prometheus::Registry& registry, const char* name, const char* help) {
  return prometheus::BuildHistogram().Name(name).Help(help).Register(registry);
}

Family<Gauge>& GaugeFamily(prometheus::Registry& registry, const char* name, const char* help) {
  return prometheus::BuildGauge().Name(name).Help(help).Register(registry);
}

void Observe(Histogram& histogram, const std::optional<double>& value) {
  if (value.has_value()) {
    histogram.Observe(*value);
  }
}

// One time series of a Session's: null until first measured.
struct SessionGauge {
  Family<Gauge>* family = nullptr;
  Gauge* gauge = nullptr;

  void Set(Family<Gauge>& in, const prometheus::Labels& labels, const std::optional<double>& value) {
    if (!value.has_value()) {
      return;
    }
    if (gauge == nullptr) {
      family = &in;
      gauge = &in.Add(labels);
    }
    gauge->Set(*value);
  }

  void Remove() const {
    if (gauge != nullptr) {
      family->Remove(gauge);
    }
  }
};

struct SessionGauges {
  SessionGauge rtt;
  SessionGauge quality_local;
  SessionGauge quality_remote;
  SessionGauge jitter;
  SessionGauge in_bytes;
  SessionGauge out_bytes;
  SessionGauge pending_bytes;

  void Remove() {
    for (SessionGauge* each : {&rtt, &quality_local, &quality_remote, &jitter, &in_bytes, &out_bytes, &pending_bytes}) {
      each->Remove();
    }
  }
};

}  // namespace

struct ConnectionHealth::Impl {
  // Over every connection.
  Family<Histogram>& rtt_histogram;
  Family<Histogram>& quality_histogram;
  Family<Histogram>& jitter_histogram;
  Histogram& rtt;
  Histogram& quality_local;
  Histogram& quality_remote;
  Histogram& jitter;
  // By Session. Distinct names from the histograms': one name can't be both a
  // histogram and a gauge in the exposition.
  Family<Gauge>& rtt_gauge;
  Family<Gauge>& quality_gauge;
  Family<Gauge>& jitter_gauge;
  Family<Gauge>& in_bytes_gauge;
  Family<Gauge>& out_bytes_gauge;
  Family<Gauge>& pending_bytes_gauge;
  std::unordered_map<SessionId, SessionGauges> sessions;

  explicit Impl(prometheus::Registry& registry)
      : rtt_histogram(HistogramFamily(registry, "augustad_connection_rtt_seconds",
                                      "Every connection's round-trip time, sampled once a second.")),
        quality_histogram(HistogramFamily(
            registry, "augustad_connection_quality_ratio",
            "Every connection's share of packets delivered, as measured here (local) and by the client (remote).")),
        jitter_histogram(HistogramFamily(registry, "augustad_connection_jitter_seconds",
                                         "Every connection's worst jitter over the last second.")),
        rtt(rtt_histogram.Add({}, Buckets(kRttBucketsSeconds))),
        quality_local(quality_histogram.Add({{"direction", "local"}}, Buckets(kQualityBuckets))),
        quality_remote(quality_histogram.Add({{"direction", "remote"}}, Buckets(kQualityBuckets))),
        jitter(jitter_histogram.Add({}, Buckets(kJitterBucketsSeconds))),
        rtt_gauge(GaugeFamily(registry, "augustad_session_connection_rtt_seconds",
                              "The round-trip time of each Session's connection.")),
        quality_gauge(GaugeFamily(registry, "augustad_session_connection_quality_ratio",
                                  "The share of packets each Session's connection delivers, each way.")),
        jitter_gauge(GaugeFamily(registry, "augustad_session_connection_jitter_seconds",
                                 "The worst jitter of each Session's connection over the last second.")),
        in_bytes_gauge(GaugeFamily(registry, "augustad_connection_in_bytes_per_second",
                                   "What each Session's connection receives.")),
        out_bytes_gauge(
            GaugeFamily(registry, "augustad_connection_out_bytes_per_second", "What each Session's connection sends.")),
        pending_bytes_gauge(GaugeFamily(registry, "augustad_connection_pending_bytes",
                                        "What each Session's connection has queued or in flight unacknowledged.")) {}

  void ObserveAll(const networking::ConnectionStats& stats) {
    Observe(rtt, RttSeconds(stats));
    Observe(quality_local, QualityRatio(stats.quality_local));
    Observe(quality_remote, QualityRatio(stats.quality_remote));
    Observe(jitter, JitterSeconds(stats));
  }

  void SetGauges(SessionId session, const networking::ConnectionStats& stats) {
    const std::string id = std::to_string(static_cast<std::uint32_t>(session));
    SessionGauges& gauges = sessions[session];
    gauges.rtt.Set(rtt_gauge, {{"session_id", id}}, RttSeconds(stats));
    gauges.quality_local.Set(quality_gauge, {{"session_id", id}, {"direction", "local"}},
                             QualityRatio(stats.quality_local));
    gauges.quality_remote.Set(quality_gauge, {{"session_id", id}, {"direction", "remote"}},
                              QualityRatio(stats.quality_remote));
    gauges.jitter.Set(jitter_gauge, {{"session_id", id}}, JitterSeconds(stats));
    gauges.in_bytes.Set(in_bytes_gauge, {{"session_id", id}}, stats.in_bytes_per_sec);
    gauges.out_bytes.Set(out_bytes_gauge, {{"session_id", id}}, stats.out_bytes_per_sec);
    gauges.pending_bytes.Set(pending_bytes_gauge, {{"session_id", id}}, stats.pending_bytes);
  }

  // Removes the gauges of every Session not in present: it has ended.
  void RemoveEnded(const std::unordered_set<SessionId>& present) {
    std::erase_if(sessions, [&present](auto& entry) {
      if (present.contains(entry.first)) {
        return false;
      }
      entry.second.Remove();
      return true;
    });
  }
};

ConnectionHealth::ConnectionHealth(prometheus::Registry& registry) : impl_(std::make_unique<Impl>(registry)) {}

ConnectionHealth::~ConnectionHealth() = default;

void ConnectionHealth::Record(const std::vector<ConnectionSample>& samples) {
  std::unordered_set<SessionId> present;
  for (const ConnectionSample& sample : samples) {
    impl_->ObserveAll(sample.stats);
    if (sample.session.has_value()) {
      present.insert(*sample.session);
      impl_->SetGauges(*sample.session, sample.stats);
    }
  }
  impl_->RemoveEnded(present);
}

}  // namespace augusta::server
