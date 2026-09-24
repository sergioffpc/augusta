#include "augusta/renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
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

// Every RemotePlayer as an instance of its character's local vertices
// (SetCharacterMesh's own flat-shaded vertices, in the character's local space
// - ADR-0040/ADR-0041), skipping one whose character has none: height-scaled around y=0 (remote.height_scale - issue
// #82's "right stance" criterion, renderer.h), then translated to that instance's own position and given its own color.
// remote.position is where the mesh's own origin (y=0) lands - the same convention the character's mesh was cooked
// around, so no further placement is needed. A non-uniform (y-only) scale needs its normals scaled by the inverse
// instead, then renormalized, to stay correct - a uniform scale (height_scale == 1, the common case) leaves them
// unchanged.
std::vector<Vertex> BuildRemoteVertices(
    std::span<const RemotePlayer> remote_players,
    const std::unordered_map<std::uint8_t, std::vector<Vertex>>& character_vertices) {
  std::vector<Vertex> vertices;
  for (const RemotePlayer& remote : remote_players) {
    const auto found = character_vertices.find(remote.character);
    if (found == character_vertices.end()) {
      continue;
    }
    const std::vector<Vertex>& local_vertices = found->second;
    const math::Vec3& p = remote.position;
    const float height_scale = remote.height_scale;
    const Falcor::float3 color{remote.color.x, remote.color.y, remote.color.z};
    for (const Vertex& local_vertex : local_vertices) {
      const math::Vec3 position(local_vertex.position.x, local_vertex.position.y * height_scale,
                                local_vertex.position.z);
      const math::Vec3 normal = math::Normalize(
          math::Vec3(local_vertex.normal.x, local_vertex.normal.y / height_scale, local_vertex.normal.z));
      vertices.push_back({
          .position = {position.x + p.x, position.y + p.y, position.z + p.z},
          .normal = {normal.x, normal.y, normal.z},
          .color = color,
      });
    }
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

// Falcor's key in the run [falcor_first, falcor_first + count) as the key at
// the same offset from first, if it is in the run. Both enums keep letters,
// digits and F1-F12 contiguous and in the same order.
std::optional<input::Key> MapRun(Falcor::Input::Key key, Falcor::Input::Key falcor_first, input::Key first, int count) {
  const int offset = static_cast<int>(key) - static_cast<int>(falcor_first);
  if (offset < 0 || offset >= count) {
    return std::nullopt;
  }
  return static_cast<input::Key>(static_cast<int>(first) + offset);
}

constexpr int kLetterCount = 26;
constexpr int kDigitCount = 10;
constexpr int kFunctionKeyCount = 12;

std::optional<input::Key> MapNamedKey(Falcor::Input::Key key) {
  switch (key) {
    case Falcor::Input::Key::Space:
      return input::Key::kSpace;
    case Falcor::Input::Key::Tab:
      return input::Key::kTab;
    case Falcor::Input::Key::Enter:
      return input::Key::kEnter;
    case Falcor::Input::Key::Backspace:
      return input::Key::kBackspace;
    case Falcor::Input::Key::Escape:
      return input::Key::kEscape;
    case Falcor::Input::Key::LeftShift:
      return input::Key::kLeftShift;
    case Falcor::Input::Key::RightShift:
      return input::Key::kRightShift;
    case Falcor::Input::Key::LeftControl:
      return input::Key::kLeftControl;
    case Falcor::Input::Key::RightControl:
      return input::Key::kRightControl;
    case Falcor::Input::Key::LeftAlt:
      return input::Key::kLeftAlt;
    case Falcor::Input::Key::RightAlt:
      return input::Key::kRightAlt;
    case Falcor::Input::Key::Up:
      return input::Key::kUp;
    case Falcor::Input::Key::Down:
      return input::Key::kDown;
    case Falcor::Input::Key::Left:
      return input::Key::kLeft;
    case Falcor::Input::Key::Right:
      return input::Key::kRight;
    default:
      return std::nullopt;
  }
}

std::optional<input::Key> MapKey(Falcor::Input::Key key) {
  if (const auto letter = MapRun(key, Falcor::Input::Key::A, input::Key::kA, kLetterCount)) {
    return letter;
  }
  if (const auto digit = MapRun(key, Falcor::Input::Key::Key0, input::Key::k0, kDigitCount)) {
    return digit;
  }
  if (const auto function_key = MapRun(key, Falcor::Input::Key::F1, input::Key::kF1, kFunctionKeyCount)) {
    return function_key;
  }
  return MapNamedKey(key);
}

std::optional<input::Key> MapMouseButton(Falcor::Input::MouseButton button) {
  switch (button) {
    case Falcor::Input::MouseButton::Left:
      return input::Key::kMouseLeft;
    case Falcor::Input::MouseButton::Right:
      return input::Key::kMouseRight;
    case Falcor::Input::MouseButton::Middle:
      return input::Key::kMouseMiddle;
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

  // Each character's mesh (ADR-0042), flat-shaded in its own local space and
  // keyed by character index - set by SetCharacterMesh, which (re)creates
  // remote_vertex_buffer/remote_vao below when a mesh outgrows them; empty
  // (and those null) until the first call.
  std::unordered_map<std::uint8_t, std::vector<Vertex>> character_vertices;

  // Unlike vao/vertex_count above, this buffer is sized for kMaxRemotePlayers
  // instances of the largest mesh in character_vertices and kept as MemoryType::Upload
  // - a persistently-mappable heap SetRemotePlayers can memcpy into every
  // frame via Buffer::setBlob with no GPU wait, unlike UploadScene's
  // DeviceLocal buffer (see that method).
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
    // remote_vertex_buffer/remote_vao are created lazily by SetCharacterMesh
    // instead, once the per-instance vertex count they're sized from is known.
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
    // Only the initial camera - a real caller overwrites this via SetCamera
    // every frame from then on (see that method's doc comment).
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

  // Creates the persistently-mappable upload-heap buffer and Vao
  // SetRemotePlayers writes into every frame - see the Impl member comment
  // on remote_vertex_buffer. Called by UploadCharacterMesh, sized for
  // kMaxRemotePlayers instances of vertices_per_player - replaces any
  // previous buffer/Vao.
  void CreateRemoteBuffer(std::size_t vertices_per_player) {
    const std::size_t vertex_capacity = kMaxRemotePlayers * vertices_per_player;
    remote_vertex_buffer = device->createBuffer(vertex_capacity * sizeof(Vertex), Falcor::ResourceBindFlags::Vertex,
                                                Falcor::MemoryType::Upload);

    auto buffer_layout = Falcor::VertexBufferLayout::create();
    buffer_layout->addElement("POSITION", offsetof(Vertex, position), Falcor::ResourceFormat::RGB32Float, 1, 0);
    buffer_layout->addElement("NORMAL", offsetof(Vertex, normal), Falcor::ResourceFormat::RGB32Float, 1, 1);
    buffer_layout->addElement("COLOR", offsetof(Vertex, color), Falcor::ResourceFormat::RGB32Float, 1, 2);
    auto layout = Falcor::VertexLayout::create();
    layout->addBufferLayout(0, buffer_layout);

    remote_vao = Falcor::Vao::create(Falcor::Vao::Topology::TriangleList, layout, {remote_vertex_buffer});
  }

  // Builds character's local vertices from mesh - the same
  // BuildFlatShadedVertices triangle expansion UploadScene uses, reused via
  // a one-mesh Scene rather than duplicated - and (re)creates the buffer/Vao
  // once kMaxRemotePlayers instances of the largest mesh no longer fit.
  // Nothing is touched if mesh is invalid (the throw comes before any member
  // is assigned, same as UploadScene).
  void UploadCharacterMesh(std::uint8_t character, const SceneMesh& mesh) {
    std::vector<Vertex> vertices = BuildFlatShadedVertices(Scene{.meshes = {mesh}, .camera = {}});
    const std::size_t needed_bytes = kMaxRemotePlayers * vertices.size() * sizeof(Vertex);
    character_vertices.insert_or_assign(character, std::move(vertices));
    if (remote_vertex_buffer != nullptr && needed_bytes <= remote_vertex_buffer->getSize()) {
      return;
    }
    std::size_t largest = 0;
    for (const auto& [index, local_vertices] : character_vertices) {
      largest = std::max(largest, local_vertices.size());
    }
    // The previous buffer may still be in flight on the GPU - see UploadScene's own comment.
    device->wait();
    CreateRemoteBuffer(largest);
    remote_vertex_count = 0;
  }

  // Rewrites the remote-player instances' vertex data in place via
  // Buffer::setBlob - a map+memcpy into the upload heap, no GPU wait (unlike
  // UploadScene/UploadCharacterMesh). Throws std::runtime_error if
  // remote_players has more instances than the buffer was sized for. A call
  // before UploadCharacterMesh (remote_vao still null) draws nothing,
  // same as an empty span.
  void UpdateRemotePlayers(std::span<const RemotePlayer> remote_players) {
    if (remote_players.size() > kMaxRemotePlayers) {
      throw std::runtime_error("more remote players than the renderer can draw");
    }
    if (remote_vao == nullptr) {
      remote_vertex_count = 0;
      return;
    }
    const std::vector<Vertex> vertices = BuildRemoteVertices(remote_players, character_vertices);
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
    input::KeyState state;
    if (event.type == Falcor::KeyboardEvent::Type::KeyPressed) {
      state = input::KeyState::kPressed;
    } else if (event.type == Falcor::KeyboardEvent::Type::KeyReleased) {
      state = input::KeyState::kReleased;
    } else {
      return;
    }
    if (const auto key = MapKey(event.key)) {
      input_sink.OnKeyEvent({.key = *key, .state = state});
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
        const auto state =
            event.type == Falcor::MouseEvent::Type::ButtonDown ? input::KeyState::kPressed : input::KeyState::kReleased;
        input_sink.OnKeyEvent({.key = *button, .state = state});
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

void Renderer::SetCamera(const Camera& camera) { impl_->camera = camera; }

void Renderer::SetCharacterMesh(std::uint8_t character, const SceneMesh& mesh) {
  impl_->UploadCharacterMesh(character, mesh);
}

void Renderer::SetRemotePlayers(std::span<const RemotePlayer> remote_players) {
  impl_->UpdateRemotePlayers(remote_players);
}

void Renderer::SetDebugHudStats(const DebugHudStats& stats) { impl_->hud_stats = stats; }

void Renderer::SetCursorLocked(bool locked) { impl_->window->setCursorLocked(locked); }

}  // namespace augusta::renderer
