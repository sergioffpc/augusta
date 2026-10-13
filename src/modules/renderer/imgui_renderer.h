#ifndef AUGUSTA_RENDERER_IMGUI_RENDERER_H_
#define AUGUSTA_RENDERER_IMGUI_RENDERER_H_

#include <imgui.h>
#include <nvrhi/nvrhi.h>

/// \file
/// Draws Dear ImGui's output through NVRHI, so the debug HUD takes no
/// backend-specific ImGui renderer (ADR-0059). Adapted from Donut's ImGui_NVRHI (MIT, see imgui_renderer.cpp),
/// private to the module, and enough for the HUD's text alone: it draws every
/// command from the font atlas, and runs no ImGui user callbacks.
namespace augusta::renderer {

class ImGuiRenderer final {
 public:
  /// Uploads the current ImGui context's font atlas, which must have its fonts
  /// added: ImGui's legacy atlas, built once, since this sets no
  /// ImGuiBackendFlags_RendererHasTextures.
  explicit ImGuiRenderer(nvrhi::IDevice& device);

  /// Records draw_data's triangles into commands, over framebuffer's contents.
  void Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer, const ImDrawData& draw_data);

 private:
  void CreatePipeline(const nvrhi::FramebufferInfo& framebuffer_info);
  // Into vertex_buffer_ and index_buffer_, growing them as needed.
  void UploadGeometry(nvrhi::ICommandList& commands, const ImDrawData& draw_data);

  nvrhi::IDevice& device_;
  nvrhi::ShaderHandle vertex_shader_;
  nvrhi::ShaderHandle pixel_shader_;
  nvrhi::InputLayoutHandle input_layout_;
  nvrhi::BindingLayoutHandle binding_layout_;
  nvrhi::TextureHandle font_texture_;
  nvrhi::SamplerHandle font_sampler_;
  nvrhi::BindingSetHandle font_bindings_;
  // Made on the first Render, for its framebuffer's formats.
  nvrhi::GraphicsPipelineHandle pipeline_;
  // Grown as the HUD's text needs, never shrunk.
  nvrhi::BufferHandle vertex_buffer_;
  nvrhi::BufferHandle index_buffer_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_IMGUI_RENDERER_H_
