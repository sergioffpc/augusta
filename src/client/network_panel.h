#ifndef AUGUSTA_CLIENT_NETWORK_PANEL_H_
#define AUGUSTA_CLIENT_NETWORK_PANEL_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>

#include "augusta/correction_tally.h"
#include "augusta/debug_panels.h"
#include "augusta/link.h"

/// \file
/// The client's Network Debug panel (ADR-0075), private to the client Runtime
/// and compiled only in a Development build (ADR-0074): the client's own
/// connection, its traffic per message type, Reconciliation's corrections and
/// the interpolation buffer's slack, as rates, plots and peaks. Its numbers
/// come from modules that ship in every build (link, prediction,
/// presentation), so its builder lives here rather than in them, which may not
/// depend on augusta_debug. It takes plain values only: the Network I/O thread
/// records each round's reading into ConnectionReadings, the Prediction thread
/// publishes its CorrectionTally's counts with its tick, and the Main/Render
/// thread records each frame's slack (RecordFrame) and takes the rest into a
/// NetworkSample whenever the panel says one is due, so the panel's state is
/// the Main/Render thread's alone. What only the server knows stays in
/// Grafana (ADR-0049). Its Impairment group says which of the transport's
/// simulated conditions the developer wants on the client's link, both ways,
/// which the client Runtime applies; the server is never told.
namespace augusta::client {

/// The Network panel's Panel id, which its ControlEvents name.
inline constexpr std::string_view kNetworkPanelId = "network";

/// One reading of the connection, as the Network I/O thread makes it.
struct ConnectionReading {
  /// nullopt while not connected. max_jitter_us is the peak since the last
  /// ConnectionReadings::Take, not since the last read of the transport.
  std::optional<link::ConnectionStats> stats;
  link::Traffic traffic;
};

/// The Network I/O thread's readings between two of the panel's samples:
/// ConnectionStats' jitter is a high-water mark each read clears, and the
/// thread reads it far more often than the panel samples, so the peak is kept
/// here. Not thread-safe: the client Runtime guards it.
class ConnectionReadings {
 public:
  /// One round's reading; stats nullopt while not connected, which forgets
  /// the jitter peak.
  void Record(const std::optional<link::ConnectionStats>& stats, const link::Traffic& traffic);

  /// The newest reading, its jitter the peak since the last Take, which
  /// starts the peak over.
  [[nodiscard]] ConnectionReading Take();

 private:
  ConnectionReading newest_;
};

/// What the client Runtime hands the panel at each sample.
struct NetworkSample {
  ConnectionReading connection;
  /// The Prediction thread's CorrectionTally as of its newest tick; nullopt
  /// with no Prediction State (spectating, a Replay viewer).
  std::optional<prediction::Corrections> corrections;
};

class NetworkPanel {
 public:
  using Clock = std::chrono::steady_clock;

  static constexpr std::chrono::milliseconds kSamplePeriod{100};
  /// Each plot's points: the last 10 s at kSamplePeriod.
  static constexpr std::size_t kPlotPoints = 100;
  /// What a rate is taken over.
  static constexpr std::chrono::seconds kRateWindow{1};

  /// Whether a frame at now crosses the next kSamplePeriod boundary since the
  /// first sample: always before the first, and after Close.
  [[nodiscard]] bool SampleDue(Clock::time_point now) const;

  /// Takes sample, at now, into the rates, plots and peaks, with the frames
  /// recorded since the last sample.
  void Sample(const NetworkSample& sample, Clock::time_point now);

  /// One Main/Render frame's interpolation slack (presentation::State), for
  /// the next sample; nullopt with none (outside a Match, no remote player).
  void RecordFrame(std::optional<double> interpolation_slack_s);

  /// The panel: the Connection group, with its plots, the traffic per message
  /// type, the Prediction and presentation group, with its plot, the
  /// Impairment group, and a "Reset peaks" button.
  [[nodiscard]] debug::Panel BuildPanel() const;

