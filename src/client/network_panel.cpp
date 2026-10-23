#include "network_panel.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/debug_panels.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "net_stats.h"

namespace augusta::client {

namespace {

constexpr std::string_view kResetPeaksButton = "reset_peaks";
constexpr double kMicrosecondsPerMillisecond = 1000.0;
constexpr double kPercent = 100.0;
constexpr double kKiB = 1024.0;
constexpr double kMiB = kKiB * 1024.0;

// Bytes -> "300B", "2.0KB" or "3.4MB", with suffix after the unit.
std::string FormatBytes(double bytes, std::string_view suffix = "") {
  if (bytes >= kMiB) {
    return std::format("{:.1f}MB{}", bytes / kMiB, suffix);
  }
  if (bytes >= kKiB) {
    return std::format("{:.1f}KB{}", bytes / kKiB, suffix);
  }
  return std::format("{:.0f}B{}", bytes, suffix);
}

// Bytes per second as the Debug HUD shows them: "512B/s", "1.2KB/s", "3.4MB/s".
std::string FormatRate(double bytes_per_sec) { return FormatBytes(bytes_per_sec, "/s"); }

std::string FormatMessageRate(double messages_per_sec) { return std::format("{:.1f}", messages_per_sec); }

std::string FormatPercent(double percent) { return std::format("{:.1f}%", percent); }

// "--" while unmeasured.
template <typename Format>
std::string FormatOrDashes(const std::optional<double>& value, Format format) {
  return value.has_value() ? format(*value) : "--";
}

// A value and its peak, "300B (peak 2.0KB)", the peak labelled by peak_label
// when there is one; "--" for either not measured, alone when neither is.
template <typename Format>
std::string FormatWatched(const std::optional<double>& now, const std::optional<double>& peak, Format format,
                          std::string_view peak_label = "") {
  if (!now.has_value() && !peak.has_value()) {
    return "--";
  }
  const std::string label = peak_label.empty() ? std::string() : std::format("{} ", peak_label);
  return std::format("{} ({}{})", FormatOrDashes(now, format), label, FormatOrDashes(peak, format));
}

// Quality 0..1 as a percent; nullopt while not measured (negative).
std::optional<double> QualityPercent(float quality) {
  return quality >= 0.0F ? std::optional<double>(static_cast<double>(quality) * kPercent) : std::nullopt;
}

std::optional<double> JitterMs(const link::ConnectionStats& stats) {
  return stats.max_jitter_us >= 0
             ? std::optional<double>(static_cast<double>(stats.max_jitter_us) / kMicrosecondsPerMillisecond)
             : std::nullopt;
}

// Whether any of later's counts is below earlier's: the counts started over,
// at a new connection.
bool StartedOver(const link::Traffic& earlier, const link::Traffic& later) {
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    const link::TypeTraffic& before = earlier.by_type.at(i);
    const link::TypeTraffic& after = later.by_type.at(i);
    if (after.sent.messages < before.sent.messages || after.sent.bytes < before.sent.bytes ||
        after.received.messages < before.received.messages || after.received.bytes < before.received.bytes) {
      return true;
    }
  }
  return false;
}

double PerSecond(std::uint64_t earlier, std::uint64_t later, double seconds) {
  return static_cast<double>(later - earlier) / seconds;
}

void Plot(std::deque<float>& series, std::optional<double> value) {
  if (!value.has_value()) {
    return;
  }
  series.push_back(static_cast<float>(*value));
  if (series.size() > NetworkPanel::kPlotPoints) {
    series.pop_front();
  }
}

debug::HistoryPlot PlotOf(std::string label, const std::deque<float>& series) {
  return {.label = std::move(label), .values = std::vector<float>(series.begin(), series.end())};
}

}  // namespace

void ConnectionReadings::Record(const std::optional<link::ConnectionStats>& stats, const link::Traffic& traffic) {
  const std::int32_t peak_jitter_us = newest_.stats.has_value() ? newest_.stats->max_jitter_us : -1;
  newest_ = {.stats = stats, .traffic = traffic};
  if (newest_.stats.has_value()) {
    newest_.stats->max_jitter_us = std::max(newest_.stats->max_jitter_us, peak_jitter_us);
  }
}

