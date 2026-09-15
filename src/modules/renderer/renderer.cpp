#include "augusta/renderer.h"

#include <Core/API/Swapchain.h>
#include <Core/Pass/RasterPass.h>
#include <Core/Window.h>
#include <Falcor.h>
#include <Utils/Math/Matrix.h>
#include <Utils/Threading.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

// M1 spike (ADR-0009): the first real (non-stub) body for this module.
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

// One cube vertex - see BuildCubeGeometry. 4 unique vertices per face
// (not 8 shared corners) so every face gets its own straight UV mapping.
struct Vertex {
  Falcor::float3 position;
  Falcor::float2 uv;
};

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

struct Renderer::Impl : public Falcor::Window::ICallbacks {
  input::EventSink& input_sink;

  Falcor::ref<Falcor::Device> device;
  Falcor::ref<Falcor::Window> window;
  Falcor::ref<Falcor::Swapchain> swapchain;
  Falcor::ref<Falcor::Fbo> target_fbo;
  Falcor::ref<Falcor::RasterPass> raster_pass;
  Falcor::ref<Falcor::Vao> vao;
  Falcor::ref<Falcor::Texture> texture;
  Falcor::ref<Falcor::Sampler> sampler;
  std::uint32_t index_count = 0;

  std::chrono::steady_clock::time_point start_time;
  bool cursor_locked = false;

  Impl(const Config& config, input::EventSink& sink) : input_sink(sink) {
    // Falcor::OSServices::start()/stop() are SampleApp-internal (not
    // FALCOR_API-exported, so not linkable from outside Falcor.dll) -
    // Window/Device construction below doesn't appear to depend on them.
    Falcor::Threading::start();

    device = Falcor::make_ref<Falcor::Device>(Falcor::Device::Desc{});

    Falcor::Window::Desc window_desc;
    window_desc.width = config.width;
    window_desc.height = config.height;
    window_desc.title = config.title;
    window_desc.resizableWindow = true;
    window = Falcor::Window::create(window_desc, this);

    Falcor::Swapchain::Desc swapchain_desc;
    swapchain_desc.format = Falcor::ResourceFormat::BGRA8UnormSrgb;
    swapchain_desc.width = window->getClientAreaSize().x;
    swapchain_desc.height = window->getClientAreaSize().y;
    swapchain_desc.imageCount = 3;
    swapchain_desc.enableVSync = false;
    swapchain = Falcor::make_ref<Falcor::Swapchain>(device, swapchain_desc, window->getApiHandle());

    CreateTargetFbo(swapchain_desc.width, swapchain_desc.height);
    BuildCubeGeometry();
    BuildCheckerboardTexture();
    BuildRasterPass();

    start_time = std::chrono::steady_clock::now();
  }

  ~Impl() {
    device->wait();
    Falcor::Threading::shutdown();
  }

  void CreateTargetFbo(std::uint32_t width, std::uint32_t height) {
    target_fbo = Falcor::Fbo::create2D(device, width, height, Falcor::ResourceFormat::BGRA8UnormSrgb,
                                       Falcor::ResourceFormat::D32Float);
  }

