#ifndef AUGUSTA_CLIENT_DEBUG_TOOLS_H_
#define AUGUSTA_CLIENT_DEBUG_TOOLS_H_

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nvtx3/nvtx3.hpp>

#include "augusta/debug_index.h"
#include "augusta/debug_interaction.h"
#include "augusta/debug_panels.h"
#include "augusta/hud.h"
#include "augusta/link.h"
#include "augusta/physics_overlays.h"
#include "augusta/presentation.h"
#include "augusta/renderer.h"
#include "client.h"
#include "debug_connection_readings.h"
#include "debug_network_panel.h"
#include "debug_profiling_panel.h"

/// \file
/// The debug tools the client Runtime drives on its Main/Render thread,
/// private to the Runtime and compiled only in a Development build (ADR-0074,
/// ADR-0075): the debug HUD's lines, Debug interaction, the Debug index and the
/// panels it switches - the Profiling panel and the NVTX capture window it
/// opens, the Network panel and the Impairment it applies to the transport, and
/// the Physics overlays. Their state is the Main/Render thread's alone; what
/// the Network I/O thread reads of the connection reaches them through
/// DebugConnectionReadings. The Runtime hands the Renderer what they build and
/// them what the Renderer gives back, and asks the server for the Debug streams
/// the overlays want.
namespace augusta::client {

/// Main/Render thread only: every debug tool's state, which lasts until the
/// client exits.
class DebugTools {
 public:
  /// The Debug index and the Physics overlays start as config says for its
  /// mode; gpu_crash_dumps is the Renderer's, which the Profiling panel shows.
  DebugTools(const RuntimeConfig& config, renderer::GpuCrashDumpStatus gpu_crash_dumps);

  /// Where each frame's window events go, F1 entering and leaving Debug
  /// interaction.
  [[nodiscard]] debug::DebugInteraction& Interaction() { return debug_interaction_; }

  /// Which Physics Debug overlays are on, drawn each frame whether the Physics
  /// panel is shown or not.
  [[nodiscard]] presentation::PhysicsOverlays& Overlays() { return physics_overlays_; }

  /// This frame's HUD lines while the Debug index has the HUD on, and none
  /// while it is off.
  [[nodiscard]] std::vector<std::string> HudLines(DebugConnectionReadings& readings);

  /// The frame's Debug panels, the Debug index in Debug interaction only, then
  /// each panel the index has on, in or out of it.
  [[nodiscard]] debug::Panels Panels() const;

  /// Tells the Network I/O thread whether to record the Network panel's
  /// readings as the Debug index has the panel on, and hands the panel a
  /// sample each frame that crosses one of its boundaries, called for only
  /// then.
  void SampleNetworkPanel(DebugConnectionReadings& readings, const std::function<NetworkSample()>& sample);

  /// The frame's interpolation slack, for the Network panel's next sample
  /// while the Debug index has it on.
  void RecordNetworkPanelFrame(const DebugConnectionReadings& readings, const presentation::State& frame_state);

  /// Once per frame after its Main/Render Frame range closes: the frame has
  /// ended for the Profiling panel's capture window.
  void TickProfilingPanel();

  /// Routes each control event the last RenderFrame's panels gave to the area
  /// whose panel it names, calling shot_judgements_changed whenever one changes
  /// whether the overlays want Shot judgements, then applies the Impairment.
  void RouteControlEvents(const std::vector<debug::ControlEvent>& events,
                          const std::function<void()>& shot_judgements_changed);

 private:
  // Applies the conditions the Network panel's Impairment group wants to the
  // client's one connection, both ways, when they change. The server is never
  // told: it measures the link as it is.
  void ApplyImpairment();

  // Opens or closes the capture window's NVTX range as the Profiling panel
  // says.
  void ApplyCaptureChange(CaptureChange change);

  // The frames the HUD's FPS averages.
  hud::FrameTimeWindow frame_times_;
  debug::DebugInteraction debug_interaction_;
  // The Debug index's switches.
  debug::DebugIndex debug_index_;
  // The Profiling panel, and the capture window it has open, if any.
  ProfilingPanel profiling_panel_;
  std::optional<nvtx3::unique_range> capture_range_;
  presentation::PhysicsOverlays physics_overlays_;
  // The Network panel, which samples the connection readings while the Debug
  // index has it on.
  NetworkPanel network_panel_;
  // The conditions last applied to the transport from the Network panel's
  // Impairment group; the real network at start.
  link::SimulatedConditions applied_conditions_;
};

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_DEBUG_TOOLS_H_
