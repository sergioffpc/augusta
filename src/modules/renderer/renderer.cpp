#include "augusta/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <Core/API/Swapchain.h>
#include <Core/Pass/RasterPass.h>
#include <Core/Window.h>
#include <Falcor.h>
#include <Utils/Math/Matrix.h>
#include <Utils/Threading.h>
#include <Utils/Timing/FrameRate.h>
#include <nvtx3/nvtx3.hpp>

#include "augusta/math.h"
#include "debug_hud.h"

// ADR-0009: the first real (non-stub) body for this module.
// Bypasses Falcor::SampleApp entirely - per ADR-0009, SampleApp fuses
// window/device/swapchain/main-loop into one blocking run() call, which
// can't give PumpEvents()/RenderFrame() the independent cadences
// renderer.h's header comment requires. Falcor::Window, Falcor::Device,
// and Falcor::Swapchain are, however, separately constructible - this
// reimplements the slice of SampleApp's constructor/renderFrame/
// handleWindowSizeChange (Source/Falcor/Core/SampleApp.cpp) needed to
// drive them directly: render into an offscreen Fbo, then copy to the
// acquired swapchain image and present, exactly as SampleApp does.
namespace augusta::renderer {

namespace {

// Default clear color - near-black, close to this editor's own chrome.
constexpr float kDefaultClearColorChannel = 0.016F;

// One vertex of the flat-shaded scene geometry - see BuildFlatShadedVertices.
struct Vertex {
  Falcor::float3 position;
  Falcor::float3 normal;
  Falcor::float3 color;
};

// A box has 6 faces of 2 triangles of 3 unshared vertices each - see
// BuildBoxVertices.
constexpr std::size_t kVerticesPerBox = 6 * 2 * 3;

// Expands every mesh's indexed triangles into 3 unshared vertices each,
// carrying the triangle's own face normal and its mesh's color: cooked meshes have positions and
// indices only (no normals - see assets::MeshData), so flat shading is the
// one lighting model the data supports. Throws std::runtime_error on an
// index at or past its mesh's position count.
std::vector<Vertex> BuildFlatShadedVertices(const Scene& scene) {
  std::vector<Vertex> vertices;
  for (const SceneMesh& mesh : scene.meshes) {
    for (const std::uint32_t index : mesh.indices) {
      if (index >= mesh.positions.size()) {
        throw std::runtime_error("scene mesh index out of range for its positions");
      }
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      // NOLINTBEGIN(readability-identifier-length) - a/b/c are the triangle's own corner notation.
      const math::Vec3& a = mesh.positions[mesh.indices[i]];
      const math::Vec3& b = mesh.positions[mesh.indices[i + 1]];
      const math::Vec3& c = mesh.positions[mesh.indices[i + 2]];
      // NOLINTEND(readability-identifier-length)
      const math::Vec3 normal = math::Normalize(math::Cross(b - a, c - a));
      for (const math::Vec3* corner : {&a, &b, &c}) {
        vertices.push_back({.position = {corner->x, corner->y, corner->z},
                            .normal = {normal.x, normal.y, normal.z},
                            .color = {mesh.color.x, mesh.color.y, mesh.color.z}});
      }
    }
  }
  return vertices;
}

// One RemotePlayer's box, as kVerticesPerBox flat-shaded, unshared vertices -
// same technique as BuildFlatShadedVertices, generated rather than indexed
// since a box's 12 triangles are fixed. box.position is its base (renderer.h)
// so the box stands from y=0 to y=2*half_extents.y above it; x/z are
// centered on it.
std::vector<Vertex> BuildBoxVertices(const RemotePlayer& box) {
  const math::Vec3& p = box.position;
  const math::Vec3& h = box.half_extents;
  const Falcor::float3 color{box.color.x, box.color.y, box.color.z};

  // Corners 0-3 are the base (y=0), 4-7 the top (y=2*h.y), both in the same
  // -x-z, +x-z, +x+z, -x+z winding order around the vertical axis.
  const std::array<math::Vec3, 8> corners{
      p + math::Vec3(-h.x, 0.0F, -h.z),       p + math::Vec3(h.x, 0.0F, -h.z),
      p + math::Vec3(h.x, 0.0F, h.z),         p + math::Vec3(-h.x, 0.0F, h.z),
      p + math::Vec3(-h.x, 2.0F * h.y, -h.z), p + math::Vec3(h.x, 2.0F * h.y, -h.z),
      p + math::Vec3(h.x, 2.0F * h.y, h.z),   p + math::Vec3(-h.x, 2.0F * h.y, h.z),
  };
  // Each face as 2 triangles of corner indices, counter-clockwise seen from
  // outside the box (matches SceneMesh's own winding convention, though
  // cull_mode is None so it isn't load-bearing for visibility - see
  // BuildRasterPass).
  constexpr std::array<std::array<std::uint8_t, 3>, 12> kFaces{
      {
          {0, 2, 1},
          {0, 3, 2},  // bottom
          {4, 5, 6},
          {4, 6, 7},  // top
          {0, 1, 5},
          {0, 5, 4},  // -z side
          {1, 2, 6},
          {1, 6, 5},  // +x side
          {2, 3, 7},
          {2, 7, 6},  // +z side
          {3, 0, 4},
          {3, 4, 7},  // -x side
      },
  };

  std::vector<Vertex> vertices;
  vertices.reserve(kVerticesPerBox);
  for (const auto& face : kFaces) {
    // NOLINTBEGIN(readability-identifier-length) - a/b/c are the triangle's own corner notation.
    const math::Vec3& a = corners[face[0]];
    const math::Vec3& b = corners[face[1]];
    const math::Vec3& c = corners[face[2]];
    // NOLINTEND(readability-identifier-length)
    const math::Vec3 normal = math::Normalize(math::Cross(b - a, c - a));
    for (const math::Vec3* corner : {&a, &b, &c}) {
      vertices.push_back({
          .position = {corner->x, corner->y, corner->z},
          .normal = {normal.x, normal.y, normal.z},
          .color = color,
      });
    }
  }
  return vertices;
}

// Every RemotePlayer's box, concatenated - what SetRemotePlayers uploads.
std::vector<Vertex> BuildRemoteVertices(std::span<const RemotePlayer> remote_players) {
  std::vector<Vertex> vertices;
  vertices.reserve(remote_players.size() * kVerticesPerBox);
  for (const RemotePlayer& box : remote_players) {
    const std::vector<Vertex> box_vertices = BuildBoxVertices(box);
    vertices.insert(vertices.end(), box_vertices.begin(), box_vertices.end());
  }
  return vertices;
}

// GLM matrices are column-major (m[column][row]); Falcor's are row-major
// (m[row][column]) - same math, transposed storage.
Falcor::float4x4 ToFalcor(const math::Mat4& matrix) {
  auto result = Falcor::float4x4::zeros();
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      result[row][column] = matrix[column][row];
    }
  }
  return result;
}