  // A unit cube (half-extent 0.5, centered on the model origin), 4
  // vertices per face so each face gets its own [0,1] UV rectangle.
  void BuildCubeGeometry() {
    constexpr float kHalf = 0.5F;
    const std::vector<Vertex> vertices = {
        // +X
        {{kHalf, -kHalf, -kHalf}, {0, 1}},
        {{kHalf, -kHalf, kHalf}, {1, 1}},
        {{kHalf, kHalf, kHalf}, {1, 0}},
        {{kHalf, kHalf, -kHalf}, {0, 0}},
        // -X
        {{-kHalf, -kHalf, kHalf}, {0, 1}},
        {{-kHalf, -kHalf, -kHalf}, {1, 1}},
        {{-kHalf, kHalf, -kHalf}, {1, 0}},
        {{-kHalf, kHalf, kHalf}, {0, 0}},
        // +Y
        {{-kHalf, kHalf, -kHalf}, {0, 1}},
        {{kHalf, kHalf, -kHalf}, {1, 1}},
        {{kHalf, kHalf, kHalf}, {1, 0}},
        {{-kHalf, kHalf, kHalf}, {0, 0}},
        // -Y
        {{-kHalf, -kHalf, kHalf}, {0, 1}},
        {{kHalf, -kHalf, kHalf}, {1, 1}},
        {{kHalf, -kHalf, -kHalf}, {1, 0}},
        {{-kHalf, -kHalf, -kHalf}, {0, 0}},
        // +Z
        {{kHalf, -kHalf, kHalf}, {0, 1}},
        {{-kHalf, -kHalf, kHalf}, {1, 1}},
        {{-kHalf, kHalf, kHalf}, {1, 0}},
        {{kHalf, kHalf, kHalf}, {0, 0}},
        // -Z
        {{-kHalf, -kHalf, -kHalf}, {0, 1}},
        {{kHalf, -kHalf, -kHalf}, {1, 1}},
        {{kHalf, kHalf, -kHalf}, {1, 0}},
        {{-kHalf, kHalf, -kHalf}, {0, 0}},
    };

    std::vector<std::uint16_t> indices;
    indices.reserve(6 * 6);
    for (std::uint16_t face = 0; face < 6; ++face) {
      const std::uint16_t base = face * 4;
      indices.insert(indices.end(), {base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2),
                                     base, static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)});
    }
    index_count = static_cast<std::uint32_t>(indices.size());

    auto vertex_buffer = device->createBuffer(vertices.size() * sizeof(Vertex), Falcor::ResourceBindFlags::Vertex,
                                              Falcor::MemoryType::DeviceLocal, vertices.data());
    auto index_buffer = device->createBuffer(indices.size() * sizeof(std::uint16_t), Falcor::ResourceBindFlags::Index,
                                             Falcor::MemoryType::DeviceLocal, indices.data());

    auto buffer_layout = Falcor::VertexBufferLayout::create();
    buffer_layout->addElement("POSITION", offsetof(Vertex, position), Falcor::ResourceFormat::RGB32Float, 1, 0);
    buffer_layout->addElement("TEXCOORD", offsetof(Vertex, uv), Falcor::ResourceFormat::RG32Float, 1, 1);
    auto layout = Falcor::VertexLayout::create();
    layout->addBufferLayout(0, buffer_layout);

    vao = Falcor::Vao::create(Falcor::Vao::Topology::TriangleList, layout, {vertex_buffer}, index_buffer,
                              Falcor::ResourceFormat::R16Uint);
  }

  // A small procedural checkerboard - the asset pipeline (ADR-0015 -
  // ADR-0020, M5) doesn't exist yet, and this spike only needs to prove
  // Falcor samples *some* texture onto the primitive.
  void BuildCheckerboardTexture() {
    constexpr std::uint32_t kSize = 64;
    constexpr std::uint32_t kCheckSize = 8;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kSize) * kSize);
    for (std::uint32_t y = 0; y < kSize; ++y) {
      for (std::uint32_t x = 0; x < kSize; ++x) {
        const bool light = ((x / kCheckSize) + (y / kCheckSize)) % 2 == 0;
        pixels[(y * kSize) + x] = light ? 0xFFE0E0E0u : 0xFF303030u;
      }
    }
    texture = device->createTexture2D(kSize, kSize, Falcor::ResourceFormat::RGBA8Unorm, 1, 1, pixels.data());
    sampler = device->createSampler(Falcor::Sampler::Desc{});
  }

  void BuildRasterPass() {
    raster_pass = Falcor::RasterPass::create(device, "Augusta/Renderer/Renderer.3d.slang", "vsMain", "psMain");
    raster_pass->getState()->setVao(vao);

    // Winding order isn't pinned down yet (no camera/coordinate-system
    // ADR exists for it), so disable culling rather than risk the cube
    // rendering as invisible from every angle.
    auto rasterizer_desc = Falcor::RasterizerState::Desc().setCullMode(Falcor::RasterizerState::CullMode::None);
    raster_pass->getState()->setRasterizerState(Falcor::RasterizerState::create(rasterizer_desc));
  }

  void Draw() {
    auto* render_context = device->getRenderContext();
    render_context->clearFbo(target_fbo.get(), Falcor::float4(0.05F, 0.05F, 0.08F, 1.0F), 1.0F, 0,
                             Falcor::FboAttachmentType::All);

    const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
    const Falcor::float4x4 model = Falcor::math::matrixFromRotation(elapsed, Falcor::float3(0.3F, 1.0F, 0.0F));
    const Falcor::float4x4 view = Falcor::math::matrixFromTranslation(Falcor::float3(0.0F, 0.0F, -3.0F));
    const float aspect = static_cast<float>(target_fbo->getWidth()) / static_cast<float>(target_fbo->getHeight());
    const Falcor::float4x4 projection = Falcor::math::perspective(0.9F, aspect, 0.1F, 100.0F);
    const Falcor::float4x4 mvp = Falcor::math::mul(projection, Falcor::math::mul(view, model));

    auto root_var = raster_pass->getRootVar();
    root_var["PerFrameCB"]["gMvp"] = mvp;
    root_var["gTexture"] = texture;
    root_var["gSampler"] = sampler;

    raster_pass->getState()->setFbo(target_fbo);
    raster_pass->drawIndexed(render_context, index_count, 0, 0);
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
}

void Renderer::SetCursorLocked([[maybe_unused]] bool locked) {
  // TODO(sergioffpc): Falcor exposes no cursor-lock/hide hook (ADR-0009) -
  // needs a small patch to the vendored submodule (cmake/patches/falcor-
  // augusta.patch). Deferred: no input consumer calls this yet (mouselook
  // lands with gameplay input handling, M2+).
}

}  // namespace augusta::renderer
