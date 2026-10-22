#include "augusta/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <nvrhi/common/resource.h>
#include <nvrhi/nvrhi.h>

#include "augusta/math.h"
#include "augusta/window.h"
#include "frame_regions.h"
#include "streamed_vertices.h"
#include "vertices.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/debug_shapes.h"
#include "debug_hud.h"
#include "debug_shapes_pass.h"
#endif

// The shaders' SPIR-V, compiled at build time (CMakeLists.txt).
#include "kEffectsPs.h"
#include "kEffectsVs.h"
#include "kScenePs.h"
#include "kSceneVs.h"

// The Renderer on NVRHI's Vulkan backend, in the window it is lent (ADR-0059).
// It draws into an offscreen target, then copies that to the swapchain's image
// and presents it (vulkan_swapchain.h). It draws the scene, the remote players, the
// fight's effects and the overlay, and, in a Development build (ADR-0074), the
// Debug overlays' shapes and the debug HUD's lines over them.
namespace augusta::renderer {

namespace {

// Near-black, in linear light.
constexpr float kClearColorChannel = 0.016F;

nvrhi::Color ClearColor() { return {kClearColorChannel, kClearColorChannel, kClearColorChannel, 1.0F}; }

// How many frames the GPU may be working on at once, and so how many regions of
// a per-frame upload buffer (FrameRegion) there are.
constexpr std::uint32_t kFramesInFlight = VulkanSwapchain::kFramesInFlight;

constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;

constexpr std::size_t kBytesPerPixel = 4;

// The depth range, in metres from the camera.
constexpr float kNearPlane = 0.1F;
constexpr float kFarPlane = 1000.0F;

// How many times a frame may rewrite the per-frame constants (NVRHI's volatile
// constant buffer) before the GPU has used them; the Renderer writes them once.
constexpr std::uint32_t kConstantVersions = 16;

// VertexAttributes reads each of Vertex's members as one RGB32_FLOAT.
static_assert(sizeof(Vertex) == 3 * sizeof(math::Vec3), "Vertex must be three packed float triples");

// Vertex's layout, as the shaders' VsIn reads it.
std::array<nvrhi::VertexAttributeDesc, 3> VertexAttributes() {
  const auto attribute = [](const char* semantic, std::size_t offset) {
    return nvrhi::VertexAttributeDesc()
        .setName(semantic)
        .setFormat(nvrhi::Format::RGB32_FLOAT)
        .setOffset(static_cast<std::uint32_t>(offset))
        .setElementStride(sizeof(Vertex));
  };
  return {attribute("POSITION", offsetof(Vertex, position)), attribute("NORMAL", offsetof(Vertex, normal)),
          attribute("COLOR", offsetof(Vertex, color))};
}

// In the swapchain's format, which is sRGB, so the clear colour and shading stay
// linear, and which a copy into its images keeps byte for byte.
nvrhi::TextureDesc TargetDesc(window::Size size, nvrhi::Format format) {
  return nvrhi::TextureDesc()
      .setWidth(size.width)
      .setHeight(size.height)
      .setFormat(format)
      .setIsRenderTarget(true)
      .setInitialState(nvrhi::ResourceStates::RenderTarget)
      .setKeepInitialState(true)
      .setClearValue(ClearColor())
      .setDebugName("Renderer target");
}

nvrhi::TextureDesc DepthDesc(window::Size size) {
  return nvrhi::TextureDesc()
      .setWidth(size.width)
      .setHeight(size.height)
      .setFormat(kDepthFormat)
      .setIsRenderTarget(true)
      .setInitialState(nvrhi::ResourceStates::DepthWrite)
      .setKeepInitialState(true)
      .setClearValue(nvrhi::Color(1.0F))
      .setDebugName("Renderer depth");
}

// Neither the scene nor the effects are culled: cooked meshes' winding
// (ADR-0032) is not verified end to end, and a billboard's depends on how it
// was turned. Depth-tested and written otherwise, as the scene and the remote
// players are drawn.
nvrhi::RenderState UnculledState() {
  nvrhi::RenderState state;
  state.rasterState.setCullNone();
  return state;
}

// The fight's effects glow: each adds its light to what is behind it, and is
// hidden by what is in front without hiding anything itself.
nvrhi::RenderState AdditiveState() {
  nvrhi::RenderState state = UnculledState();
  state.blendState.setRenderTarget(0, nvrhi::BlendState::RenderTarget()
                                          .setBlendEnable(true)
                                          .setSrcBlend(nvrhi::BlendFactor::One)
                                          .setDestBlend(nvrhi::BlendFactor::One)
                                          .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                                          .setDestBlendAlpha(nvrhi::BlendFactor::One));
  state.depthStencilState.disableDepthWrite();
  return state;
}

// The overlay is written over everything.
nvrhi::RenderState OverlayState() {
  nvrhi::RenderState state = UnculledState();
  state.depthStencilState.disableDepthTest().disableDepthWrite();
  return state;
}

nvrhi::ShaderHandle CreateShader(nvrhi::IDevice& device, nvrhi::ShaderType type, std::span<const std::uint32_t> spirv) {
  return device.createShader(nvrhi::ShaderDesc().setShaderType(type), spirv.data(), spirv.size_bytes());
}

// frame's pixels from blue-first to red-first order, as CapturedFrame has them.
void SwapRedAndBlue(CapturedFrame& frame) {
  for (std::size_t i = 0; i < frame.pixels.size(); i += kBytesPerPixel) {
    std::swap(frame.pixels[i], frame.pixels[i + 2]);
  }
}

}  // namespace

struct Renderer::Impl {
  // Declared in the order they are made, so they are destroyed in reverse.
  // window, lent by the Renderer's owner, outlives them all.
  const window::Window& window;
  VulkanDevice vulkan;
  nvrhi::IDevice* device;
  VulkanSwapchain swapchain;
  // Drawn into at the swapchain's size, or at the window's while it has none.
  nvrhi::TextureHandle target;
  nvrhi::TextureHandle depth;
  nvrhi::FramebufferHandle framebuffer;
  nvrhi::CommandListHandle commands;

