#ifndef AUGUSTA_RENDERER_FRAME_REGIONS_H_
#define AUGUSTA_RENDERER_FRAME_REGIONS_H_

#include <cstdint>

// Decision half of the renderer's per-frame upload buffers, kept free of
// Falcor so it is tested on every platform: renderer.cpp does the writing,
// fencing and drawing around it.
namespace augusta::renderer {

/// Which of frames_in_flight regions of a per-frame buffer frame_index writes
/// and draws from. Consecutive frames take consecutive regions, wrapping, so a
/// region comes round again only once every other frame in flight has had its
/// own. frames_in_flight must not be 0.
constexpr std::uint32_t FrameRegion(std::uint64_t frame_index, std::uint32_t frames_in_flight) {
  return static_cast<std::uint32_t>(frame_index % frames_in_flight);
}

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_FRAME_REGIONS_H_
