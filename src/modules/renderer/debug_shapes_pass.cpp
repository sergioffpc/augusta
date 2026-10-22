#include "debug_shapes_pass.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <nvrhi/nvrhi.h>

#include "augusta/debug_shapes.h"
#include "augusta/math.h"

// The shaders' SPIR-V, compiled at build time (CMakeLists.txt).
#include "kDebugShapesPs.h"
#include "kDebugShapesVs.h"

namespace augusta::renderer {

namespace {

// Which of DebugShapesPass's batches draws what, in the order they are drawn.
constexpr std::size_t kDepthTestedTriangles = 0;
constexpr std::size_t kDepthTestedLines = 1;
constexpr std::size_t kThroughWallsTriangles = 2;
constexpr std::size_t kThroughWallsLines = 3;

static_assert(sizeof(debug::Vertex) == (2 * sizeof(math::Vec3)) + sizeof(float),
              "debug::Vertex must be two packed float triples and a float");

// debug::Vertex's layout, as DebugShapes.3d.slang's VsIn reads it.
std::array<nvrhi::VertexAttributeDesc, 3> VertexAttributes() {
  const auto attribute = [](const char* semantic, nvrhi::Format format, std::size_t offset) {
    return nvrhi::VertexAttributeDesc()
        .setName(semantic)
        .setFormat(format)
        .setOffset(static_cast<std::uint32_t>(offset))
        .setElementStride(sizeof(debug::Vertex));
  };
  return {attribute("POSITION", nvrhi::Format::RGB32_FLOAT, offsetof(debug::Vertex, position)),
          attribute("COLOR", nvrhi::Format::RGB32_FLOAT, offsetof(debug::Vertex, color)),
          attribute("ALPHA", nvrhi::Format::R32_FLOAT, offsetof(debug::Vertex, alpha))};
}

// Laid over what is behind by its alpha, leaving the frame opaque, and seen
// from both sides; it writes no depth, so a shape hides no other. Unless it
// shows through walls, it is hidden by what is in front of it, a line lying on
// a face included.
nvrhi::RenderState ShapeState(bool through_walls) {
  nvrhi::RenderState state;
  state.rasterState.setCullNone();
  state.blendState.setRenderTarget(0, nvrhi::BlendState::RenderTarget()
                                          .setBlendEnable(true)
                                          .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                                          .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                                          .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                                          .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha));
  state.depthStencilState.disableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
  if (through_walls) {
    state.depthStencilState.disableDepthTest();
  }
  return state;
}

nvrhi::ShaderHandle CreateShader(nvrhi::IDevice& device, nvrhi::ShaderType type, std::span<const std::uint32_t> spirv) {
  return device.createShader(nvrhi::ShaderDesc().setShaderType(type), spirv.data(), spirv.size_bytes());
}

}  // namespace

DebugShapesPass::DebugShapesPass(nvrhi::IDevice& device, nvrhi::IBindingLayout& binding_layout,
                                 const nvrhi::FramebufferInfo& framebuffer_info)
    : device_(device) {
  const nvrhi::ShaderHandle vertex_shader = CreateShader(device, nvrhi::ShaderType::Vertex, kDebugShapesVs);
  const nvrhi::ShaderHandle pixel_shader = CreateShader(device, nvrhi::ShaderType::Pixel, kDebugShapesPs);
  const std::array<nvrhi::VertexAttributeDesc, 3> attributes = VertexAttributes();
  const nvrhi::InputLayoutHandle input_layout =
      device.createInputLayout(attributes.data(), static_cast<std::uint32_t>(attributes.size()), vertex_shader);
  const auto create = [&](nvrhi::PrimitiveType primitives, bool through_walls) {
    nvrhi::GraphicsPipelineDesc desc;
    desc.setPrimType(primitives)
        .setInputLayout(input_layout)
        .setVertexShader(vertex_shader)
        .setPixelShader(pixel_shader)
        .addBindingLayout(&binding_layout)
        .setRenderState(ShapeState(through_walls));
    return device.createGraphicsPipeline(desc, framebuffer_info);
  };
  batches_[kDepthTestedTriangles].pipeline = create(nvrhi::PrimitiveType::TriangleList, false);
  batches_[kDepthTestedLines].pipeline = create(nvrhi::PrimitiveType::LineList, false);
  batches_[kThroughWallsTriangles].pipeline = create(nvrhi::PrimitiveType::TriangleList, true);
  batches_[kThroughWallsLines].pipeline = create(nvrhi::PrimitiveType::LineList, true);
}

void DebugShapesPass::Write(const debug::Shapes& shapes, std::uint32_t region) {
  const debug::Tessellation tessellation = debug::Tessellate(shapes);
  batches_[kDepthTestedTriangles].vertices.Write(device_, tessellation.depth_tested.triangles, region);
  batches_[kDepthTestedLines].vertices.Write(device_, tessellation.depth_tested.lines, region);
  batches_[kThroughWallsTriangles].vertices.Write(device_, tessellation.through_walls.triangles, region);
  batches_[kThroughWallsLines].vertices.Write(device_, tessellation.through_walls.lines, region);
}

void DebugShapesPass::Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer,
                             nvrhi::IBindingSet& per_frame) const {
  for (const Batch& batch : batches_) {
    if (batch.vertices.count == 0) {
      continue;
    }
    nvrhi::GraphicsState state;
    state.setPipeline(batch.pipeline)
        .setFramebuffer(&framebuffer)
        .addBindingSet(&per_frame)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(batch.vertices.buffer).setSlot(0).setOffset(0));
    state.viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());
    commands.setGraphicsState(state);
    commands.draw(nvrhi::DrawArguments()
                      .setVertexCount(batch.vertices.count)
                      .setStartVertexLocation(batch.vertices.FirstVertex()));
  }
}

}  // namespace augusta::renderer
