#include "debug_tools.h"

#include <functional>
#include <string>
#include <vector>

#include "augusta/debug_index.h"
#include "augusta/debug_interaction.h"
#include "augusta/debug_panels.h"
#include "augusta/hud.h"
#include "augusta/input.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/physics_overlays.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"
#include "client.h"
#include "debug_connection_readings.h"
#include "debug_index_entries.h"
#include "debug_network_panel.h"
#include "debug_profiling_panel.h"

namespace augusta::client {

namespace {

// The NVTX range, in the default domain, that `nsys profile
// --capture-range=nvtx --nvtx-capture=augusta.capture` records alone.
constexpr const char* kCaptureRange = "augusta.capture";

// What config runs the client as, for which debug tools it carries.
ClientMode ModeOf(const RuntimeConfig& config) {
  return client::ModeOf(config.replay.has_value(), config.reenactment.has_value());
}

// The Debug index's entries for config's mode, as config starts them.
std::vector<debug::IndexEntry> DebugIndexEntries(const RuntimeConfig& config) {
  return DebugIndexEntries(
      DebugIndexStart{
          .hud = config.debug_hud,
          .profiling_panel = config.debug_profiling_panel,
          .network_panel = config.debug_network_panel,
          .physics_panel = config.debug_physics.panel,
      },
      ModeOf(config));
}

// Which Physics overlays start on for config's mode.
presentation::PhysicsOverlaysStart PhysicsOverlaysStartOf(const RuntimeConfig& config) {
  return client::PhysicsOverlaysStartOf(config.debug_physics, ModeOf(config));
}

}  // namespace

DebugTools::DebugTools(const RuntimeConfig& config, renderer::GpuCrashDumpStatus gpu_crash_dumps)
    : debug_interaction_({.toggle = input::kReservedKey, .leave = input::kReleaseCursorKey}),
      debug_index_(DebugIndexEntries(config)),
      profiling_panel_(gpu_crash_dumps),
      physics_overlays_(PhysicsOverlaysStartOf(config)) {}

std::vector<std::string> DebugTools::HudLines(DebugConnectionReadings& readings) {
  // Each frame ends the last one for the FPS the HUD reads, shown or not, so
  // the HUD switched on reads the frames before it.
  frame_times_.Tick(hud::FrameTimeWindow::Clock::now());
  if (!debug_index_.IsOn(kDebugHudEntry)) {
    return {};
  }
  // Impaired while the Network panel's Impairment group says so, the panel
  // shown or not, connected or not.
  return hud::Lines(frame_times_.Average(), readings.LatestHudNet(), network_panel_.Impaired());
}

debug::Panels DebugTools::Panels() const {
  debug::Panels panels;
  if (debug_interaction_.In()) {
    panels.push_back(debug_index_.BuildPanel());
  }
  if (debug_index_.IsOn(kProfilingPanelId)) {
    panels.push_back(profiling_panel_.BuildPanel());
  }
  if (debug_index_.IsOn(kNetworkPanelId)) {
    panels.push_back(network_panel_.BuildPanel());
  }
  if (debug_index_.IsOn(presentation::kPhysicsPanelId)) {
    panels.push_back(physics_overlays_.BuildPanel());
  }
  return panels;
}

void DebugTools::SampleNetworkPanel(DebugConnectionReadings& readings, const std::function<NetworkSample()>& sample) {
  const bool on = debug_index_.IsOn(kNetworkPanelId);
  if (on != readings.PanelOn()) {
    if (!on) {
      network_panel_.Close();
    }
    readings.SetPanelOn(on);
  }
  if (const auto now = NetworkPanel::Clock::now(); on && network_panel_.SampleDue(now)) {
    network_panel_.Sample(sample(), now);
  }
}

void DebugTools::RecordNetworkPanelFrame(const DebugConnectionReadings& readings,
                                         const presentation::State& frame_state) {
  if (readings.PanelOn()) {
    network_panel_.RecordFrame(frame_state.interpolation_slack);
  }
}

void DebugTools::TickProfilingPanel() { ApplyCaptureChange(profiling_panel_.Tick()); }

void DebugTools::RouteControlEvents(const std::vector<debug::ControlEvent>& events,
                                    const std::function<void()>& shot_judgements_changed) {
  for (const debug::ControlEvent& event : events) {
    if (event.panel == debug::kDebugIndexPanelId) {
      debug_index_.Update(event);
    } else if (event.panel == kProfilingPanelId) {
      ApplyCaptureChange(profiling_panel_.Update(event));
    } else if (event.panel == kNetworkPanelId) {
      network_panel_.Update(event);
    } else if (event.panel == presentation::kPhysicsPanelId) {
      const bool wanted = physics_overlays_.WantsShotJudgements();
      physics_overlays_.Update(event);
      if (physics_overlays_.WantsShotJudgements() != wanted) {
        shot_judgements_changed();
      }
    }
  }
  ApplyImpairment();
}

void DebugTools::ApplyImpairment() {
  const link::SimulatedConditions wanted = network_panel_.WantedConditions();
  if (wanted != applied_conditions_) {
    link::SimulateNetworkConditions(wanted);
    applied_conditions_ = wanted;
  }
}

void DebugTools::ApplyCaptureChange(CaptureChange change) {
  switch (change) {
    case CaptureChange::kNone:
      break;
    case CaptureChange::kOpens:
      capture_range_.emplace(kCaptureRange);
      LOG_INFO("subsystem=clientruntime event=profiling_capture_opened");
      break;
    case CaptureChange::kCloses:
      capture_range_.reset();
      LOG_INFO("subsystem=clientruntime event=profiling_capture_closed");
      break;
  }
}

}  // namespace augusta::client
