// Adapted from Donut's src/app/imgui_nvrhi.cpp
// (https://github.com/NVIDIA-RTX/Donut), under the MIT licence:
//
// Copyright (c) 2014-2025, NVIDIA CORPORATION. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "imgui_renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include "kImGuiPs.h"
#include "kImGuiVs.h"

namespace augusta::renderer {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

// ImGui's own blend: text's alpha over what is drawn, and a target alpha that
// stays opaque where it was.
nvrhi::BlendState::RenderTarget AlphaBlend() {
  return nvrhi::BlendState::RenderTarget()
      .setBlendEnable(true)
      .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
      .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
      .setSrcBlendAlpha(nvrhi::BlendFactor::One)
      .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
}

nvrhi::ShaderHandle CreateShader(nvrhi::IDevice& device, nvrhi::ShaderType type, std::span<const std::uint32_t> spirv) {
  return device.createShader(nvrhi::ShaderDesc().setShaderType(type), spirv.data(), spirv.size_bytes());
}

nvrhi::TextureHandle UploadFontAtlas(nvrhi::IDevice& device) {
  ImGuiIO& io = ImGui::GetIO();
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  if (pixels == nullptr) {
    throw std::runtime_error("Building ImGui's font atlas failed");
  }
  nvrhi::TextureHandle texture = device.createTexture(nvrhi::TextureDesc()
                                                          .setWidth(static_cast<std::uint32_t>(width))
                                                          .setHeight(static_cast<std::uint32_t>(height))
                                                          .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                          .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                          .setKeepInitialState(true)
                                                          .setDebugName("ImGui font atlas"));
  const nvrhi::CommandListHandle commands = device.createCommandList();
  commands->open();
  commands->writeTexture(texture, 0, 0, pixels, static_cast<std::size_t>(width) * kBytesPerPixel);
  commands->close();
  device.executeCommandList(commands);
  // Draw commands name their texture by this; all of them name the atlas.
  io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.Get())));
  return texture;
}

nvrhi::BufferDesc VertexBufferDesc() {
  return nvrhi::BufferDesc()
      .setIsVertexBuffer(true)
      .setInitialState(nvrhi::ResourceStates::VertexBuffer)
      .setKeepInitialState(true)
      .setDebugName("ImGui vertices");
}

nvrhi::BufferDesc IndexBufferDesc() {
  return nvrhi::BufferDesc()
      .setIsIndexBuffer(true)
      .setInitialState(nvrhi::ResourceStates::IndexBuffer)
      .setKeepInitialState(true)
      .setDebugName("ImGui indices");
}

// A buffer of at least size bytes: buffer, if it is big enough, or a new one
// like desc with room to grow.
nvrhi::BufferHandle Reserve(nvrhi::IDevice& device, nvrhi::BufferHandle buffer, nvrhi::BufferDesc desc,
                            std::size_t size) {
  if (buffer != nullptr && buffer->getDesc().byteSize >= size) {
    return buffer;
  }
  return device.createBuffer(desc.setByteSize(size * 2));
}

// command's clip rectangle in framebuffer pixels, or nullopt if it is empty:
// an empty scissor with a non-empty viewport is a debug-layer warning.
std::optional<nvrhi::Rect> Scissor(const ImDrawCmd& command, const ImDrawData& draw_data) {
  const ImVec2 origin = draw_data.DisplayPos;
  const ImVec2 scale = draw_data.FramebufferScale;
  const auto min_x = static_cast<int>((command.ClipRect.x - origin.x) * scale.x);
  const auto min_y = static_cast<int>((command.ClipRect.y - origin.y) * scale.y);
  const auto max_x = static_cast<int>((command.ClipRect.z - origin.x) * scale.x);
  const auto max_y = static_cast<int>((command.ClipRect.w - origin.y) * scale.y);
  if (max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return nvrhi::Rect(min_x, max_x, min_y, max_y);
}

}  // namespace