std::optional<input::Key> MapKey(Falcor::Input::Key key) {
  switch (key) {
    case Falcor::Input::Key::W:
      return input::Key::kW;
    case Falcor::Input::Key::A:
      return input::Key::kA;
    case Falcor::Input::Key::S:
      return input::Key::kS;
    case Falcor::Input::Key::D:
      return input::Key::kD;
    case Falcor::Input::Key::LeftShift:
      return input::Key::kLeftShift;
    case Falcor::Input::Key::LeftControl:
      return input::Key::kLeftControl;
    case Falcor::Input::Key::Z:
      return input::Key::kZ;
    case Falcor::Input::Key::R:
      return input::Key::kR;
    default:
      return std::nullopt;
  }
}

std::optional<input::MouseButton> MapMouseButton(Falcor::Input::MouseButton button) {
  switch (button) {
    case Falcor::Input::MouseButton::Left:
      return input::MouseButton::kLeft;
    case Falcor::Input::MouseButton::Right:
      return input::MouseButton::kRight;
    default:
      return std::nullopt;
  }
}

}  // namespace

// final: Falcor::Window::ICallbacks (a pure-virtual interface) has no
// virtual destructor of its own, so a non-final Impl deleted through
// unique_ptr<Impl> trips -Wdelete-non-abstract-non-virtual-dtor - Impl is
// never subclassed, so final is both the fix and the correct contract.
struct Renderer::Impl final : public Falcor::Window::ICallbacks {
  input::EventSink& input_sink;

