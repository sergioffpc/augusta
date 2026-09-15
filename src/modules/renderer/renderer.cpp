#include "augusta/renderer.h"

namespace augusta::renderer {

// TODO(sergioffpc): every method below is a placeholder - NVIDIA Falcor
// isn't wired in yet (ADR-0009). Just enough is defined here for callers
// to construct/link against this module; there is no real window/GPU
// device behind any of it, so ShouldClose() never becomes true on its
// own - callers can't rely on this loop terminating by itself yet.

Renderer::Renderer([[maybe_unused]] const Config& config, [[maybe_unused]] input::EventSink& input_sink) {
  // TODO(sergioffpc): create the Falcor window/GPU device/swapchain per
  // config, and forward its keyboard/mouse events to input_sink.
}

void Renderer::PumpEvents() {
  // TODO(sergioffpc): drain the OS/Falcor event queue.
}

bool Renderer::ShouldClose() const {
  // TODO(sergioffpc): report whether the (not yet real) window's close
  // was requested.
  return false;
}

Size Renderer::GetSize() const {
  // TODO(sergioffpc): report the (not yet real) window's client-area
  // size.
  return {};
}

void Renderer::RenderFrame() {
  // TODO(sergioffpc): draw and present one frame via Falcor.
}

void Renderer::SetCursorLocked([[maybe_unused]] bool locked) {
  // TODO(sergioffpc): hide/confine the OS cursor to this window.
}

}  // namespace augusta::renderer
