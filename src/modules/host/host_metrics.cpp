#include "augusta/host_metrics.h"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>

#include "augusta/command_queue.h"
#include "augusta/heartbeat.h"
#include "augusta/host_log.h"
#include "augusta/link.h"
#include "augusta/lock_free_metrics.h"
#include "augusta/tick.h"

namespace augusta::host {

namespace {

// The catalogue's tick buckets, 1 to 100 ms: 16.7 ms is a tick at 60 Hz.
constexpr std::initializer_list<double> kTickBuckets = {0.001,  0.002, 0.004, 0.008, 0.012,
                                                        0.0167, 0.020, 0.033, 0.050, 0.100};
// From a short match to an hour.
constexpr std::initializer_list<double> kMatchBuckets = {30, 60, 120, 180, 300, 600, 900, 1200, 1800, 3600};
// Around a datagram's size, up to a few.
constexpr std::initializer_list<double> kUpdateBuckets = {64, 128, 256, 512, 1024, 1200, 1500, 2048, 4096};
// Up to the 300 ms cap (simulation::kMaxShootersDelay), the bucket a capped round
// falls in (CountCombat observes it at the cap).
constexpr std::initializer_list<double> kShootersDelayBuckets = {0.025, 0.050, 0.075, 0.100, 0.125,
                                                                 0.150, 0.200, 0.250, 0.300};

}  // namespace

HostMetrics::HostMetrics(std::uint8_t tick_rate_hz)
    : tick_rate_hz(tick_rate_hz),
      tick_duration(kTickBuckets),
      match_duration(kMatchBuckets),
      authoritative_state_update_bytes(kUpdateBuckets),
      shooters_delay(kShootersDelayBuckets),
      replay_tick_duration(kTickBuckets) {}

std::optional<Activity> CountTick(HostMetrics& metrics, Heartbeat& heartbeat, const tick::Timing& timing,
                                  std::chrono::steady_clock::time_point now) {
  metrics.tick_duration.Observe(std::chrono::duration<double>(timing.duration).count());
  metrics.ticks.Increment();
  metrics.ticks_late.Increment(timing.late ? 1 : 0);
  metrics.tick_overruns.Increment(timing.overrun ? 1 : 0);
  metrics.tick_resyncs.Increment(timing.resynchronised ? 1 : 0);
  return heartbeat.Record(Totals(metrics), now);
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

void CountSent(HostMetrics& metrics, const link::Outgoing& message) {
  metrics.sent_bytes.Increment(message.Payload().size());
  metrics.messages_sent[message.Type()].Increment();
}

}  // namespace augusta::host