  // The scene and the remote players, lit by Renderer.3d.slang, and the
  // fight's effects, by Effects.3d.slang, all from the camera's
  // view-projection in per_frame (their PerFrameCB). The overlay is drawn by
  // Effects.3d.slang too, but is already in clip space: overlay_per_frame holds
  // the identity.
  nvrhi::BufferHandle per_frame;
  nvrhi::BufferHandle overlay_per_frame;
  nvrhi::BindingLayoutHandle binding_layout;
  nvrhi::BindingSetHandle bindings;
  nvrhi::BindingSetHandle overlay_bindings;
  nvrhi::GraphicsPipelineHandle lit_pipeline;
  nvrhi::GraphicsPipelineHandle effects_pipeline;
  nvrhi::GraphicsPipelineHandle overlay_pipeline;

  // The scene's vertices, uploaded once by SetScene; null when it has none.
  nvrhi::BufferHandle scene_vertices;
  std::uint32_t scene_vertex_count = 0;
  Camera camera;

  // Each character's mesh (ADR-0042), flat-shaded in its own local space and
  // keyed by character name - set by SetCharacterMesh; empty until the first
  // call.
  CharacterVertices character_vertices;
  StreamedVertices<Vertex> remote_vertices;
  StreamedVertices<Vertex> effect_vertices;
  StreamedVertices<Vertex> overlay_vertices;

#if AUGUSTA_DEBUG_TOOLS
  // Made once the pipelines are, drawing from the camera's per_frame.
  std::unique_ptr<DebugShapesPass> debug_shapes;
  std::unique_ptr<DebugHud> debug_hud;
  std::vector<std::string> text_lines;
#endif

  // Signaled after each frame's work, per frame region (FrameRegion): a frame
  // waits on its region's before writing there or drawing, which keeps the CPU
  // at most kFramesInFlight frames ahead.
  std::array<nvrhi::EventQueryHandle, kFramesInFlight> frame_done;
  std::uint64_t frame_index = 0;

  Impl(const window::Window& window, const Config& config)
      : window(window),
        vulkan(window, config.adapter, config.validation),
        device(&vulkan.Nvrhi()),
        swapchain(vulkan, window.GetSize()) {
    CreateFramebuffer(swapchain.ImageSize().value_or(window.GetSize()));
    commands = device->createCommandList();
    CreatePipelines();
#if AUGUSTA_DEBUG_TOOLS
    debug_shapes = std::make_unique<DebugShapesPass>(*device, *binding_layout, framebuffer->getFramebufferInfo());
    debug_hud = std::make_unique<DebugHud>(*device);
#endif
    for (nvrhi::EventQueryHandle& query : frame_done) {
      query = device->createEventQuery();
    }
    vulkan.ThrowValidationErrors();
  }

