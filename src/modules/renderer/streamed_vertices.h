#ifndef AUGUSTA_RENDERER_STREAMED_VERTICES_H_
#define AUGUSTA_RENDERER_STREAMED_VERTICES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <nvrhi/nvrhi.h>

#include "frame_regions.h"
#include "vulkan_swapchain.h"

/// \file
/// The Renderer's per-frame vertex buffers (ADR-0059), private to the module:
/// what the CPU rewrites every frame - the remote players, the fight's effects,
/// the overlay and, in a Development build, the Debug overlays' shapes - written
/// straight into GPU-visible memory with no GPU wait. renderer.cpp waits for a
/// region to be free before writing there and draws from it.
namespace augusta::renderer {

/// A vertex buffer of Vertex in memory the CPU writes, mapped once for good.
///
/// It holds kFramesInFlight regions of region_capacity vertices each, and a
/// frame writes and draws only the region FrameRegion gives it: several frames
/// are in flight, and rewriting the one region a previous frame is still
/// drawing from tears that frame's drawing. The caller waits for the GPU to
/// finish the last frame drawn from a region before writing there
/// (Renderer::Impl::ReadyRegion).
///
/// It grows into a new buffer only when a call needs more room than any before
/// it, and never shrinks. Frames in flight keep drawing from the old one, which
/// NVRHI keeps alive until they are done, so growing waits for nothing either.
template <typename Vertex>
struct StreamedVertices {
  static constexpr std::uint32_t kFramesInFlight = VulkanSwapchain::kFramesInFlight;

  nvrhi::BufferHandle buffer;
  std::span<Vertex> mapped;
  std::uint32_t region_capacity = 0;
  std::uint32_t count = 0;
  std::uint32_t region = 0;

  /// Writes vertices into frame_region, first growing the buffer to fit them
  /// if a region holds fewer. Nothing is drawn if there are none.
  void Write(nvrhi::IDevice& device, const std::vector<Vertex>& vertices, std::uint32_t frame_region) {
    count = static_cast<std::uint32_t>(vertices.size());
    region = frame_region;
    if (vertices.empty()) {
      return;
    }
    if (count > region_capacity) {
      const std::size_t capacity = std::size_t{count} * kFramesInFlight;
      buffer = device.createBuffer(nvrhi::BufferDesc()
                                       .setByteSize(capacity * sizeof(Vertex))
                                       .setIsVertexBuffer(true)
                                       .setCpuAccess(nvrhi::CpuAccessMode::Write)
                                       .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                       .setKeepInitialState(true)
                                       .setDebugName("Streamed vertices"));
      auto* data = static_cast<Vertex*>(device.mapBuffer(buffer, nvrhi::CpuAccessMode::Write));
      if (data == nullptr) {
        throw std::runtime_error("Mapping a per-frame vertex buffer failed");
      }
      mapped = {data, capacity};
      region_capacity = count;
    }
    std::ranges::copy(vertices, mapped.subspan(FirstVertex(), count).begin());
  }

  /// Where region starts in buffer, in vertices.
  [[nodiscard]] std::uint32_t FirstVertex() const { return region * region_capacity; }
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_STREAMED_VERTICES_H_
