#ifndef AUGUSTA_RENDERER_DEBUG_SHAPES_PASS_H_
#define AUGUSTA_RENDERER_DEBUG_SHAPES_PASS_H_

#include <array>
#include <cstdint>

#include <nvrhi/nvrhi.h>

#include "augusta/debug_shapes.h"
#include "streamed_vertices.h"

/// \file
/// The Renderer's Debug overlays (ADR-0075), private to the module and
/// compiled in a Development build only (ADR-0074): the shapes it is handed,
/// tessellated by augusta::debug into lines and triangles, drawn alpha-blended
/// over the frame with DebugShapes.3d.slang, those that show through walls
/// without the depth test. It keeps no shape between frames: each frame's
/// are written into that frame's region of its per-frame buffers, which
/// Renderer::Impl hands it free.
namespace augusta::renderer {

class DebugShapesPass final {
 public:
  /// device must outlive this pass. binding_layout is the one constant buffer
  /// of the camera's view-projection, framebuffer_info what it draws into.
  DebugShapesPass(nvrhi::IDevice& device, nvrhi::IBindingLayout& binding_layout,
                  const nvrhi::FramebufferInfo& framebuffer_info);

  /// Replaces the shapes Render draws with shapes, written into region, which
  /// no frame in flight may still be drawing from.
  void Write(const debug::Shapes& shapes, std::uint32_t region);

  /// Records the shapes last written into commands, over framebuffer's
  /// contents and against its depth, from the view-projection per_frame binds.
  void Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer, nvrhi::IBindingSet& per_frame) const;

 private:
  // One draw: a pipeline and the vertices it draws.
  struct Batch {
    nvrhi::GraphicsPipelineHandle pipeline;
    StreamedVertices<debug::Vertex> vertices;
  };

  nvrhi::IDevice& device_;
  // In the order drawn: the depth-tested triangles, then their lines, then the
  // through-walls ones, so a line shows over the face it outlines and what
  // shows through walls over everything.
  std::array<Batch, 4> batches_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_DEBUG_SHAPES_PASS_H_
