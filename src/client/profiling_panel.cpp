#include "profiling_panel.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "augusta/debug_panels.h"
#include "augusta/renderer.h"

namespace augusta::client {

namespace {

constexpr std::string_view kFramesSlider = "frames";
constexpr std::string_view kCaptureButton = "capture";

}  // namespace

ProfilingPanel::ProfilingPanel(renderer::GpuCrashDumpStatus gpu_crash_dumps)
    : gpu_crash_dumps_(std::move(gpu_crash_dumps)) {}

debug::Panel ProfilingPanel::BuildPanel() const {
  debug::Panel panel{.id = std::string(kProfilingPanelId), .title = "Profiling", .elements = {}};
  panel.elements.emplace_back(debug::Slider{.id = std::string(kFramesSlider),
                                            .label = "Frames",
                                            .value = static_cast<float>(frames_),
                                            .min = static_cast<float>(kMinFrames),
                                            .max = static_cast<float>(kMaxFrames),
                                            .whole_numbers = true,
                                            .disabled = Capturing()});
  panel.elements.emplace_back(debug::Button{
      .id = std::string(kCaptureButton), .label = std::format("Capture {} frames", frames_), .disabled = Capturing()});
  panel.elements.emplace_back(debug::Text{
      .text = Capturing() ? std::format("Capturing frame {} of {}", captured_ + 1, frames_) : "Not capturing"});
  panel.elements.emplace_back(
      debug::Text{.text = gpu_crash_dumps_.enabled ? "Aftermath: enabled" : "Aftermath: not enabled"});
  panel.elements.emplace_back(
      debug::Text{.text = gpu_crash_dumps_.attached ? "Attached to the device: yes" : "Attached to the device: no"});
  panel.elements.emplace_back(debug::Text{.text = "Dumps land in: " + gpu_crash_dumps_.directory.string()});
  return panel;
}

CaptureChange ProfilingPanel::Update(const debug::ControlEvent& event) {
  if (event.panel != kProfilingPanelId || Capturing()) {
    return CaptureChange::kNone;
  }
  if (event.control == kCaptureButton && std::holds_alternative<debug::ButtonPress>(event.value)) {
    captured_ = 0;
    return CaptureChange::kOpens;
  }
  if (const auto* value = std::get_if<float>(&event.value); event.control == kFramesSlider && value != nullptr) {
    frames_ = static_cast<int>(
        std::lround(std::clamp(*value, static_cast<float>(kMinFrames), static_cast<float>(kMaxFrames))));
  }
  return CaptureChange::kNone;
}

CaptureChange ProfilingPanel::Tick() {
  if (!Capturing()) {
    return CaptureChange::kNone;
  }
  if (++captured_ < frames_) {
    return CaptureChange::kNone;
  }
  captured_ = -1;
  return CaptureChange::kCloses;
}

}  // namespace augusta::client
