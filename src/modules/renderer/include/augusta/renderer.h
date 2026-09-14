#ifndef AUGUSTA_RENDERER_H_
#define AUGUSTA_RENDERER_H_

#include <cstdint>
#include <string>

#include "augusta/input.h"

// augusta::renderer wraps NVIDIA Falcor/D3D12 (ADR-0009), used exclusively
// by the Windows client. It also owns the client's single OS window:
// Falcor fuses window creation, GPU device, and swapchain into one object
// (Falcor::SampleApp) rather than offering them as separable pieces, so
// there is no clean seam to split a separate augusta::window module out
// of it. The window/device-event vocabulary (Key, KeyEvent, EventSink,
// ...) lives in augusta::input instead, even though Renderer is the one
// that pushes those events - see input.h's header comment for why the
// dependency has to run this direction and not the other.
//
// PumpEvents and RenderFrame are deliberately two separate calls, not one
// opaque loop, because this engine's threads don't share a cadence
// (ARCHITECTURE.md §8, ADR-0005): PredictionWorld ticks the Simulation
// thread at a fixed rate decoupled from how often a frame is actually
// presented on the Main/Render thread. PumpEvents just drains the
// OS/Falcor event queue (cheap, safe to call often, e.g. once per
// Simulation tick's worth of wall time even though it runs on the
// Main/Render thread); RenderFrame does the actual GPU work and should be
// called at the app's presentation rate instead. The app's own main loop
// (ClientRuntime, not yet designed) decides that split; this module just
// exposes the two primitives.
//
// Interface scope, for now: enough to drive M1's Falcor spike (a
// textured, rotating primitive on screen) and to establish the seam
// PresentationWorld will render through. What RenderFrame actually draws
// - consuming Presentation State - is deliberately not designed yet: that
// type doesn't exist until the ECS (ADR-0001) and PresentationWorld
// (ADR-0021, ADR-0024) are. Revisit this header once those land.
namespace augusta::renderer {

// Default initial client-area size, in pixels (see Config::width/height).
constexpr std::uint32_t kDefaultWidth = 1920;
constexpr std::uint32_t kDefaultHeight = 1080;

// Initial window/renderer parameters.
struct Config {
  // Window title, shown in the OS title bar and taskbar.
  std::string title;
  // Initial client-area size, in pixels. The window is resizable by the
  // user afterward via standard OS chrome.
  std::uint32_t width = kDefaultWidth;
  std::uint32_t height = kDefaultHeight;
};

// A client-area size, in pixels.
struct Size {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// Owns the client's single OS window, GPU device, and swapchain. The
// client constructs exactly one, on the Main/Render thread.
class Renderer {
 public:
  // Creates the window and GPU device per config, and wires this
  // Renderer to push keyboard/mouse events to input_sink as Falcor
  // delivers them (see input::EventSink). Throws std::runtime_error if
  // window or device creation fails - there is no recoverable path for a
  // client that can't render. input_sink must outlive this Renderer.
  Renderer(const Config& config, input::EventSink& input_sink);

  // Drains the OS/Falcor event queue and returns immediately - never
  // waits for or presents a frame. Updates the state ShouldClose/GetSize
  // return, and synchronously calls input_sink's matching method for
  // every keyboard/mouse event seen. Must be called from the same
  // thread that constructed this Renderer (the Main/Render thread); see
  // the header comment for why this is separate from RenderFrame.
  void PumpEvents();

  // True once the user has requested the window close (clicked the
  // title bar's close button, Alt+F4, ...), as of the most recent
  // PumpEvents call. The app's main loop is responsible for actually
  // exiting - Renderer keeps working regardless, until destroyed.
  [[nodiscard]] bool ShouldClose() const;

  // Current client-area size, as of the most recent PumpEvents call.
  [[nodiscard]] Size GetSize() const;

  // Draws and presents one frame - the actual GPU work. Call this at
  // the app's target presentation rate (e.g. vsynced to the display),
  // independently of how often PumpEvents is called. From the
  // Main/Render thread.
  //
  // What gets drawn is not yet part of this interface (see the header
  // comment) - today this only proves Falcor renders M1's test
  // primitive into the window.
  void RenderFrame();

  // Hides the OS cursor and confines/relocks it to this window each
  // frame, for continuous mouselook (as opposed to the free OS cursor a
  // menu/UI would need - no such UI exists yet, so v1 callers enable
  // this once and leave it on). Idempotent. Falcor exposes no such hook
  // itself (ADR-0009's vendored-fork note); this is expected to require
  // a small patch to the vendored copy.
  void SetCursorLocked(bool locked);
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_H_
