#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <dxgiformat.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <nvrhi/common/resource.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/nvrhi.h>
#include <windows.h>
#include <wrl/client.h>

#include "augusta/input.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "frame_regions.h"
#include "vertices.h"

// The shaders' DXIL, compiled at build time (CMakeLists.txt).
#include "kScenePs.h"
#include "kSceneVs.h"

// glfw3native.h declares glfwGetWin32Window only for the platforms named before
// it is included.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// The Renderer on NVRHI's D3D12 backend, in a GLFW window (ADR-0059). Like
// Falcor's, it draws into an offscreen target, then copies that to the
// swapchain's image and presents it. So far it draws the scene and the remote
// players: combat effects, overlay, debug HUD, input and resizing come next,
// and their setters do nothing until then.
namespace augusta::renderer {

namespace {

using Microsoft::WRL::ComPtr;

// Falcor's Renderer's: near-black, in linear light.
constexpr float kClearColorChannel = 0.016F;

nvrhi::Color ClearColor() { return {kClearColorChannel, kClearColorChannel, kClearColorChannel, 1.0F}; }

// How many frames the GPU may be working on at once, and so how many swapchain
// images and regions of a per-frame upload buffer (FrameRegion) there are.
constexpr std::uint32_t kFramesInFlight = 3;

// The target is sRGB, so the clear colour and shading stay linear; the
// swapchain's images can't be (flip-model swapchains take no sRGB format), and
// a copy between the two keeps the bytes as they are.
constexpr nvrhi::Format kTargetFormat = nvrhi::Format::SRGBA8_UNORM;
constexpr nvrhi::Format kSwapchainFormat = nvrhi::Format::RGBA8_UNORM;
constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;

constexpr std::size_t kBytesPerPixel = 4;

// Falcor's Renderer's depth range, in metres from the camera.
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

// A vertex buffer the CPU rewrites every frame (the remote players): unlike the
// scene's (UploadScene), it is in an upload heap, mapped once for good, so a
// frame's vertices are copied straight into it with no GPU wait.
//
// It holds kFramesInFlight regions of region_capacity vertices each, and a
// frame writes and draws only the region FrameRegion gives it: several frames
// are in flight, and rewriting the one region a previous frame is still drawing
// from tears that frame's drawing. The caller waits for the GPU to finish the
// last frame drawn from a region before writing there
// (Renderer::Impl::ReadyRegion).
//
// It grows into a new buffer only when a call needs more room than any before
// it. Frames in flight keep drawing from the old one, which NVRHI keeps alive
// until they are done, so growing waits for nothing either.
struct StreamedVertices {
  nvrhi::BufferHandle buffer;
  std::span<Vertex> mapped;
  std::uint32_t region_capacity = 0;
  std::uint32_t count = 0;
  std::uint32_t region = 0;

  // Writes vertices into region, first growing the buffer to fit them if a
  // region holds fewer. Nothing is drawn if there are none.
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

  // Where region starts in buffer, in vertices.
  [[nodiscard]] std::uint32_t FirstVertex() const { return region * region_capacity; }
};

void ThrowIfFailed(HRESULT result, const char* what) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string(what) + " failed");
  }
}

// What NVRHI reports, logged at its own severity.
class MessageCallback final : public nvrhi::IMessageCallback {
 public:
  void message(nvrhi::MessageSeverity severity, const char* text) override {
    switch (severity) {
      case nvrhi::MessageSeverity::Info:
        LD("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
        return;
      case nvrhi::MessageSeverity::Warning:
        LW("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
        return;
      case nvrhi::MessageSeverity::Error:
        LE("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
        return;
      case nvrhi::MessageSeverity::Fatal:
        LC("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
        return;
    }
  }
};

// GLFW's library, initialized for as long as one is alive.
struct GlfwLibrary {
  GlfwLibrary() {
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("GLFW failed to initialize");
    }
  }
  ~GlfwLibrary() { glfwTerminate(); }
  GlfwLibrary(const GlfwLibrary&) = delete;
  GlfwLibrary& operator=(const GlfwLibrary&) = delete;
  GlfwLibrary(GlfwLibrary&&) = delete;
  GlfwLibrary& operator=(GlfwLibrary&&) = delete;
};

struct GlfwWindowDeleter {
  void operator()(GLFWwindow* window) const { glfwDestroyWindow(window); }
};
using GlfwWindow = std::unique_ptr<GLFWwindow, GlfwWindowDeleter>;

GlfwWindow OpenWindow(const Config& config) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  // Until the swapchain follows a resize, the window keeps its size.
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, config.hidden ? GLFW_FALSE : GLFW_TRUE);
  GlfwWindow window(glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                                     config.title.c_str(), nullptr, nullptr));
  if (window == nullptr) {
    throw std::runtime_error("GLFW failed to create the window");
  }
  return window;
}