  ~Impl() { device->waitForIdle(); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  // The target and its depth, at frame_size, and the framebuffer drawing into
  // both. Frames in flight keep drawing into the ones they had, which NVRHI
  // keeps alive until they are done.
  void CreateFramebuffer(window::Size frame_size) {
    target = device->createTexture(TargetDesc(frame_size, swapchain.Format()));
    depth = device->createTexture(DepthDesc(frame_size));
    framebuffer =
        device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target).setDepthAttachment(depth));
  }

  // Makes the swapchain again if the window's size changed, and the framebuffer
  // with it.
  void Resize() {
    const window::Size size = window.GetSize();
    swapchain.Update(size);
    const window::Size frame_size = swapchain.ImageSize().value_or(size);
    const nvrhi::TextureDesc& target_desc = target->getDesc();
    if (target_desc.width != frame_size.width || target_desc.height != frame_size.height) {
      CreateFramebuffer(frame_size);
    }
  }

  // The pipelines, which all read Vertex and one PerFrameCB.
  void CreatePipelines() {
    per_frame = CreatePerFrame("PerFrameCB");
    overlay_per_frame = CreatePerFrame("Overlay PerFrameCB");
    binding_layout = device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                     .setVisibility(nvrhi::ShaderType::All)
                                                     .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)));
    bindings = device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, per_frame)), binding_layout);
    overlay_bindings = device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, overlay_per_frame)), binding_layout);

    const nvrhi::ShaderHandle scene_vs = CreateShader(*device, nvrhi::ShaderType::Vertex, kSceneVs);
    const nvrhi::ShaderHandle scene_ps = CreateShader(*device, nvrhi::ShaderType::Pixel, kScenePs);
    const nvrhi::ShaderHandle effects_vs = CreateShader(*device, nvrhi::ShaderType::Vertex, kEffectsVs);
    const nvrhi::ShaderHandle effects_ps = CreateShader(*device, nvrhi::ShaderType::Pixel, kEffectsPs);
    const std::array<nvrhi::VertexAttributeDesc, 3> attributes = VertexAttributes();
    const nvrhi::InputLayoutHandle input_layout =
        device->createInputLayout(attributes.data(), static_cast<std::uint32_t>(attributes.size()), scene_vs);

    const auto create = [&](nvrhi::IShader* vertex_shader, nvrhi::IShader* pixel_shader,
                            const nvrhi::RenderState& state) {
      nvrhi::GraphicsPipelineDesc desc;
      desc.setInputLayout(input_layout)
          .setVertexShader(vertex_shader)
          .setPixelShader(pixel_shader)
          .addBindingLayout(binding_layout)
          .setRenderState(state);
      return device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
    };
    lit_pipeline = create(scene_vs, scene_ps, UnculledState());
    effects_pipeline = create(effects_vs, effects_ps, AdditiveState());
    overlay_pipeline = create(effects_vs, effects_ps, OverlayState());
  }

  // A float4x4 of constants, rewritten every frame.
  [[nodiscard]] nvrhi::BufferHandle CreatePerFrame(const char* name) const {
    return device->createBuffer(nvrhi::BufferDesc()
                                    .setByteSize(sizeof(math::Mat4))
                                    .setIsConstantBuffer(true)
                                    .setIsVolatile(true)
                                    .setMaxVersions(kConstantVersions)
                                    .setDebugName(name));
  }

  // Replaces the drawn geometry with scene's, as one vertex buffer drawn in a
  // single call. Nothing is touched if scene is invalid (the throw comes before
  // any member is assigned). Frames in flight keep drawing the previous scene's
  // buffer, which NVRHI keeps alive until they are done.
  void UploadScene(const Scene& scene) {
    const std::vector<Vertex> vertices = BuildFlatShadedVertices(scene);
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("scene has too many vertices to draw");
    }
    // Only the initial camera: a real caller overwrites it with SetCamera every
    // frame from then on.
    camera = scene.camera;
    scene_vertex_count = static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
      scene_vertices = nullptr;
      return;
    }
    const std::size_t byte_size = vertices.size() * sizeof(Vertex);
    scene_vertices = device->createBuffer(nvrhi::BufferDesc()
                                              .setByteSize(byte_size)
                                              .setIsVertexBuffer(true)
                                              .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                              .setKeepInitialState(true)
                                              .setDebugName("Scene vertices"));
    commands->open();
    commands->writeBuffer(scene_vertices, vertices.data(), byte_size);
    commands->close();
    device->executeCommandList(commands);
  }

  // Builds character's local vertices from mesh, as UploadScene does a scene's.
  // Nothing is touched if mesh is invalid.
  void UploadCharacterMesh(const std::string& character, const SceneMesh& mesh) {
    std::vector<Vertex> vertices = BuildFlatShadedVertices(Scene{.meshes = {mesh}, .camera = {}});
    character_vertices.insert_or_assign(character, std::move(vertices));
  }

  // Writes every remote player's vertices into this frame's region.
  void UploadRemotePlayers(std::span<const RemotePlayer> remote_players) {
    const std::uint32_t region = ReadyRegion();
    remote_vertices.Write(*device, BuildRemoteVertices(remote_players, character_vertices), region);
  }

  // Writes the fight's effects into this frame's region, facing the camera as
  // it is now.
  void UploadCombatEffects(const CombatEffects& effects) {
    const std::uint32_t region = ReadyRegion();
    effect_vertices.Write(*device, BuildEffectVertices(effects, camera), region);
  }

  // Writes the overlay into this frame's region, sized for the frame's pixels:
  // the window's, which RenderFrame resizes the target to before drawing it.
  void UploadOverlay(const Overlay& overlay) {
    const std::uint32_t region = ReadyRegion();
    const window::Size size = window.GetSize();
    overlay_vertices.Write(*device, BuildOverlayVertices(overlay, size.width, size.height), region);
  }

