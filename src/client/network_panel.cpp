#include "network_panel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/correction_tally.h"
#include "augusta/debug_panels.h"
#include "augusta/hud.h"
#include "augusta/interpolation.h"
#include "augusta/link.h"
#include "augusta/link_types.h"
#include "net_stats.h"

namespace augusta::client {

namespace {

constexpr std::string_view kResetPeaksButton = "reset_peaks";
constexpr double kMicrosecondsPerMillisecond = 1000.0;
constexpr double kPercent = 100.0;
constexpr double kMillisecondsPerSecond = 1000.0;

// One of the Impairment group's sliders: the value it sets and its range,
// from zero.
struct ImpairmentSlider {
  std::string_view id;
  std::string_view label;
  float max;
  bool whole_numbers;
  float (*get)(const link::SimulatedConditions&);
  void (*set)(link::SimulatedConditions&, float);
};

int WholeMilliseconds(float value) { return static_cast<int>(std::lround(value)); }

constexpr std::array kImpairmentSliders{
    ImpairmentSlider{.id = "latency_ms",
                     .label = "Latency, each way (ms)",
                     .max = 250.0F,
                     .whole_numbers = true,
                     .get = [](const link::SimulatedConditions& c) { return static_cast<float>(c.latency_ms); },
                     .set = [](link::SimulatedConditions& c, float value) { c.latency_ms = WholeMilliseconds(value); }},
    ImpairmentSlider{
        .id = "jitter_mean_ms",
        .label = "Jitter mean (ms)",
        .max = 100.0F,
        .whole_numbers = true,
        .get = [](const link::SimulatedConditions& c) { return static_cast<float>(c.jitter_mean_ms); },
        .set = [](link::SimulatedConditions& c, float value) { c.jitter_mean_ms = WholeMilliseconds(value); }},
    ImpairmentSlider{
        .id = "jitter_max_ms",
        .label = "Jitter max (ms)",
        .max = 100.0F,
        .whole_numbers = true,
        .get = [](const link::SimulatedConditions& c) { return static_cast<float>(c.jitter_max_ms); },
        .set = [](link::SimulatedConditions& c, float value) { c.jitter_max_ms = WholeMilliseconds(value); }},
    ImpairmentSlider{.id = "loss_percent",
                     .label = "Loss (%)",
                     .max = 50.0F,
                     .whole_numbers = false,
                     .get = [](const link::SimulatedConditions& c) { return c.loss_percent; },
                     .set = [](link::SimulatedConditions& c, float value) { c.loss_percent = value; }},
    ImpairmentSlider{.id = "reorder_percent",
                     .label = "Reorder (%)",
                     .max = 50.0F,
                     .whole_numbers = false,
                     .get = [](const link::SimulatedConditions& c) { return c.reorder_percent; },
                     .set = [](link::SimulatedConditions& c, float value) { c.reorder_percent = value; }},
    ImpairmentSlider{
        .id = "reorder_delay_ms",
        .label = "Reorder delay (ms)",
        .max = 100.0F,
        .whole_numbers = true,
        .get = [](const link::SimulatedConditions& c) { return static_cast<float>(c.reorder_delay_ms); },
        .set = [](link::SimulatedConditions& c, float value) { c.reorder_delay_ms = WholeMilliseconds(value); }},
};

// A named link, one click away: the impaired-link tests' profiles
// (tools/agent/tests/netcode_test.cpp), from a good broadband link up to the
// agreed worst case (ADR-0013).
struct ImpairmentProfile {
  std::string_view id;
  std::string_view label;
  link::SimulatedConditions conditions;
};

constexpr std::array kImpairmentProfiles{
    ImpairmentProfile{.id = "profile_broadband",
                      .label = "Broadband",
                      .conditions = {.latency_ms = 20,
                                     .jitter_mean_ms = 5,
                                     .jitter_max_ms = 20,
                                     .loss_percent = 1.0F,
                                     .reorder_percent = 1.0F,
                                     .reorder_delay_ms = 10}},
    ImpairmentProfile{.id = "profile_nfr02",
                      .label = "NFR-02",
                      .conditions = {.latency_ms = 50,
                                     .jitter_mean_ms = 10,
                                     .jitter_max_ms = 30,
                                     .loss_percent = 5.0F,
                                     .reorder_percent = 2.0F,
                                     .reorder_delay_ms = 20}},
    ImpairmentProfile{.id = "profile_worst_case",
                      .label = "Worst case",
                      .conditions = {.latency_ms = 75,
                                     .jitter_mean_ms = 10,
                                     .jitter_max_ms = 30,
                                     .loss_percent = 10.0F,
                                     .reorder_percent = 5.0F,
                                     .reorder_delay_ms = 20}},
};

constexpr std::string_view kImpairmentOffButton = "impairment_off";

std::string FormatBytes(double bytes) { return hud::FormatBytes(bytes); }

std::string FormatRate(double bytes_per_sec) { return hud::FormatRate(bytes_per_sec); }

std::string FormatPerSecond(double per_second) { return std::format("{:.1f}", per_second); }

std::string FormatPercent(double percent) { return std::format("{:.1f}%", percent); }

std::string FormatMilliseconds(double ms) { return std::format("{:.1f}ms", ms); }

// "--" while unmeasured.
std::string FormatOrDashes(const std::optional<double>& value, std::string (*format)(double)) {
  return value.has_value() ? format(*value) : "--";
}

// The connection's RTT, "42ms", or "--" while not connected.
std::string RttText(const std::optional<link::ConnectionStats>& stats) {
  return stats.has_value() ? std::format("{}ms", stats->ping_ms) : "--";
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

// A type's counts in TypeRates' column order: messages and bytes in, then out.
std::array<std::uint64_t, 4> CountsOf(const link::TypeTraffic& traffic) {
  return {traffic.received.messages, traffic.received.bytes, traffic.sent.messages, traffic.sent.bytes};
}

// Whether any of later's counts is below earlier's: the counts started over,
// at a new connection.
bool StartedOver(const link::Traffic& earlier, const link::Traffic& later) {
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    const std::array<std::uint64_t, 4> before = CountsOf(earlier.by_type.at(i));
    const std::array<std::uint64_t, 4> after = CountsOf(later.by_type.at(i));
    if (!std::ranges::equal(before, after, std::ranges::less_equal{})) {
      return true;
    }
  }
  const link::AuthoritativeStateTally& before = earlier.authoritative_state;
  const link::AuthoritativeStateTally& after = later.authoritative_state;
  return after.updates < before.updates || after.ticks_missing < before.ticks_missing ||
         after.ticks_out_of_order < before.ticks_out_of_order;
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

// Adds value, at now, to window, drops what is older than the rate window, and
// returns the seconds from its oldest sample to now.
template <typename T>
double Slide(std::deque<std::pair<NetworkPanel::Clock::time_point, T>>& window, NetworkPanel::Clock::time_point now,
             const T& value) {
  window.emplace_back(now, value);
  while (now - window.front().first > NetworkPanel::kRateWindow) {
    window.pop_front();
  }
  return std::chrono::duration<double>(now - window.front().first).count();
}

// The largest correction between two tallies, 0 for none: a new largest if
// there was one, else the last stands for them.
double LargestBetween(const prediction::Corrections& earlier, const prediction::Corrections& later) {
  if (later.count <= earlier.count) {
    return 0.0;
  }
  return later.largest_m > earlier.largest_m ? later.largest_m : later.last_m;
}

// Each of watched unmeasured now, its peak kept.
template <typename Group>
void Unmeasure(const Group& watched) {
  for (auto* each : watched) {
    each->Set(std::nullopt);
  }
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

void Impairment::AddGroup(debug::Panel& panel, std::string_view measured_rtt) const {
  panel.elements.emplace_back(debug::Text{.text = "Impairment, both ways"});
  panel.elements.emplace_back(debug::Text{.text = std::format("Measured RTT: {}", measured_rtt)});
  for (const ImpairmentSlider& slider : kImpairmentSliders) {
    panel.elements.emplace_back(debug::Slider{.id = std::string(slider.id),
                                              .label = std::string(slider.label),
                                              .value = slider.get(values_),
                                              .min = 0.0F,
                                              .max = slider.max,
                                              .whole_numbers = slider.whole_numbers});
  }
  for (const ImpairmentProfile& profile : kImpairmentProfiles) {
    panel.elements.emplace_back(debug::Button{.id = std::string(profile.id), .label = std::string(profile.label)});
  }
  panel.elements.emplace_back(debug::Button{.id = std::string(kImpairmentOffButton), .label = "Off"});
}

void Impairment::Update(const debug::ControlEvent& event) {
  if (const auto* value = std::get_if<float>(&event.value)) {
    const auto slider = std::ranges::find(kImpairmentSliders, event.control, &ImpairmentSlider::id);
    if (slider != kImpairmentSliders.end()) {
      slider->set(values_, std::clamp(*value, 0.0F, slider->max));
    }
    return;
  }
  if (!std::holds_alternative<debug::ButtonPress>(event.value)) {
    return;
  }
  if (event.control == kImpairmentOffButton) {
    values_ = {};
    return;
  }
  const auto profile = std::ranges::find(kImpairmentProfiles, event.control, &ImpairmentProfile::id);
  if (profile != kImpairmentProfiles.end()) {
    values_ = profile->conditions;
  }
}

link::SimulatedConditions Impairment::WantedConditions() const {
  if (values_ == link::SimulatedConditions{}) {
    return {};
  }
  link::SimulatedConditions wanted = values_;
  wanted.applies_to = link::SimulatedTraffic::kSentAndReceived;
  return wanted;
}

bool Impairment::Impaired() const { return WantedConditions() != link::SimulatedConditions{}; }

void NetworkPanel::Watched::Set(std::optional<double> value) {
  now_ = value;
  if (!value.has_value()) {
    return;
  }
  RaisePeak(*value);
}

void NetworkPanel::Watched::RaisePeak(double value) {
  if (!peak_.has_value() || (lower_is_worse_ ? value < *peak_ : value > *peak_)) {
    peak_ = value;
  }
}

std::string NetworkPanel::Watched::Format(std::string (*format)(double), std::string_view peak_label) const {
  if (!now_.has_value() && !peak_.has_value()) {
    return "--";
  }
  const std::string label = peak_label.empty() ? std::string() : std::format("{} ", peak_label);
  return std::format("{} ({}{})", FormatOrDashes(now_, format), label, FormatOrDashes(peak_, format));
}

bool NetworkPanel::SampleDue(Clock::time_point now) const {
  return !readout_.next_sample.has_value() || now >= *readout_.next_sample;
}

void NetworkPanel::Sample(const NetworkSample& sample, Clock::time_point now) {
  std::optional<Clock::time_point>& next_sample = readout_.next_sample;
  if (!next_sample.has_value()) {
    next_sample = now + kSamplePeriod;
  } else if (now >= *next_sample) {
    // The first boundary after now, however many frames skipped.
    next_sample = *next_sample + (((now - *next_sample) / kSamplePeriod) + 1) * kSamplePeriod;
  }
  SampleConnection(sample.connection.stats, now);
  SampleTraffic(sample.connection.traffic, now);
  SampleAuthoritativeState(sample);
  SampleCorrections(sample.corrections, now);
  SampleFrames(now);
}

void NetworkPanel::RecordFrame(std::optional<double> interpolation_slack_s) {
  if (!interpolation_slack_s.has_value()) {
    return;
  }
  Frames& pending = readout_.pending_frames;
  pending.smallest_slack_s =
      std::min(pending.smallest_slack_s.value_or(*interpolation_slack_s), *interpolation_slack_s);
  if (*interpolation_slack_s <= 0.0) {
    ++pending.dry;
  }
}

void NetworkPanel::SampleCorrections(const std::optional<prediction::Corrections>& corrections, Clock::time_point now) {
  Prediction& group = readout_.prediction;
  auto& window = readout_.corrections_window;
  if (!corrections.has_value()) {
    window.clear();
    Unmeasure(group.Corrections());
    return;
  }
  group.correction_m.Set(corrections->last_m);
  // A first sample has nothing before it to tell what is new.
  const double largest_new = window.empty() ? 0.0 : LargestBetween(window.back().second, *corrections);
  group.correction_m.RaisePeak(largest_new);
  Plot(group.correction_plot, largest_new);

  const double seconds = Slide(window, now, *corrections);
  if (seconds <= 0.0) {
    group.corrections_per_sec.Set(std::nullopt);
    group.rifle_corrections_per_sec.Set(std::nullopt);
    return;
  }
  const prediction::Corrections& oldest = window.front().second;
  group.corrections_per_sec.Set(PerSecond(oldest.count, corrections->count, seconds));
  group.rifle_corrections_per_sec.Set(PerSecond(oldest.rifle_count, corrections->rifle_count, seconds));
}

void NetworkPanel::SampleFrames(Clock::time_point now) {
  auto& window = readout_.frames_window;
  const double seconds = Slide(window, now, std::exchange(readout_.pending_frames, {}));
  // The frames since the oldest sample: those recorded before it are older
  // than the window.
  std::optional<double> smallest;
  std::uint32_t dry = 0;
  for (auto frames = std::next(window.begin()); frames != window.end(); ++frames) {
    if (const std::optional<double>& slack = frames->second.smallest_slack_s; slack.has_value()) {
      smallest = std::min(smallest.value_or(*slack), *slack);
    }
    dry += frames->second.dry;
  }
  Prediction& group = readout_.prediction;
  if (seconds <= 0.0 || !smallest.has_value()) {
    Unmeasure(group.Frames());
    return;
  }
  group.smallest_slack_s.Set(smallest);
  group.dry_frames_per_sec.Set(static_cast<double>(dry) / seconds);
}

void NetworkPanel::SampleConnection(const std::optional<link::ConnectionStats>& stats, Clock::time_point now) {
  Connection& connection = readout_.connection;
  connection.stats = stats;
  if (!stats.has_value()) {
    // No connection starts the jitter's peak over, as the Debug HUD's.
    connection.jitter_window.clear();
    Unmeasure(connection.All());
    return;
  }
  Slide(connection.jitter_window, now, JitterMs(*stats));
  connection.quality_local.Set(QualityPercent(stats->quality_local));
  connection.quality_remote.Set(QualityPercent(stats->quality_remote));
  connection.pending_bytes.Set(static_cast<double>(stats->pending_bytes));
  Plots& plots = readout_.plots;
  Plot(plots.rtt_ms, static_cast<double>(stats->ping_ms));
  Plot(plots.jitter_ms, JitterMs(*stats));
  Plot(plots.loss_percent, PacketLossPercent(*stats));
  Plot(plots.in_bytes_per_sec, static_cast<double>(stats->in_bytes_per_sec));
  Plot(plots.out_bytes_per_sec, static_cast<double>(stats->out_bytes_per_sec));
}

void NetworkPanel::SampleTraffic(const link::Traffic& traffic, Clock::time_point now) {
  auto& window = readout_.traffic_window;
  if (!window.empty() && StartedOver(window.back().second, traffic)) {
    window.clear();
  }
  const double seconds = Slide(window, now, traffic);
  const link::Traffic& oldest = window.front().second;
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    TypeRates& rates = readout_.rates.at(i);
    const std::array<std::uint64_t, 4> before = CountsOf(oldest.by_type.at(i));
    const std::array<std::uint64_t, 4> after = CountsOf(traffic.by_type.at(i));
    for (std::size_t column = 0; column < rates.size(); ++column) {
      rates.at(column).Set(seconds > 0.0 ? std::optional(PerSecond(before.at(column), after.at(column), seconds))
                                         : std::nullopt);
    }
  }
}

void NetworkPanel::SampleAuthoritativeState(const NetworkSample& sample) {
  AuthoritativeState& state = readout_.authoritative_state;
  const auto& [oldest_time, oldest] = readout_.traffic_window.front();
  const auto& [newest_time, newest] = readout_.traffic_window.back();
  const double seconds = std::chrono::duration<double>(newest_time - oldest_time).count();
  const link::AuthoritativeStateTally& before = oldest.authoritative_state;
  const link::AuthoritativeStateTally& tally = newest.authoritative_state;
  if (!sample.receives_authoritative_state || seconds <= 0.0) {
    Unmeasure(state.Rates());
  } else {
    state.updates.Set(PerSecond(before.updates, tally.updates, seconds));
    state.ticks_missing.Set(PerSecond(before.ticks_missing, tally.ticks_missing, seconds));
    state.ticks_out_of_order.Set(PerSecond(before.ticks_out_of_order, tally.ticks_out_of_order, seconds));
  }
  state.received = sample.receives_authoritative_state;
  state.tick_rate_hz = sample.tick_rate_hz;
  if (!sample.receives_authoritative_state || tally.updates == 0) {
    Unmeasure(state.Sizes());
  } else {
    state.last_bytes.Set(static_cast<double>(tally.last_bytes));
    state.average_bytes.Set(static_cast<double>(tally.total_bytes) / static_cast<double>(tally.updates));
    state.largest_bytes.Set(static_cast<double>(tally.largest_bytes));
  }
}

debug::Panel NetworkPanel::BuildPanel() const {
  debug::Panel panel{.id = std::string(kNetworkPanelId), .title = "Network", .elements = {}};
  AddConnectionGroup(panel);
  AddTrafficTable(panel);
  AddAuthoritativeState(panel);
  AddPredictionGroup(panel);
  impairment_.AddGroup(panel, RttText(readout_.connection.stats));
  panel.elements.emplace_back(debug::Button{.id = std::string(kResetPeaksButton), .label = "Reset peaks"});
  return panel;
}

void NetworkPanel::AddConnectionGroup(debug::Panel& panel) const {
  const Connection& connection = readout_.connection;
  const std::optional<link::ConnectionStats>& stats = connection.stats;
  // The peak over the last kRateWindow, as the Debug HUD shows it.
  std::optional<double> jitter_ms;
  for (const auto& [time, sample_ms] : connection.jitter_window) {
    if (sample_ms.has_value()) {
      jitter_ms = std::max(jitter_ms.value_or(*sample_ms), *sample_ms);
    }
  }
  const auto stat = [&stats](float link::ConnectionStats::* field) {
    return stats.transform([field](const link::ConnectionStats& s) { return static_cast<double>(s.*field); });
  };

  panel.elements.emplace_back(debug::Text{.text = "Connection"});
  panel.elements.emplace_back(debug::Text{.text = "RTT: " + RttText(stats)});
  panel.elements.emplace_back(debug::Text{.text = "Jitter: " + FormatOrDashes(jitter_ms, FormatMilliseconds)});
  panel.elements.emplace_back(
      debug::Text{.text = "Quality local: " + connection.quality_local.Format(FormatPercent, "worst")});
  panel.elements.emplace_back(
      debug::Text{.text = "Quality remote: " + connection.quality_remote.Format(FormatPercent, "worst")});
  panel.elements.emplace_back(
      debug::Text{.text = "In: " + FormatOrDashes(stat(&link::ConnectionStats::in_bytes_per_sec), FormatRate)});
  panel.elements.emplace_back(
      debug::Text{.text = "Out: " + FormatOrDashes(stat(&link::ConnectionStats::out_bytes_per_sec), FormatRate)});
  panel.elements.emplace_back(debug::Text{.text = "Pending: " + connection.pending_bytes.Format(FormatBytes, "peak")});
  const Plots& plots = readout_.plots;
  panel.elements.emplace_back(PlotOf("RTT (ms)", plots.rtt_ms));
  panel.elements.emplace_back(PlotOf("Jitter (ms)", plots.jitter_ms));
  panel.elements.emplace_back(PlotOf("Loss (%)", plots.loss_percent));
  panel.elements.emplace_back(PlotOf("In (B/s)", plots.in_bytes_per_sec));
  panel.elements.emplace_back(PlotOf("Out (B/s)", plots.out_bytes_per_sec));
}

void NetworkPanel::AddTrafficTable(debug::Panel& panel) const {
  // Each column's format, in TypeRates' order.
  constexpr std::array<std::string (*)(double), 4> kColumnFormats{FormatPerSecond, FormatRate, FormatPerSecond,
                                                                  FormatRate};
  panel.elements.emplace_back(debug::Text{.text = "Traffic per message type, now (peak)"});
  debug::Table table{.header = {"Type", "Msgs/s in", "Bytes/s in", "Msgs/s out", "Bytes/s out"}, .rows = {}};
  for (std::size_t i = 0; i < link::kMessageTypeCount; ++i) {
    std::vector<std::string>& row = table.rows.emplace_back();
    row.emplace_back(link::NameOf(static_cast<link::MessageType>(i)));
    const TypeRates& rates = readout_.rates.at(i);
    for (std::size_t column = 0; column < rates.size(); ++column) {
      row.push_back(rates.at(column).Format(kColumnFormats.at(column)));
    }
  }
  panel.elements.emplace_back(std::move(table));
}

void NetworkPanel::AddAuthoritativeState(debug::Panel& panel) const {
  const AuthoritativeState& state = readout_.authoritative_state;
  const auto per_second = [](double value) { return std::format("{:.1f}/s", value); };
  const auto line = [&panel](std::string_view label, const Watched& watched, std::string (*format)(double)) {
    panel.elements.emplace_back(debug::Text{.text = std::format("{}: {}", label, watched.Format(format, "peak"))});
  };

  panel.elements.emplace_back(debug::Text{.text = "Authoritative State"});
  if (state.received) {
    panel.elements.emplace_back(debug::Text{
        .text = std::format("Updates: {}, tick rate {}", state.updates.Format(per_second, "peak"),
                            state.tick_rate_hz.has_value() ? std::format("{}Hz", *state.tick_rate_hz) : "--")});
  } else {
    panel.elements.emplace_back(debug::Text{.text = "Updates: --"});
  }
  line("Last size", state.last_bytes, FormatBytes);
  line("Average size", state.average_bytes, FormatBytes);
  line("Largest size", state.largest_bytes, FormatBytes);
  line("Ticks missing", state.ticks_missing, per_second);
  line("Ticks out of order", state.ticks_out_of_order, per_second);
}

void NetworkPanel::AddPredictionGroup(debug::Panel& panel) const {
  const Prediction& prediction = readout_.prediction;
  const auto metres = [](double value) { return std::format("{:.2f}m", value); };
  const auto ms = [](double value) { return FormatMilliseconds(value * kMillisecondsPerSecond); };
  panel.elements.emplace_back(debug::Text{.text = "Prediction and presentation"});
  panel.elements.emplace_back(
      debug::Text{.text = "Corrections/s: " + prediction.corrections_per_sec.Format(FormatPerSecond, "peak")});
  panel.elements.emplace_back(debug::Text{.text = "Correction: " + prediction.correction_m.Format(metres, "largest")});
  panel.elements.emplace_back(debug::Text{
      .text = "Rifle corrections/s: " + prediction.rifle_corrections_per_sec.Format(FormatPerSecond, "peak")});
  panel.elements.emplace_back(
      debug::Text{.text = std::format("Slack: {}, delay {:.0f}ms", prediction.smallest_slack_s.Format(ms, "worst"),
                                      presentation::kInterpolationDelay * kMillisecondsPerSecond)});
  panel.elements.emplace_back(
      debug::Text{.text = "Dry frames/s: " + prediction.dry_frames_per_sec.Format(FormatPerSecond, "peak")});
  panel.elements.emplace_back(PlotOf("Correction (m)", prediction.correction_plot));
}

void NetworkPanel::Update(const debug::ControlEvent& event) {
  if (event.panel != kNetworkPanelId) {
    return;
  }
  if (event.control == kResetPeaksButton && std::holds_alternative<debug::ButtonPress>(event.value)) {
    ResetPeaks();
    return;
  }
  impairment_.Update(event);
}

void NetworkPanel::ResetPeaks() {
  const auto reset = [](const auto& group) {
    for (Watched* watched : group) {
      watched->ResetPeak();
    }
  };
  reset(readout_.connection.All());
  for (TypeRates& rates : readout_.rates) {
    for (Watched& watched : rates) {
      watched.ResetPeak();
    }
  }
  reset(readout_.authoritative_state.Rates());
  reset(readout_.authoritative_state.Sizes());
  reset(readout_.prediction.Corrections());
  reset(readout_.prediction.Frames());
}

}  // namespace augusta::client