Size FramebufferSize(GLFWwindow& window) {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(&window, &width, &height);
  return {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height)};
}

// The adapter config names: WARP, or null, which D3D12 takes as the system's
// default GPU.
ComPtr<IDXGIAdapter> ChooseAdapter(IDXGIFactory4& factory, Adapter adapter) {
  ComPtr<IDXGIAdapter> chosen;
  if (adapter == Adapter::kSoftware) {
    ThrowIfFailed(factory.EnumWarpAdapter(IID_PPV_ARGS(&chosen)), "Finding the WARP adapter");
  }
  return chosen;
}

nvrhi::TextureDesc TargetDesc(std::uint32_t width, std::uint32_t height) {
  return nvrhi::TextureDesc()
      .setWidth(width)
      .setHeight(height)
      .setFormat(kTargetFormat)
      .setIsRenderTarget(true)
      .setInitialState(nvrhi::ResourceStates::RenderTarget)
      .setKeepInitialState(true)
      .setClearValue(ClearColor())
      .setDebugName("Renderer target");
}

nvrhi::TextureDesc DepthDesc(std::uint32_t width, std::uint32_t height) {
  return nvrhi::TextureDesc()
      .setWidth(width)
      .setHeight(height)
      .setFormat(kDepthFormat)
      .setIsRenderTarget(true)
      .setInitialState(nvrhi::ResourceStates::DepthWrite)
      .setKeepInitialState(true)
      .setClearValue(nvrhi::Color(1.0F))
      .setDebugName("Renderer depth");
}

nvrhi::ShaderHandle CreateShader(nvrhi::IDevice& device, nvrhi::ShaderType type, std::span<const unsigned char> dxil) {
  return device.createShader(nvrhi::ShaderDesc().setShaderType(type), dxil.data(), dxil.size());
}

}  // namespace

struct Renderer::Impl {
  // Unused until the window's events are mapped to it.
  input::EventSink& input_sink;

  // Declared in the order they are made, so they are destroyed in reverse.
  GlfwLibrary glfw;
  GlfwWindow window;
  ComPtr<IDXGIFactory4> factory;
  ComPtr<ID3D12Device> d3d_device;
  ComPtr<ID3D12CommandQueue> queue;
  MessageCallback messages;
  nvrhi::DeviceHandle device;
  ComPtr<IDXGISwapChain3> swapchain;
  std::array<nvrhi::TextureHandle, kFramesInFlight> swapchain_images;
  nvrhi::TextureHandle target;
  nvrhi::TextureHandle depth;
  nvrhi::FramebufferHandle framebuffer;
  nvrhi::CommandListHandle commands;

  // The scene and the remote players, lit by Renderer.3d.slang from the
  // camera's view-projection in per_frame (its PerFrameCB).
  nvrhi::BufferHandle per_frame;
  nvrhi::BindingSetHandle bindings;
  nvrhi::GraphicsPipelineHandle lit_pipeline;

  // The scene's vertices, uploaded once by SetScene; null when it has none.
  nvrhi::BufferHandle scene_vertices;
  std::uint32_t scene_vertex_count = 0;
  Camera camera;

  // Each character's mesh (ADR-0042), flat-shaded in its own local space and
  // keyed by character name - set by SetCharacterMesh; empty until the first
  // call.
  CharacterVertices character_vertices;
  StreamedVertices remote_vertices;

