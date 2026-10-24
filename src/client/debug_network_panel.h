#ifndef AUGUSTA_CLIENT_DEBUG_NETWORK_PANEL_H_
#define AUGUSTA_CLIENT_DEBUG_NETWORK_PANEL_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/correction_tally.h"
#include "augusta/debug_panels.h"
#include "augusta/link.h"

/// \file
/// The client's Network Debug panel (ADR-0075), private to the client Runtime
/// and compiled only in a Development build (ADR-0074): the client's own
/// connection, its traffic per message type, the Authoritative State updates
/// it receives, Reconciliation's corrections and the interpolation buffer's
/// slack, as rates, plots and peaks. Its numbers come from modules that ship in
/// every build (link, prediction, presentation), so its builder lives here
/// rather than in them, which may not depend on augusta_debug. It takes plain
/// values only: the Network I/O thread records each round's reading into
/// ConnectionReadings, the Prediction thread publishes its CorrectionTally's
/// counts with its tick, and the Main/Render thread records each frame's slack
/// (RecordFrame) and takes the rest into a NetworkSample whenever the panel
/// says one is due, so the panel's state is the Main/Render thread's alone.
/// What only the server knows stays in Grafana (ADR-0049). Its Impairment
/// group says which of the transport's simulated conditions the developer
/// wants on the client's link, both ways, which the client Runtime applies;
/// the server is never told.
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
  /// The server's tick rate, at which Authoritative State updates should
  /// arrive; nullopt until the server admits the client.
  std::optional<std::uint8_t> tick_rate_hz;
  /// Whether the mode receives Authoritative State: a Replay viewer does not,
  /// and the Authoritative State group reads "--".
  bool receives_authoritative_state = true;
  /// The Prediction thread's CorrectionTally as of its newest tick; nullopt
  /// with no Prediction State (spectating, a Replay viewer).
  std::optional<prediction::Corrections> corrections;
};

/// The Network panel's Impairment group: which of the transport's simulated
/// conditions the developer wants on the client's link, both ways. The client
/// Runtime applies them; the server is never told. Its values are the
/// developer's alone, never written to a config, so they last until Off or
/// the client exits, however often the panel is closed.
class Impairment {
 public:
  /// The group's lines and controls, the measured RTT among them as given
  /// ("42ms" or "--").
  void AddGroup(debug::Panel& panel, std::string_view measured_rtt) const;

  /// A slider sets its one value, within its range; a profile's button sets
  /// them all to the profile's, and Off to none. Any other control changes
  /// nothing. The event is the Network panel's: the panel checks.
  void Update(const debug::ControlEvent& event);

  /// Its values on what the client sends and what it receives, or default
  /// conditions (the real network) while every value is zero.
  [[nodiscard]] link::SimulatedConditions WantedConditions() const;

  /// Whether it wants any condition on the link.
  [[nodiscard]] bool Impaired() const;

 private:
  // applies_to left at its default: WantedConditions says both ways.
  link::SimulatedConditions values_;
};

class NetworkPanel {
 public:
  using Clock = std::chrono::steady_clock;

  static constexpr std::chrono::milliseconds kSamplePeriod{100};
  /// Each plot's points: the last 10 s at kSamplePeriod.
  static constexpr std::size_t kPlotPoints = 100;
  /// What a rate is taken over, and the jitter's peak, as the Debug HUD takes
  /// it (HudNetStats::kJitterWindow).
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
  /// type, the Authoritative State updates, the Prediction and presentation
  /// group, with its plot, the Impairment group, and a "Reset peaks" button.
  [[nodiscard]] debug::Panel BuildPanel() const;

  /// "Reset peaks" restarts every peak from its current value; the Impairment
  /// group's controls go to it (Impairment::Update). Any other event, another
  /// panel's included, changes nothing.
  void Update(const debug::ControlEvent& event);

  /// The Impairment group's (Impairment::WantedConditions).
  [[nodiscard]] link::SimulatedConditions WantedConditions() const { return impairment_.WantedConditions(); }