  Falcor::ref<Falcor::Device> device;
  Falcor::ref<Falcor::Window> window;
  Falcor::ref<Falcor::Swapchain> swapchain;
  Falcor::ref<Falcor::Fbo> target_fbo;
  Falcor::ref<Falcor::RasterPass> raster_pass;
  Falcor::ref<Falcor::Vao> vao;
  std::uint32_t vertex_count = 0;
  Camera camera;

  // Remote-player placeholder boxes (issue #82). Unlike vao/vertex_count
  // above, this buffer is created once, sized for kMaxRemotePlayers boxes,
  // and kept as MemoryType::Upload - a persistently-mappable heap
  // SetRemotePlayers can memcpy into every frame via Buffer::setBlob with no
  // GPU wait, unlike UploadScene's DeviceLocal buffer (see that method).
  Falcor::ref<Falcor::Buffer> remote_vertex_buffer;
  Falcor::ref<Falcor::Vao> remote_vao;
  std::uint32_t remote_vertex_count = 0;

  // Debug HUD (FPS, RTT) - see debug_hud.h. frame_rate is ticked once
  // per RenderFrame; hud_stats carries what the caller supplies.
  std::unique_ptr<DebugHud> debug_hud;
  Falcor::FrameRate frame_rate;
  DebugHudStats hud_stats;

  // Render settings - fixed defaults for now (no in-app editor; use
  // NVIDIA Nsight/Tracy for profiling instead).
  Falcor::float4 clear_color{kDefaultClearColorChannel, kDefaultClearColorChannel, kDefaultClearColorChannel, 1.0F};
  Falcor::RasterizerState::CullMode cull_mode = Falcor::RasterizerState::CullMode::None;
  bool wireframe_enabled = false;
  bool vsync_enabled = false;

  bool cursor_locked = false;

  Impl(const Config& config, input::EventSink& sink) : input_sink(sink) {
    // Falcor::OSServices::start()/stop() are SampleApp-internal (not
    // FALCOR_API-exported, so not linkable from outside Falcor.dll) -
    // Window/Device construction below doesn't appear to depend on them.
    Falcor::Threading::start();

    // Requested explicitly rather than left at Type::Default, even though
    // Falcor.lib is itself now built with FALCOR_HAS_VULKAN=OFF
    // (cmake/patches/falcor.patch) so getDefaultDeviceType()
    // could only ever resolve to D3D12 anyway - this documents the
    // choice at the call site instead of relying on that patch being
    // read. D3D12 is the only backend this renderer targets (ADR-0009).
    Falcor::Device::Desc device_desc;
    device_desc.type = Falcor::Device::Type::D3D12;
    // Always requested, not gated on FALCOR_HAS_AFTERMATH here: Falcor's
    // own Device ctor (Device.cpp) already handles the "compiled without
    // Aftermath support" case itself (a logWarning, not a hard failure)
    // when no Nsight Graphics install was found to source the SDK from
    // (see this module's own CMakeLists.txt) - nothing else to guard here.
    device_desc.enableAftermath = true;
    device = Falcor::make_ref<Falcor::Device>(device_desc);

    Falcor::Window::Desc window_desc;
    window_desc.width = config.width;
    window_desc.height = config.height;
    window_desc.title = config.title;
    window_desc.resizableWindow = true;
    window = Falcor::Window::create(window_desc, this);

    RecreateSwapchain();
    const auto size = window->getClientAreaSize();
    CreateTargetFbo(size.x, size.y);
    debug_hud = std::make_unique<DebugHud>(device, Falcor::uint2(size.x, size.y));
    BuildRasterPass();
    CreateRemoteBuffer();
  }

  ~Impl() {
    device->wait();
    Falcor::Threading::shutdown();
  }

  void CreateTargetFbo(std::uint32_t width, std::uint32_t height) {
    target_fbo = Falcor::Fbo::create2D(device, width, height, Falcor::ResourceFormat::BGRA8UnormSrgb,
                                       Falcor::ResourceFormat::D32Float);
  }