ConnectionReading ConnectionReadings::Take() {
  const ConnectionReading taken = newest_;
  if (newest_.stats.has_value()) {
    newest_.stats->max_jitter_us = -1;
  }
  return taken;
}

void NetworkPanel::Watched::Set(std::optional<double> value) {
  now_ = value;
  if (!value.has_value()) {
    return;
  }
  if (!peak_.has_value() || (lower_is_worse_ ? *value < *peak_ : *value > *peak_)) {
    peak_ = value;
  }
}

bool NetworkPanel::SampleDue(Clock::time_point now) const { return !next_sample_.has_value() || now >= *next_sample_; }

void NetworkPanel::Sample(const NetworkSample& sample, Clock::time_point now) {
  if (!next_sample_.has_value()) {
    next_sample_ = now + kSamplePeriod;
  } else if (now >= *next_sample_) {
    // The first boundary after now, however many frames skipped.
    next_sample_ = *next_sample_ + (((now - *next_sample_) / kSamplePeriod) + 1) * kSamplePeriod;
  }
  SampleConnection(sample.connection.stats);
  SampleTraffic(sample.connection.traffic, now);
}

void NetworkPanel::SampleConnection(const std::optional<link::ConnectionStats>& stats) {
  connection_.stats = stats;
  if (!stats.has_value()) {
    connection_.quality_local.Set(std::nullopt);
    connection_.quality_remote.Set(std::nullopt);
    connection_.pending_bytes.Set(std::nullopt);
    return;
  }
  connection_.quality_local.Set(QualityPercent(stats->quality_local));
  connection_.quality_remote.Set(QualityPercent(stats->quality_remote));
  connection_.pending_bytes.Set(static_cast<double>(stats->pending_bytes));
  Plot(plots_.rtt_ms, static_cast<double>(stats->ping_ms));
  Plot(plots_.jitter_ms, JitterMs(*stats));
  Plot(plots_.loss_percent, PacketLossPercent(*stats));
  Plot(plots_.in_bytes_per_sec, static_cast<double>(stats->in_bytes_per_sec));
  Plot(plots_.out_bytes_per_sec, static_cast<double>(stats->out_bytes_per_sec));
}

void NetworkPanel::SampleTraffic(const link::Traffic& traffic, Clock::time_point now) {
  if (!traffic_window_.empty() && StartedOver(traffic_window_.back().second, traffic)) {
    traffic_window_.clear();
  }
  traffic_window_.emplace_back(now, traffic);
  while (now - traffic_window_.front().first > kRateWindow) {
    traffic_window_.pop_front();
  }
  const auto& [oldest_time, oldest] = traffic_window_.front();
  const double seconds = std::chrono::duration<double>(now - oldest_time).count();
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    TypeRates& rates = rates_.at(i);
    if (seconds <= 0.0) {
      rates.messages_in.Set(std::nullopt);
      rates.bytes_in.Set(std::nullopt);
      rates.messages_out.Set(std::nullopt);
      rates.bytes_out.Set(std::nullopt);
      continue;
    }
    const link::TypeTraffic& before = oldest.by_type.at(i);
    const link::TypeTraffic& after = traffic.by_type.at(i);
    rates.messages_in.Set(PerSecond(before.received.messages, after.received.messages, seconds));
    rates.bytes_in.Set(PerSecond(before.received.bytes, after.received.bytes, seconds));
    rates.messages_out.Set(PerSecond(before.sent.messages, after.sent.messages, seconds));
    rates.bytes_out.Set(PerSecond(before.sent.bytes, after.sent.bytes, seconds));
  }
}

