#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
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
#include "augusta/renderer.h"

// glfw3native.h declares glfwGetWin32Window only for the platforms named before
// it is included.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// The Renderer on NVRHI's D3D12 backend, in a GLFW window (ADR-0059). Like
// Falcor's, it draws into an offscreen target, then copies that to the
// swapchain's image and presents it. So far it only clears the frame: the
// scene, remote players, combat effects, overlay, debug HUD, input and resizing
// come next, and their setters do nothing until then.
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

// What NVRHI reports, on standard error: there is no logger in this module.
class MessageCallback final : public nvrhi::IMessageCallback {
 public:
  void message(nvrhi::MessageSeverity severity, const char* text) override {
    if (severity != nvrhi::MessageSeverity::Info) {
      std::cerr << "NVRHI: " << text << '\n';
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
  nvrhi::CommandListHandle commands;

  // Signaled after each frame's work, per swapchain image: a frame waits on its
  // image's before reusing it, which keeps the CPU at most kFramesInFlight
  // frames ahead.
  std::array<nvrhi::EventQueryHandle, kFramesInFlight> frame_done;
  std::uint64_t frame_index = 0;

  Impl(const Config& config, input::EventSink& sink) : input_sink(sink), window(OpenWindow(config)) {
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Creating the DXGI factory");
    CreateDevice(ChooseAdapter(*factory.Get(), config.adapter).Get());
    CreateSwapchain();
    // The window's own size, which Windows may have widened past config's.
    const Size size = FramebufferSize(*window);
    target = device->createTexture(TargetDesc(size.width, size.height));
    commands = device->createCommandList();
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

  void RenderFrame() {
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

void Renderer::PumpEvents() { glfwPollEvents(); }

bool Renderer::ShouldClose() const { return glfwWindowShouldClose(impl_->window.get()) == GLFW_TRUE; }

Size Renderer::GetSize() const { return FramebufferSize(*impl_->window); }

void Renderer::RenderFrame() { impl_->RenderFrame(); }

CapturedFrame Renderer::CaptureFrame() { return impl_->CaptureFrame(); }

void Renderer::SetScene(const Scene& /*scene*/) {}

void Renderer::SetCamera(const Camera& /*camera*/) {}

void Renderer::SetCharacterMesh(const std::string& /*character*/, const SceneMesh& /*mesh*/) {}

void Renderer::SetRemotePlayers(std::span<const RemotePlayer> /*remote_players*/) {}

void Renderer::SetCombatEffects(const CombatEffects& /*effects*/) {}

void Renderer::SetOverlay(const Overlay& /*overlay*/) {}

void Renderer::SetCursorLocked(bool /*locked*/) {}

void Renderer::SetDebugHudStats(const DebugHudStats& /*stats*/) {}

}  // namespace augusta::renderer