  /// "Reset peaks" restarts every peak from its current value. An Impairment
  /// slider sets its one value, within its range; a profile's button sets
  /// them all to the profile's, and Off to none. Any other event, another
  /// panel's included, changes nothing.
  void Update(const debug::ControlEvent& event);

  /// The conditions the Impairment group wants on the client's link: its
  /// values on what the client sends and what it receives, or default
  /// conditions (the real network) while every value is zero.
  [[nodiscard]] link::SimulatedConditions WantedConditions() const;

  /// Whether the Impairment group wants any condition on the link.
  [[nodiscard]] bool Impaired() const;

  /// The panel is turned off: drops its samples, plots and peaks. The
  /// impairment stays, until Off or the client exits.
  void Close();

 private:
  // A number and its peak since the panel opened or its peaks were reset;
  // either nullopt until measured. The peak is the largest, or the smallest
  // where lower is worse.
  class Watched {
   public:
    Watched() = default;
    explicit Watched(bool lower_is_worse) : lower_is_worse_(lower_is_worse) {}

    void Set(std::optional<double> value);
    // A value reached between two Sets: a peak, not the number now.
    void RaisePeak(double value);
    void ResetPeak() { peak_ = now_; }

    [[nodiscard]] const std::optional<double>& Now() const { return now_; }
    [[nodiscard]] const std::optional<double>& Peak() const { return peak_; }

   private:
    bool lower_is_worse_ = false;
    std::optional<double> now_;
    std::optional<double> peak_;
  };

  // One message type's rates, per second, each way.
  struct TypeRates {
    Watched messages_in;
    Watched bytes_in;
    Watched messages_out;
    Watched bytes_out;
  };

  // The connection's numbers the group shows as lines.
  struct Connection {
    std::optional<link::ConnectionStats> stats;
    Watched quality_local{true};
    Watched quality_remote{true};
    Watched pending_bytes;
  };

  // The plotted series, oldest first, each at most kPlotPoints long.
  struct Plots {
    std::deque<float> rtt_ms;
    std::deque<float> jitter_ms;
    std::deque<float> loss_percent;
    std::deque<float> in_bytes_per_sec;
    std::deque<float> out_bytes_per_sec;
  };

  // The Prediction and presentation group's numbers.
  struct Prediction {
    Watched corrections_per_sec;
    // Now the last correction's size, its peak the largest since the reset.
    Watched correction_m;
    Watched rifle_corrections_per_sec;
    Watched smallest_slack_s{true};
    Watched dry_frames_per_sec;
    // The largest correction of each sample, 0 for none, oldest first.
    std::deque<float> correction_plot;
  };

  // The frames recorded between two samples.
  struct Frames {
    std::optional<double> smallest_slack_s;
    std::uint32_t dry = 0;
  };

  std::optional<Clock::time_point> next_sample_;
  Connection connection_;
  Plots plots_;
  // The samples' traffic over the last kRateWindow, oldest first.
  std::deque<std::pair<Clock::time_point, link::Traffic>> traffic_window_;
  std::array<TypeRates, link::kMessageTypeCount> rates_{};
  Prediction prediction_;
  // The samples' corrections and frames over the last kRateWindow, oldest
  // first: the corrections window empty while there are none.
  std::deque<std::pair<Clock::time_point, prediction::Corrections>> corrections_window_;
  std::deque<std::pair<Clock::time_point, Frames>> frames_window_;
  // The frames recorded since the last sample.
  Frames pending_frames_;
  // The Impairment group's values, applies_to left at its default:
  // WantedConditions says both ways.
  link::SimulatedConditions impairment_;

  void SampleConnection(const std::optional<link::ConnectionStats>& stats);
  void SampleTraffic(const link::Traffic& traffic, Clock::time_point now);
  void SampleCorrections(const std::optional<prediction::Corrections>& corrections, Clock::time_point now);
  void SampleFrames(Clock::time_point now);
  void AddPredictionGroup(debug::Panel& panel) const;
  void ResetPeaks();
  void AddImpairmentGroup(debug::Panel& panel) const;
  void UpdateImpairment(const debug::ControlEvent& event);
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_NETWORK_PANEL_H_