  /// Whether the Impairment group wants any condition on the link.
  [[nodiscard]] bool Impaired() const { return impairment_.Impaired(); }

  /// The panel is turned off: drops its samples, plots and peaks. The
  /// impairment stays, until Off or the client exits.
  void Close() { readout_ = {}; }

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

    // "300B (peak 2.0KB)", each by format, the peak labelled by peak_label
    // when there is one; "--" for either not measured, alone when neither is.
    [[nodiscard]] std::string Format(std::string (*format)(double), std::string_view peak_label = "") const;

   private:
    bool lower_is_worse_ = false;
    std::optional<double> now_;
    std::optional<double> peak_;
  };

  // One message type's rates, per second, in the table's column order:
  // messages and bytes in, then messages and bytes out.
  using TypeRates = std::array<Watched, 4>;

  // The connection's numbers the group shows as lines.
  struct Connection {
    std::optional<link::ConnectionStats> stats;
    // Each sample's peak jitter in milliseconds, nullopt for not measured,
    // over the last kRateWindow, oldest first.
    std::deque<std::pair<Clock::time_point, std::optional<double>>> jitter_window;
    Watched quality_local{true};
    Watched quality_remote{true};
    Watched pending_bytes;

    std::array<Watched*, 3> All() { return {&quality_local, &quality_remote, &pending_bytes}; }
  };

  // The Authoritative State updates' numbers: rates per second over
  // kRateWindow, sizes in bytes; all nullopt in a mode that receives none.
  struct AuthoritativeState {
    // Whether the mode receives Authoritative State.
    bool received = true;
    std::optional<std::uint8_t> tick_rate_hz;
    Watched updates;
    Watched ticks_missing;
    Watched ticks_out_of_order;
    Watched last_bytes;
    Watched average_bytes;
    Watched largest_bytes;

    std::array<Watched*, 3> Rates() { return {&updates, &ticks_missing, &ticks_out_of_order}; }
    std::array<Watched*, 3> Sizes() { return {&last_bytes, &average_bytes, &largest_bytes}; }
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

    std::array<Watched*, 3> Corrections() { return {&corrections_per_sec, &correction_m, &rifle_corrections_per_sec}; }
    std::array<Watched*, 2> Frames() { return {&smallest_slack_s, &dry_frames_per_sec}; }
  };

  // The frames recorded between two samples.
  struct Frames {
    std::optional<double> smallest_slack_s;
    std::uint32_t dry = 0;
  };

  // What the panel measured since it was turned on, which Close drops.
  struct Readout {
    std::optional<Clock::time_point> next_sample;
    Connection connection;
    Plots plots;
    // The samples' traffic over the last kRateWindow, oldest first.
    std::deque<std::pair<Clock::time_point, link::Traffic>> traffic_window;
    std::array<TypeRates, link::kMessageTypeCount> rates{};
    AuthoritativeState authoritative_state;
    Prediction prediction;
    // The samples' corrections and frames over the last kRateWindow, oldest
    // first: the corrections window empty while there are none.
    std::deque<std::pair<Clock::time_point, prediction::Corrections>> corrections_window;
    std::deque<std::pair<Clock::time_point, Frames>> frames_window;
    // The frames recorded since the last sample.
    Frames pending_frames;
  };

  Readout readout_;
  Impairment impairment_;

  void SampleConnection(const std::optional<link::ConnectionStats>& stats, Clock::time_point now);
  void SampleTraffic(const link::Traffic& traffic, Clock::time_point now);
  // After SampleTraffic, whose window it takes its rates over.
  void SampleAuthoritativeState(const NetworkSample& sample);
  void SampleCorrections(const std::optional<prediction::Corrections>& corrections, Clock::time_point now);
  void SampleFrames(Clock::time_point now);
  void AddConnectionGroup(debug::Panel& panel) const;
  void AddTrafficTable(debug::Panel& panel) const;
  void AddAuthoritativeState(debug::Panel& panel) const;
  void AddPredictionGroup(debug::Panel& panel) const;
  void ResetPeaks();
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_DEBUG_NETWORK_PANEL_H_