#if AUGUSTA_DEBUG_TOOLS
  // Writes the Debug overlays' shapes into this frame's region.
  void UploadDebugShapes(const debug::Shapes& shapes) { debug_shapes->Write(shapes, ReadyRegion()); }
#endif

  // This frame's region of the per-frame upload buffers, once the GPU has
  // finished the last frame drawn from it: normally at once, since that frame
  // is kFramesInFlight back.
  std::uint32_t ReadyRegion() {
    const std::uint32_t region = FrameRegion(frame_index, kFramesInFlight);
    device->waitEventQuery(frame_done.at(region));
    return region;
  }

  // World-to-clip transform of the current camera: the inverse of the camera's
  // own placement, then a right-handed perspective projection onto a depth
  // range of 0 to 1, Vulkan's. NVRHI flips Vulkan's viewport, so clip space's
  // +Y is up, as the shaders have it.
  [[nodiscard]] math::Mat4 ViewProjection() const {
    const math::Mat4 camera_to_world = math::ToMat4(camera.position, camera.rotation, math::Vec3(1.0F));
    const nvrhi::TextureDesc& desc = target->getDesc();
    const float aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
    return glm::perspectiveRH_ZO(camera.vertical_fov, aspect, kNearPlane, kFarPlane) * math::Inverse(camera_to_world);
  }

  // Draws count vertices of buffer from first on, with pipeline and the
  // constants it reads, if there are any.
  void Draw(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IBindingSet* constants, nvrhi::IBuffer* buffer,
            std::uint32_t count, std::uint32_t first) const {
    if (count == 0) {
      return;
    }
    nvrhi::GraphicsState state;
    state.setPipeline(pipeline)
        .setFramebuffer(framebuffer)
        .addBindingSet(constants)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(buffer).setSlot(0).setOffset(0));
    state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    commands->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
  }

  // Draws the region of vertices the last frame wrote.
  void Draw(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IBindingSet* constants,
            const StreamedVertices<Vertex>& vertices) const {
    Draw(pipeline, constants, vertices.buffer, vertices.count, vertices.FirstVertex());
  }

  void RenderFrame() {
    Resize();
    const std::uint32_t region = ReadyRegion();
    nvrhi::IEventQuery* done = frame_done.at(region);
    device->resetEventQuery(done);
    nvrhi::ITexture* image = swapchain.Acquire(region);

    commands->open();
    commands->clearTextureFloat(target, nvrhi::AllSubresources, ClearColor());
    commands->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 1.0F, false, 0);
    const math::Mat4 view_projection = ViewProjection();
    commands->writeBuffer(per_frame, &view_projection, sizeof(view_projection));
    // Volatile constants last one command list, so even the overlay's constant
    // identity is written in every frame's.
    const math::Mat4 identity(1.0F);
    commands->writeBuffer(overlay_per_frame, &identity, sizeof(identity));
    Draw(lit_pipeline, bindings, scene_vertices, scene_vertex_count, 0);
    Draw(lit_pipeline, bindings, remote_vertices);
    // After everything opaque, so the depth buffer hides the effects behind it.
    Draw(effects_pipeline, bindings, effect_vertices);