debug::Panel NetworkPanel::BuildPanel() const {
  debug::Panel panel{.id = std::string(kNetworkPanelId), .title = "Network", .elements = {}};
  const std::optional<link::ConnectionStats>& stats = connection_.stats;
  const auto rate = [](double value) { return FormatRate(value); };
  const auto bytes = [](double value) { return FormatBytes(value); };
  const auto percent = [](double value) { return FormatPercent(value); };

  panel.elements.emplace_back(debug::Text{.text = "Connection"});
  panel.elements.emplace_back(
      debug::Text{.text = "RTT: " + (stats.has_value() ? std::format("{}ms", stats->ping_ms) : std::string("--"))});
  panel.elements.emplace_back(
      debug::Text{.text = "Jitter: " + FormatOrDashes(stats.has_value() ? JitterMs(*stats) : std::nullopt,
                                                      [](double value) { return std::format("{:.1f}ms", value); })});
  panel.elements.emplace_back(
      debug::Text{.text = "Quality local: " + FormatWatched(connection_.quality_local.Now(),
                                                            connection_.quality_local.Peak(), percent, "worst")});
  panel.elements.emplace_back(
      debug::Text{.text = "Quality remote: " + FormatWatched(connection_.quality_remote.Now(),
                                                             connection_.quality_remote.Peak(), percent, "worst")});
  panel.elements.emplace_back(
      debug::Text{.text = "In: " + FormatOrDashes(stats.transform([](const link::ConnectionStats& s) {
                            return static_cast<double>(s.in_bytes_per_sec);
                          }),
                                                  rate)});
  panel.elements.emplace_back(
      debug::Text{.text = "Out: " + FormatOrDashes(stats.transform([](const link::ConnectionStats& s) {
                            return static_cast<double>(s.out_bytes_per_sec);
                          }),
                                                   rate)});
  panel.elements.emplace_back(
      debug::Text{.text = "Pending: " + FormatWatched(connection_.pending_bytes.Now(), connection_.pending_bytes.Peak(),
                                                      bytes, "peak")});
  panel.elements.emplace_back(PlotOf("RTT (ms)", plots_.rtt_ms));
  panel.elements.emplace_back(PlotOf("Jitter (ms)", plots_.jitter_ms));
  panel.elements.emplace_back(PlotOf("Loss (%)", plots_.loss_percent));
  panel.elements.emplace_back(PlotOf("In (B/s)", plots_.in_bytes_per_sec));
  panel.elements.emplace_back(PlotOf("Out (B/s)", plots_.out_bytes_per_sec));

  panel.elements.emplace_back(debug::Text{.text = "Traffic per message type, now (peak)"});
  debug::Table table{.header = {"Type", "Msgs/s in", "Bytes/s in", "Msgs/s out", "Bytes/s out"}, .rows = {}};
  const auto messages = [](double value) { return FormatMessageRate(value); };
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    const TypeRates& rates = rates_.at(i);
    table.rows.push_back({std::string(link::NameOf(static_cast<link::MessageType>(i))),
                          FormatWatched(rates.messages_in.Now(), rates.messages_in.Peak(), messages),
                          FormatWatched(rates.bytes_in.Now(), rates.bytes_in.Peak(), rate),
                          FormatWatched(rates.messages_out.Now(), rates.messages_out.Peak(), messages),
                          FormatWatched(rates.bytes_out.Now(), rates.bytes_out.Peak(), rate)});
  }
  panel.elements.emplace_back(std::move(table));
  panel.elements.emplace_back(debug::Button{.id = std::string(kResetPeaksButton), .label = "Reset peaks"});
  return panel;
}

void NetworkPanel::Update(const debug::ControlEvent& event) {
  if (event.panel == kNetworkPanelId && event.control == kResetPeaksButton &&
      std::holds_alternative<debug::ButtonPress>(event.value)) {
    ResetPeaks();
  }
}

void NetworkPanel::ResetPeaks() {
  connection_.quality_local.ResetPeak();
  connection_.quality_remote.ResetPeak();
  connection_.pending_bytes.ResetPeak();
  for (TypeRates& rates : rates_) {
    rates.messages_in.ResetPeak();
    rates.bytes_in.ResetPeak();
    rates.messages_out.ResetPeak();
    rates.bytes_out.ResetPeak();
  }
}

void NetworkPanel::Close() { *this = NetworkPanel{}; }

}  // namespace augusta::client