  // Signaled after each frame's work, per frame region (FrameRegion): a frame
  // waits on its region's before writing there or drawing, which keeps the CPU
  // at most kFramesInFlight frames ahead.
  std::array<nvrhi::EventQueryHandle, kFramesInFlight> frame_done;
  std::uint64_t frame_index = 0;

  Impl(const Config& config, input::EventSink& sink) : input_sink(sink), window(OpenWindow(config)) {
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Creating the DXGI factory");
    CreateDevice(ChooseAdapter(*factory.Get(), config.adapter).Get());
    CreateSwapchain();
    // The window's own size, which Windows may have widened past config's.
    const Size size = FramebufferSize(*window);
    target = device->createTexture(TargetDesc(size.width, size.height));
    depth = device->createTexture(DepthDesc(size.width, size.height));
    framebuffer =
        device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target).setDepthAttachment(depth));
    commands = device->createCommandList();
    CreateLitPipeline();
    for (nvrhi::EventQueryHandle& query : frame_done) {
      query = device->createEventQuery();
    }
  }

  ~Impl() { device->waitForIdle(); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  void CreateDevice(IDXGIAdapter* adapter) {
    ThrowIfFailed(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d_device)),
                  "Creating the D3D12 device");
    const D3D12_COMMAND_QUEUE_DESC queue_desc{.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                              .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                                              .NodeMask = 0};
    ThrowIfFailed(d3d_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "Creating the command queue");
    nvrhi::d3d12::DeviceDesc device_desc;
    device_desc.errorCB = &messages;
    device_desc.pDevice = d3d_device.Get();
    device_desc.pGraphicsCommandQueue = queue.Get();
    device = nvrhi::d3d12::createDevice(device_desc);
  }

  void CreateSwapchain() {
    const Size size = FramebufferSize(*window);
    const DXGI_SWAP_CHAIN_DESC1 desc{.Width = size.width,
                                     .Height = size.height,
                                     .Format = nvrhi::d3d12::convertFormat(kSwapchainFormat),
                                     .Stereo = FALSE,
                                     .SampleDesc = {.Count = 1, .Quality = 0},
                                     .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                     .BufferCount = kFramesInFlight,
                                     .Scaling = DXGI_SCALING_STRETCH,
                                     .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                     .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
                                     .Flags = 0};
    HWND hwnd = glfwGetWin32Window(window.get());
    ComPtr<IDXGISwapChain1> swapchain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &desc, nullptr, nullptr, &swapchain1),
                  "Creating the swapchain");
    // GLFW's window has no exclusive fullscreen, so neither does DXGI's Alt+Enter.
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "Associating the window");
    ThrowIfFailed(swapchain1.As(&swapchain), "Getting IDXGISwapChain3");

    const nvrhi::TextureDesc image_desc = nvrhi::TextureDesc()
                                              .setWidth(desc.Width)
                                              .setHeight(desc.Height)
                                              .setFormat(kSwapchainFormat)
                                              .setInitialState(nvrhi::ResourceStates::Present)
                                              .setKeepInitialState(true)
                                              .setDebugName("Swapchain image");
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      ComPtr<ID3D12Resource> image;
      ThrowIfFailed(swapchain->GetBuffer(i, IID_PPV_ARGS(&image)), "Getting a swapchain image");
      swapchain_images.at(i) = device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource,
                                                                    nvrhi::Object(image.Get()), image_desc);
    }
  }

  // Falcor's Renderer's: depth-tested, and not culled, since cooked meshes'
  // winding (ADR-0032) is not verified end to end.
  void CreateLitPipeline() {
    const nvrhi::ShaderHandle vertex_shader = CreateShader(*device, nvrhi::ShaderType::Vertex, kSceneVs);
    const nvrhi::ShaderHandle pixel_shader = CreateShader(*device, nvrhi::ShaderType::Pixel, kScenePs);
    const std::array<nvrhi::VertexAttributeDesc, 3> attributes = VertexAttributes();
    const nvrhi::InputLayoutHandle input_layout =
        device->createInputLayout(attributes.data(), static_cast<std::uint32_t>(attributes.size()), vertex_shader);

    per_frame = device->createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(sizeof(math::Mat4))
                                         .setIsConstantBuffer(true)
                                         .setIsVolatile(true)
                                         .setMaxVersions(kConstantVersions)
                                         .setDebugName("PerFrameCB"));
    const nvrhi::BindingLayoutHandle binding_layout =
        device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                        .setVisibility(nvrhi::ShaderType::All)
                                        .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)));
    bindings = device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, per_frame)), binding_layout);

    nvrhi::GraphicsPipelineDesc desc;
    desc.setInputLayout(input_layout)
        .setVertexShader(vertex_shader)
        .setPixelShader(pixel_shader)
        .addBindingLayout(binding_layout);
    desc.renderState.rasterState.setCullNone();
    lit_pipeline = device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
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

  // This frame's region of the per-frame upload buffers, once the GPU has
  // finished the last frame drawn from it: normally at once, since that frame
  // is kFramesInFlight back.
  std::uint32_t ReadyRegion() {
    const std::uint32_t region = FrameRegion(frame_index, kFramesInFlight);
    device->waitEventQuery(frame_done.at(region));
    return region;
  }

  // World-to-clip transform of the current camera: the inverse of the camera's
  // own placement, then a right-handed perspective projection onto D3D's depth
  // range of 0 to 1.
  [[nodiscard]] math::Mat4 ViewProjection() const {
    const math::Mat4 camera_to_world = math::ToMat4(camera.position, camera.rotation, math::Vec3(1.0F));
    const nvrhi::TextureDesc& desc = target->getDesc();
    const float aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
    return glm::perspectiveRH_ZO(camera.vertical_fov, aspect, kNearPlane, kFarPlane) * math::Inverse(camera_to_world);
  }

  // Draws count vertices of buffer from first on, lit, if there are any.
  void DrawLit(nvrhi::IBuffer* buffer, std::uint32_t count, std::uint32_t first) const {
    if (count == 0) {
      return;
    }
    nvrhi::GraphicsState state;
    state.setPipeline(lit_pipeline)
        .setFramebuffer(framebuffer)
        .addBindingSet(bindings)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(buffer).setSlot(0).setOffset(0));
    state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    commands->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
  }

  void RenderFrame() {
    nvrhi::IEventQuery* done = frame_done.at(ReadyRegion());
    device->resetEventQuery(done);
    const UINT image_index = swapchain->GetCurrentBackBufferIndex();

    commands->open();
    commands->clearTextureFloat(target, nvrhi::AllSubresources, ClearColor());
    commands->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 1.0F, false, 0);
    const math::Mat4 view_projection = ViewProjection();
    commands->writeBuffer(per_frame, &view_projection, sizeof(view_projection));
    DrawLit(scene_vertices, scene_vertex_count, 0);
    DrawLit(remote_vertices.buffer, remote_vertices.count, remote_vertices.FirstVertex());
    commands->copyTexture(swapchain_images.at(image_index), nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
    commands->close();
    device->executeCommandList(commands);
    device->setEventQuery(done, nvrhi::CommandQueue::Graphics);

    // DXGI_STATUS_OCCLUDED, for a hidden or minimized window, is a success.
    ThrowIfFailed(swapchain->Present(0, 0), "Presenting");
    device->runGarbageCollection();
    ++frame_index;
  }

  [[nodiscard]] CapturedFrame CaptureFrame() const {
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
    return frame;
  }
};

Renderer::Renderer(const Config& config, input::EventSink& input_sink)
    : impl_(std::make_unique<Impl>(config, input_sink)) {}

Renderer::~Renderer() = default;

void Renderer::PumpEvents() { glfwPollEvents(); }

bool Renderer::ShouldClose() const { return glfwWindowShouldClose(impl_->window.get()) == GLFW_TRUE; }

Size Renderer::GetSize() const { return FramebufferSize(*impl_->window); }

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

void Renderer::SetCombatEffects(const CombatEffects& /*effects*/) {}

void Renderer::SetOverlay(const Overlay& /*overlay*/) {}

void Renderer::SetCursorLocked(bool /*locked*/) {}

void Renderer::SetDebugHudStats(const DebugHudStats& /*stats*/) {}

}  // namespace augusta::renderer