  // Swapchain::Desc::enableVSync can only be set at construction (no
  // runtime setter on Swapchain itself), so changing vsync_enabled means
  // tearing down and recreating the whole swapchain - same as
  // handleWindowSizeChange does for a size change.
  void RecreateSwapchain() {
    device->wait();
    // The old swapchain's underlying DXGI swap chain is bound to the
    // window's HWND; explicitly drop it (rather than letting the
    // assignment below replace it, which would briefly construct the
    // new one while the old one is still alive) so it's fully torn down
    // before a second swap chain is created against the same HWND.
    swapchain.reset();
    // Dropping the ref above only *enqueues* the back buffers' GPU
    // resources for release (Texture::~Texture() -> Device::
    // releaseResource(), deferred until a later fence signal - see
    // Device::executeDeferredReleases()) rather than freeing them on the
    // spot. A second wait() signals the fence again and flushes that
    // queue, so the old swap chain's DXGI resources are actually gone
    // before creating a new one on the same HWND - without this,
    // createSwapchain() fails with E_ACCESSDENIED.
    device->wait();
    const auto size = window->getClientAreaSize();
    Falcor::Swapchain::Desc desc;
    desc.format = Falcor::ResourceFormat::BGRA8UnormSrgb;
    desc.width = size.x;
    desc.height = size.y;
    desc.imageCount = 3;
    desc.enableVSync = vsync_enabled;
    swapchain = Falcor::make_ref<Falcor::Swapchain>(device, desc, window->getApiHandle());
  }

  void RebuildRasterizerState() const {
    auto rasterizer_desc = Falcor::RasterizerState::Desc().setCullMode(cull_mode).setFillMode(
        wireframe_enabled ? Falcor::RasterizerState::FillMode::Wireframe : Falcor::RasterizerState::FillMode::Solid);
    raster_pass->getState()->setRasterizerState(Falcor::RasterizerState::create(rasterizer_desc));
  }