#if AUGUSTA_DEBUG_TOOLS
    // Over the whole frame as the scene and the effects leave it, at its output
    // resolution, but under what is drawn over the frame itself.
    debug_shapes->Render(*commands, *framebuffer, *bindings);
#endif
    Draw(overlay_pipeline, overlay_bindings, overlay_vertices);
#if AUGUSTA_DEBUG_TOOLS
    debug_hud->Render(*commands, *framebuffer, text_lines);
#endif
    // None while the window is minimized: the frame is drawn, not shown.
    if (image != nullptr) {
      // The copy overwrites the whole image, so what it held, and the layout it
      // was left in, don't matter.
      commands->beginTrackingTextureState(image, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
      commands->copyTexture(image, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
    }
    commands->close();
    device->executeCommandList(commands);
    device->setEventQuery(done, nvrhi::CommandQueue::Graphics);
    swapchain.Present();
    vulkan.ThrowValidationErrors();
    device->runGarbageCollection();
    ++frame_index;
  }

  [[nodiscard]] CapturedFrame CaptureFrame() {
    if (frame_index == 0) {
      return {};
    }
    const nvrhi::TextureDesc& desc = target->getDesc();
    const nvrhi::StagingTextureHandle staging = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    commands->open();
    commands->copyTexture(staging, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
    commands->close();
    device->executeCommandList(commands);
    device->waitForIdle();

    std::size_t row_pitch = 0;
    const auto* mapped = static_cast<const std::uint8_t*>(
        device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &row_pitch));
    if (mapped == nullptr) {
      throw std::runtime_error("Mapping the captured frame failed");
    }
    CapturedFrame frame{.width = desc.width, .height = desc.height, .pixels = {}};
    const std::size_t row_size = std::size_t{desc.width} * kBytesPerPixel;
    frame.pixels.resize(row_size * desc.height);
    for (std::uint32_t row = 0; row < desc.height; ++row) {
      std::memcpy(&frame.pixels[row * row_size], mapped + (row * row_pitch), row_size);
    }
    device->unmapStagingTexture(staging);
    if (desc.format == nvrhi::Format::SBGRA8_UNORM) {
      SwapRedAndBlue(frame);
    }
    vulkan.ThrowValidationErrors();
    return frame;
  }
};

Renderer::Renderer(const window::Window& window, const Config& config)
    : impl_(std::make_unique<Impl>(window, config)) {}

Renderer::~Renderer() = default;

void Renderer::RenderFrame() { impl_->RenderFrame(); }

CapturedFrame Renderer::CaptureFrame() { return impl_->CaptureFrame(); }

void Renderer::SetScene(const Scene& scene) { impl_->UploadScene(scene); }

void Renderer::SetCamera(const Camera& camera) { impl_->camera = camera; }

void Renderer::SetCharacterMesh(const std::string& character, const SceneMesh& mesh) {
  impl_->UploadCharacterMesh(character, mesh);
}

void Renderer::SetRemotePlayers(std::span<const RemotePlayer> remote_players) {
  impl_->UploadRemotePlayers(remote_players);
}

void Renderer::SetCombatEffects(const CombatEffects& effects) { impl_->UploadCombatEffects(effects); }

void Renderer::SetOverlay(const Overlay& overlay) { impl_->UploadOverlay(overlay); }

#if AUGUSTA_DEBUG_TOOLS
void Renderer::SetTextLines(std::vector<std::string> lines) { impl_->text_lines = std::move(lines); }

void Renderer::SetDebugShapes(const debug::Shapes& shapes) { impl_->UploadDebugShapes(shapes); }
#endif

}  // namespace augusta::renderer
