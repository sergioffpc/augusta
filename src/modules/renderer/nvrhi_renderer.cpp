#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <dxgiformat.h>
#include <nvrhi/common/resource.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/nvrhi.h>
#include <windows.h>
#include <wrl/client.h>

#include "augusta/input.h"
#include "augusta/logging.h"
#include "augusta/renderer.h"
#include "glfw_keys.h"

// glfw3native.h declares glfwGetWin32Window only for the platforms named before
// it is included.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// The Renderer on NVRHI's D3D12 backend, in a GLFW window (ADR-0059). Like
// Falcor's, it draws into an offscreen target, then copies that to the
// swapchain's image and presents it, and its window pushes the keyboard and
// mouse to input::EventSink. So far it only clears the frame: the scene, remote
// players, combat effects, overlay and debug HUD come next, and their setters
// do nothing until then.
namespace augusta::renderer {

namespace {

using Microsoft::WRL::ComPtr;

// Falcor's Renderer's: near-black, in linear light.
constexpr float kClearColorChannel = 0.016F;

nvrhi::Color ClearColor() { return {kClearColorChannel, kClearColorChannel, kClearColorChannel, 1.0F}; }

// How many frames the GPU may be working on at once, and so how many swapchain
// images there are.
constexpr std::uint32_t kFramesInFlight = 3;

// The target is sRGB, so the clear colour and, later, shading stay linear; the
// swapchain's images can't be (flip-model swapchains take no sRGB format), and
// a copy between the two keeps the bytes as they are.
constexpr nvrhi::Format kTargetFormat = nvrhi::Format::SRGBA8_UNORM;
constexpr nvrhi::Format kSwapchainFormat = nvrhi::Format::RGBA8_UNORM;

constexpr std::size_t kBytesPerPixel = 4;

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

// The window's user pointer is the EventSink its callbacks push to.
input::EventSink& SinkOf(GLFWwindow* window) {
  return *static_cast<input::EventSink*>(glfwGetWindowUserPointer(window));
}

void OnKey(GLFWwindow* window, int glfw_key, int /*scancode*/, int action, int /*mods*/) {
  const std::optional<input::Key> key = KeyFromGlfw(glfw_key);
  const std::optional<input::KeyState> state = KeyStateFromGlfw(action);
  if (key && state) {
    SinkOf(window).OnKeyEvent({.key = *key, .state = *state});
  }
}

void OnMouseButton(GLFWwindow* window, int glfw_button, int action, int /*mods*/) {
  const std::optional<input::Key> button = MouseButtonFromGlfw(glfw_button);
  const std::optional<input::KeyState> state = KeyStateFromGlfw(action);
  if (button && state) {
    SinkOf(window).OnKeyEvent({.key = *button, .state = *state});
  }
}

// In client-area pixels; unbounded while the cursor is locked, when GLFW moves
// a virtual cursor instead.
void OnCursorMove(GLFWwindow* window, double x, double y) {
  SinkOf(window).OnMouseMoveEvent({.x = static_cast<float>(x), .y = static_cast<float>(y)});
}

// The window's keyboard and mouse, from now on pushed to sink as glfwPollEvents
// delivers them. Not the wheel: EventSink, like Falcor's Renderer, takes none.
void ForwardInput(GLFWwindow& window, input::EventSink& sink) {
  glfwSetWindowUserPointer(&window, &sink);
  glfwSetKeyCallback(&window, OnKey);
  glfwSetMouseButtonCallback(&window, OnMouseButton);
  glfwSetCursorPosCallback(&window, OnCursorMove);
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

}  // namespace

struct Renderer::Impl {
  // Declared in the order they are made, so they are destroyed in reverse.
  GlfwLibrary glfw;
  GlfwWindow window;
  // The client area as of the last PumpEvents, which a minimized window, at
  // 0 by 0, leaves as it was: the swapchain can't be empty.
  Size size;
  ComPtr<IDXGIFactory4> factory;
  ComPtr<ID3D12Device> d3d_device;
  ComPtr<ID3D12CommandQueue> queue;
  MessageCallback messages;
  nvrhi::DeviceHandle device;
  ComPtr<IDXGISwapChain3> swapchain;
  std::array<nvrhi::TextureHandle, kFramesInFlight> swapchain_images;
  nvrhi::TextureHandle target;
  nvrhi::CommandListHandle commands;

