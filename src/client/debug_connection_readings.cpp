#include "debug_connection_readings.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>

#include "augusta/hud.h"
#include "augusta/link.h"
#include "debug_network_panel.h"

namespace augusta::client {

void DebugConnectionReadings::Record(const std::optional<link::ConnectionStats>& stats,
                                     const std::function<link::Traffic()>& traffic) {
  // Whether the HUD is on or not: the Debug index can switch it on at any frame.
  const std::optional<hud::NetStats> net = hud_net_stats_.Update(stats, std::chrono::steady_clock::now());
  // The Network panel's reading is this round's, the HUD's: the two never disagree.
  const bool network_panel_reads = network_panel_on_.load();
  const link::Traffic round_traffic = network_panel_reads ? traffic() : link::Traffic{};
  const std::lock_guard<std::mutex> lock(mutex_);
  latest_hud_net_ = net;
  if (network_panel_reads) {
    connection_readings_.Record(stats, round_traffic);
  }
}

std::optional<hud::NetStats> DebugConnectionReadings::LatestHudNet() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return latest_hud_net_;
}

ConnectionReading DebugConnectionReadings::Take() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return connection_readings_.Take();
}

void DebugConnectionReadings::SetPanelOn(bool on) {
  if (on) {
    // Readings left from before it was last closed are stale.
    const std::lock_guard<std::mutex> lock(mutex_);
    connection_readings_ = {};
  }
  network_panel_on_.store(on);
}

}  // namespace augusta::client