ImGuiRenderer::ImGuiRenderer(nvrhi::IDevice& device)
    : device_(device),
      vertex_shader_(CreateShader(device, nvrhi::ShaderType::Vertex, kImGuiVs)),
      pixel_shader_(CreateShader(device, nvrhi::ShaderType::Pixel, kImGuiPs)),
      font_texture_(UploadFontAtlas(device)) {
  const std::array<nvrhi::VertexAttributeDesc, 3> attributes{
      nvrhi::VertexAttributeDesc()
          .setName("POSITION")
          .setFormat(nvrhi::Format::RG32_FLOAT)
          .setOffset(offsetof(ImDrawVert, pos))
          .setElementStride(sizeof(ImDrawVert)),
      nvrhi::VertexAttributeDesc()
          .setName("TEXCOORD")
          .setFormat(nvrhi::Format::RG32_FLOAT)
          .setOffset(offsetof(ImDrawVert, uv))
          .setElementStride(sizeof(ImDrawVert)),
      nvrhi::VertexAttributeDesc()
          .setName("COLOR")
          .setFormat(nvrhi::Format::RGBA8_UNORM)
          .setOffset(offsetof(ImDrawVert, col))
          .setElementStride(sizeof(ImDrawVert)),
  };
  input_layout_ =
      device.createInputLayout(attributes.data(), static_cast<std::uint32_t>(attributes.size()), vertex_shader_);

  nvrhi::BindingLayoutDesc layout_desc;
  layout_desc.setVisibility(nvrhi::ShaderType::All)
      .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float) * 2))
      .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
      .addItem(nvrhi::BindingLayoutItem::Sampler(0));
  binding_layout_ = device.createBindingLayout(layout_desc);

  font_sampler_ = device.createSampler(
      nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setAllFilters(true));
  nvrhi::BindingSetDesc bindings;
  bindings.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 2))
      .addItem(nvrhi::BindingSetItem::Texture_SRV(0, font_texture_))
      .addItem(nvrhi::BindingSetItem::Sampler(0, font_sampler_));
  font_bindings_ = device.createBindingSet(bindings, binding_layout_);
}

void ImGuiRenderer::Render(nvrhi::ICommandList& commands, nvrhi::IFramebuffer& framebuffer,
                           const ImDrawData& draw_data) {
  if (draw_data.TotalVtxCount == 0) {
    return;
  }
  if (pipeline_ == nullptr) {
    CreatePipeline(framebuffer.getFramebufferInfo());
  }
  UploadGeometry(commands, draw_data);

  const ImVec2 scale = draw_data.FramebufferScale;
  const std::array<float, 2> inv_display_size{1.0F / draw_data.DisplaySize.x, 1.0F / draw_data.DisplaySize.y};
  nvrhi::GraphicsState state;
  state.setPipeline(pipeline_)
      .setFramebuffer(&framebuffer)
      .addBindingSet(font_bindings_)
      .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vertex_buffer_).setSlot(0).setOffset(0))
      .setIndexBuffer(nvrhi::IndexBufferBinding()
                          .setBuffer(index_buffer_)
                          .setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT)
                          .setOffset(0));
  state.viewport.addViewport(nvrhi::Viewport(draw_data.DisplaySize.x * scale.x, draw_data.DisplaySize.y * scale.y));

  std::uint32_t vertex_offset = 0;
  std::uint32_t index_offset = 0;
  for (const ImDrawList* list : draw_data.CmdLists) {
    for (const ImDrawCmd& command : list->CmdBuffer) {
      const std::optional<nvrhi::Rect> scissor = Scissor(command, draw_data);
      if (command.UserCallback != nullptr || !scissor.has_value()) {
        continue;
      }
      state.viewport.scissorRects = {*scissor};
      commands.setGraphicsState(state);
      commands.setPushConstants(inv_display_size.data(), sizeof(inv_display_size));
      commands.drawIndexed(nvrhi::DrawArguments()
                               .setVertexCount(command.ElemCount)
                               .setStartIndexLocation(index_offset + command.IdxOffset)
                               .setStartVertexLocation(vertex_offset + command.VtxOffset));
    }
    vertex_offset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
    index_offset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
  }
}

void ImGuiRenderer::CreatePipeline(const nvrhi::FramebufferInfo& framebuffer_info) {
  nvrhi::GraphicsPipelineDesc desc;
  desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
      .setInputLayout(input_layout_)
      .setVertexShader(vertex_shader_)
      .setPixelShader(pixel_shader_)
      .addBindingLayout(binding_layout_);
  desc.renderState.blendState.setRenderTarget(0, AlphaBlend());
  desc.renderState.rasterState.setCullNone().setScissorEnable(true);
  desc.renderState.depthStencilState.disableDepthTest().disableDepthWrite();
  pipeline_ = device_.createGraphicsPipeline(desc, framebuffer_info);
}

void ImGuiRenderer::UploadGeometry(nvrhi::ICommandList& commands, const ImDrawData& draw_data) {
  // Every list's vertices and indices, one after another.
  std::vector<ImDrawVert> vertices;
  std::vector<ImDrawIdx> indices;
  vertices.reserve(static_cast<std::size_t>(draw_data.TotalVtxCount));
  indices.reserve(static_cast<std::size_t>(draw_data.TotalIdxCount));
  for (const ImDrawList* list : draw_data.CmdLists) {
    vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
    indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
  }
  const std::size_t vertex_bytes = vertices.size() * sizeof(ImDrawVert);
  const std::size_t index_bytes = indices.size() * sizeof(ImDrawIdx);
  vertex_buffer_ = Reserve(device_, vertex_buffer_, VertexBufferDesc(), vertex_bytes);
  index_buffer_ = Reserve(device_, index_buffer_, IndexBufferDesc(), index_bytes);
  commands.writeBuffer(vertex_buffer_, vertices.data(), vertex_bytes);
  commands.writeBuffer(index_buffer_, indices.data(), index_bytes);
}

}  // namespace augusta::renderer