  // Replaces the drawn geometry with scene's, as one vertex buffer drawn in
  // a single call. Nothing is touched if scene is invalid (the throw comes
  // before any member is assigned).
  void UploadScene(const Scene& scene) {
    const std::vector<Vertex> vertices = BuildFlatShadedVertices(scene);
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("scene has too many vertices to draw");
    }
    camera = scene.camera;
    vertex_count = static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
      vao = nullptr;
      return;
    }

    // The previous scene's buffers may still be in flight on the GPU.
    device->wait();
    auto vertex_buffer = device->createBuffer(vertices.size() * sizeof(Vertex), Falcor::ResourceBindFlags::Vertex,
                                              Falcor::MemoryType::DeviceLocal, vertices.data());

    auto buffer_layout = Falcor::VertexBufferLayout::create();
    buffer_layout->addElement("POSITION", offsetof(Vertex, position), Falcor::ResourceFormat::RGB32Float, 1, 0);
    buffer_layout->addElement("NORMAL", offsetof(Vertex, normal), Falcor::ResourceFormat::RGB32Float, 1, 1);
    buffer_layout->addElement("COLOR", offsetof(Vertex, color), Falcor::ResourceFormat::RGB32Float, 1, 2);
    auto layout = Falcor::VertexLayout::create();
    layout->addBufferLayout(0, buffer_layout);

    // Draw() sets the active Vao itself before every draw call (it now
    // alternates between this one and remote_vao), so there's nothing more
    // to bind here.
    vao = Falcor::Vao::create(Falcor::Vao::Topology::TriangleList, layout, {vertex_buffer});
  }

  // Creates the fixed-size, persistently-mappable upload-heap buffer and Vao
  // SetRemotePlayers writes into every frame - see the Impl member comment
  // on remote_vertex_buffer. Called once, from the constructor; the buffer
  // is always sized for the worst case (kMaxRemotePlayers) and never
  // recreated afterward.
  void CreateRemoteBuffer() {
    remote_vertex_buffer = device->createBuffer(kMaxRemotePlayers * kVerticesPerBox * sizeof(Vertex),
                                                Falcor::ResourceBindFlags::Vertex, Falcor::MemoryType::Upload);

    auto buffer_layout = Falcor::VertexBufferLayout::create();
    buffer_layout->addElement("POSITION", offsetof(Vertex, position), Falcor::ResourceFormat::RGB32Float, 1, 0);
    buffer_layout->addElement("NORMAL", offsetof(Vertex, normal), Falcor::ResourceFormat::RGB32Float, 1, 1);
    buffer_layout->addElement("COLOR", offsetof(Vertex, color), Falcor::ResourceFormat::RGB32Float, 1, 2);
    auto layout = Falcor::VertexLayout::create();
    layout->addBufferLayout(0, buffer_layout);

    remote_vao = Falcor::Vao::create(Falcor::Vao::Topology::TriangleList, layout, {remote_vertex_buffer});
  }

  // Rewrites the remote-player boxes' vertex data in place via
  // Buffer::setBlob - a map+memcpy into the upload heap, no GPU wait (unlike
  // UploadScene). Throws std::runtime_error if remote_players has more boxes
  // than the buffer was sized for.
  void UpdateRemotePlayers(std::span<const RemotePlayer> remote_players) {
    if (remote_players.size() > kMaxRemotePlayers) {
      throw std::runtime_error("more remote players than the renderer can draw");
    }
    const std::vector<Vertex> vertices = BuildRemoteVertices(remote_players);
    remote_vertex_count = static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
      return;
    }
    remote_vertex_buffer->setBlob(vertices.data(), 0, vertices.size() * sizeof(Vertex));
  }

  // World-to-clip transform of the current camera: the inverse of the
  // camera's own placement, then a right-handed perspective projection.
  [[nodiscard]] Falcor::float4x4 ViewProjection() const {
    const math::Mat4 camera_to_world = math::ToMat4(camera.position, camera.rotation, math::Vec3(1.0F));
    const Falcor::float4x4 view = ToFalcor(math::Inverse(camera_to_world));
    const float aspect = static_cast<float>(target_fbo->getWidth()) / static_cast<float>(target_fbo->getHeight());
    const Falcor::float4x4 projection = Falcor::math::perspective(camera.vertical_fov, aspect, 0.1F, 1000.0F);
    return Falcor::math::mul(projection, view);
  }

  void BuildRasterPass() {
    raster_pass = Falcor::RasterPass::create(device, "Augusta/Renderer/Renderer.3d.slang", "vsMain", "psMain");

    // Cooked meshes carry their authored winding through the cooker's
    // handedness fix (ADR-0032), but nothing has verified it end to end
    // yet, so cull_mode defaults to None rather than risk a scene
    // rendering as invisible from every angle.
    RebuildRasterizerState();
  }

  void Draw() {
    auto* render_context = device->getRenderContext();

    {
      // Named to match the FALCOR_PROFILE scopes below (GPU-side, read by
      // Falcor's own Profiler/ProfilerUI) - these are the CPU-side
      // command-recording durations for the same spans, visible in
      // Nsight Systems, so the two can be lined up on one timeline.
      const nvtx3::scoped_range frame_range{"Frame"};
      FALCOR_PROFILE(render_context, "Frame");

      {
        const nvtx3::scoped_range clear_range{"Clear"};
        FALCOR_PROFILE(render_context, "Clear");
        render_context->clearFbo(target_fbo.get(), clear_color, 1.0F, 0, Falcor::FboAttachmentType::All);
      }

      if (vertex_count > 0 || remote_vertex_count > 0) {
        // Shared by both draws below - same raster_pass/shader, same
        // camera for the whole frame.
        auto root_var = raster_pass->getRootVar();
        root_var["PerFrameCB"]["gViewProj"] = ViewProjection();
        raster_pass->getState()->setFbo(target_fbo);
      }

      if (vertex_count > 0) {
        const nvtx3::scoped_range scene_range{"Scene"};
        FALCOR_PROFILE(render_context, "Scene");

        raster_pass->getState()->setVao(vao);
        raster_pass->draw(render_context, vertex_count, 0);
      }

      if (remote_vertex_count > 0) {
        const nvtx3::scoped_range remote_range{"RemotePlayers"};
        FALCOR_PROFILE(render_context, "RemotePlayers");

        raster_pass->getState()->setVao(remote_vao);
        raster_pass->draw(render_context, remote_vertex_count, 0);
      }
    }

    device->getProfiler()->endFrame(render_context);

    frame_rate.newFrame();
    debug_hud->Render(render_context, target_fbo,
                      {.average_frame_time_s = frame_rate.getAverageFrameTime(),
                       .net = hud_stats.net,
                       .delta_time_s = static_cast<float>(frame_rate.getLastFrameTime())});
  }

  void handleWindowSizeChange() override {
    const auto size = window->getClientAreaSize();
    if (size.x == 0 || size.y == 0) {
      // Minimized - Vulkan/D3D12 both reject a zero-size swapchain.
      return;
    }
    device->wait();
    swapchain->resize(size.x, size.y);
    CreateTargetFbo(size.x, size.y);
    debug_hud->OnWindowResize(size.x, size.y);
  }

  void handleRenderFrame() override {
    // Unused: Window only invokes this from its own blocking msgLoop(),
    // which this Renderer never calls (see the file header comment) -
    // RenderFrame() below is called directly by ClientRuntime instead.
  }

  void handleKeyboardEvent(const Falcor::KeyboardEvent& event) override {
    input::Action action;
    if (event.type == Falcor::KeyboardEvent::Type::KeyPressed) {
      action = input::Action::kPressed;
    } else if (event.type == Falcor::KeyboardEvent::Type::KeyReleased) {
      action = input::Action::kReleased;
    } else {
      return;
    }
    if (const auto key = MapKey(event.key)) {
      input_sink.OnKeyEvent({.key = *key, .action = action});
    }
  }

  void handleMouseEvent(const Falcor::MouseEvent& event) override {
    switch (event.type) {
      case Falcor::MouseEvent::Type::Move:
        input_sink.OnMouseMoveEvent({.x = event.screenPos.x, .y = event.screenPos.y});
        return;
      case Falcor::MouseEvent::Type::ButtonDown:
      case Falcor::MouseEvent::Type::ButtonUp: {
        const auto button = MapMouseButton(event.button);
        if (!button) {
          return;
        }
        const auto action =
            event.type == Falcor::MouseEvent::Type::ButtonDown ? input::Action::kPressed : input::Action::kReleased;
        input_sink.OnMouseButtonEvent({.button = *button, .action = action});
        return;
      }
      default:
        return;
    }
  }

  void handleGamepadEvent(const Falcor::GamepadEvent& /*event*/) override {}
  void handleGamepadState(const Falcor::GamepadState& /*state*/) override {}
  void handleDroppedFile(const std::filesystem::path& /*path*/) override {}
};