  // Signaled after each frame's work, per swapchain image: a frame waits on its
  // image's before reusing it, which keeps the CPU at most kFramesInFlight
  // frames ahead.
  std::array<nvrhi::EventQueryHandle, kFramesInFlight> frame_done;
  std::uint64_t frame_index = 0;

  // size starts as the window's own, which Windows may have widened past
  // config's.
  Impl(const Config& config, input::EventSink& input_sink)
      : window(OpenWindow(config)), size(FramebufferSize(*window)) {
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Creating the DXGI factory");
    CreateDevice(ChooseAdapter(*factory.Get(), config.adapter).Get());
    CreateSwapchain();
    target = device->createTexture(TargetDesc(size.width, size.height));
    commands = device->createCommandList();
    for (nvrhi::EventQueryHandle& query : frame_done) {
      query = device->createEventQuery();
    }
    ForwardInput(*window, input_sink);
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
    WrapSwapchainImages();
  }

  // The swapchain's images, as NVRHI textures of the swapchain's size.
  void WrapSwapchainImages() {
    const nvrhi::TextureDesc image_desc = nvrhi::TextureDesc()
                                              .setWidth(size.width)
                                              .setHeight(size.height)
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

  void PumpEvents() {
    glfwPollEvents();
    const Size client_area = FramebufferSize(*window);
    if (client_area.width != 0 && client_area.height != 0) {
      size = client_area;
    }
  }

  // Resizes the swapchain and the target to size. DXGI resizes only a swapchain
  // whose images nothing holds, so this waits for the frames in flight and
  // lets NVRHI drop what they held.
  void Resize() {
    device->waitForIdle();
    device->runGarbageCollection();
    for (nvrhi::TextureHandle& image : swapchain_images) {
      image = nullptr;
    }
    ThrowIfFailed(swapchain->ResizeBuffers(kFramesInFlight, size.width, size.height, DXGI_FORMAT_UNKNOWN, 0),
                  "Resizing the swapchain");
    WrapSwapchainImages();
    target = device->createTexture(TargetDesc(size.width, size.height));
  }

  void SetCursorLocked(bool locked) const {
    glfwSetInputMode(window.get(), GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    // Without raw motion, GLFW's disabled cursor still moves as the OS's
    // pointer settings (acceleration) make it.
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
      glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
    }
  }

  void RenderFrame() {
    const nvrhi::TextureDesc& target_desc = target->getDesc();
    if (target_desc.width != size.width || target_desc.height != size.height) {
      Resize();
    }

    const UINT image_index = swapchain->GetCurrentBackBufferIndex();
    nvrhi::IEventQuery* done = frame_done.at(image_index);
    device->waitEventQuery(done);
    device->resetEventQuery(done);

    commands->open();
    commands->clearTextureFloat(target, nvrhi::AllSubresources, ClearColor());
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

void Renderer::PumpEvents() { impl_->PumpEvents(); }

bool Renderer::ShouldClose() const { return glfwWindowShouldClose(impl_->window.get()) == GLFW_TRUE; }

Size Renderer::GetSize() const { return impl_->size; }

void Renderer::RenderFrame() { impl_->RenderFrame(); }

CapturedFrame Renderer::CaptureFrame() { return impl_->CaptureFrame(); }

void Renderer::SetScene(const Scene& /*scene*/) {}

void Renderer::SetCamera(const Camera& /*camera*/) {}

void Renderer::SetCharacterMesh(const std::string& /*character*/, const SceneMesh& /*mesh*/) {}

void Renderer::SetRemotePlayers(std::span<const RemotePlayer> /*remote_players*/) {}

void Renderer::SetCombatEffects(const CombatEffects& /*effects*/) {}

void Renderer::SetOverlay(const Overlay& /*overlay*/) {}

void Renderer::SetCursorLocked(bool locked) { impl_->SetCursorLocked(locked); }

void Renderer::SetDebugHudStats(const DebugHudStats& /*stats*/) {}

}  // namespace augusta::renderer
