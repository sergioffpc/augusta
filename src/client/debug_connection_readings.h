#ifndef AUGUSTA_CLIENT_DEBUG_CONNECTION_READINGS_H_
#define AUGUSTA_CLIENT_DEBUG_CONNECTION_READINGS_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

#include "augusta/hud.h"
#include "augusta/link.h"
#include "debug_network_panel.h"
#include "net_stats.h"

/// \file
/// The connection's readings the client Runtime's debug tools show, private to
/// the Runtime and compiled only in a Development build (ADR-0074): what the
/// Network I/O thread reads of the connection each round of the Runner's
/// network work (Record), handed over to the Main/Render thread, which shows
/// them in the debug HUD and the Network panel (debug_tools.h). It is the one
/// piece of the debug tooling two threads share; everything else of it is the
/// Main/Render thread's alone.
namespace augusta::client {

/// The debug HUD's and the Network panel's readings of the connection, written
/// by the Network I/O thread and read by the Main/Render thread. They should be
/// consistent with each other, so - like the Runtime's latest tick - they are
/// mutex-guarded copies.
class DebugConnectionReadings {
 public:
  /// Network I/O thread only, once per round: what the HUD shows of stats and,
  /// while the Network panel is on, the panel's reading of stats and traffic,
  /// which is called for only then.
  void Record(const std::optional<link::ConnectionStats>& stats, const std::function<link::Traffic()>& traffic);

  /// Connection numbers for the debug HUD, read once per frame.
  [[nodiscard]] std::optional<hud::NetStats> LatestHudNet();

  /// The Network panel's readings recorded since the last Take.
  [[nodiscard]] ConnectionReading Take();

  /// Whether the Network panel's readings are recorded.
  [[nodiscard]] bool PanelOn() const { return network_panel_on_.load(); }

  /// Main/Render thread only: records the Network panel's readings from now on
  /// while on, those left from before it was last closed dropped.
  void SetPanelOn(bool on);

 private:
  // Guards the connection's readings below, the debug HUD's and the Network
  // panel's.
  std::mutex mutex_;
  std::optional<hud::NetStats> latest_hud_net_;
  // The Network panel's readings of the connection, from the same rounds as
  // latest_hud_net_, recorded only while network_panel_on_: a closed panel
  // costs nothing beyond the Link's counting.
  ConnectionReadings connection_readings_;
  std::atomic<bool> network_panel_on_{false};

  // Network I/O thread only: what the HUD shows of the connection.
  HudNetStats hud_net_stats_;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_DEBUG_CONNECTION_READINGS_H_