Renderer::Renderer(const Config& config, input::EventSink& input_sink)
    : impl_(std::make_unique<Impl>(config, input_sink)) {}

Renderer::~Renderer() = default;

void Renderer::PumpEvents() { impl_->window->pollForEvents(); }

bool Renderer::ShouldClose() const { return impl_->window->shouldClose(); }

Size Renderer::GetSize() const {
  const auto size = impl_->window->getClientAreaSize();
  return {.width = size.x, .height = size.y};
}

void Renderer::RenderFrame() {
  impl_->Draw();

  auto* render_context = impl_->device->getRenderContext();
  const int image_index = impl_->swapchain->acquireNextImage();
  if (image_index < 0) {
    // Swapchain out of date (e.g. mid-resize) - skip presenting this frame.
    return;
  }
  const Falcor::Texture* swapchain_image = impl_->swapchain->getImage(image_index).get();
  render_context->copyResource(swapchain_image, impl_->target_fbo->getColorTexture(0).get());
  render_context->resourceBarrier(swapchain_image, Falcor::Resource::State::Present);
  render_context->submit();

  impl_->swapchain->present();
  impl_->device->endFrame();
}

void Renderer::SetScene(const Scene& scene) { impl_->UploadScene(scene); }

void Renderer::SetRemotePlayers(std::span<const RemotePlayer> remote_players) {
  impl_->UpdateRemotePlayers(remote_players);
}

void Renderer::SetDebugHudStats(const DebugHudStats& stats) { impl_->hud_stats = stats; }

void Renderer::SetCursorLocked([[maybe_unused]] bool locked) {
  // TODO(sergioffpc): Falcor exposes no cursor-lock/hide hook (ADR-0009) -
  // needs a small patch to the vendored submodule (cmake/patches/falcor-
  // augusta.patch). Deferred: no input consumer calls this yet (mouselook
  // lands with gameplay input handling, M3+).
}

}  // namespace augusta::renderer
