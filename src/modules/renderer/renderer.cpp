#include "augusta/renderer.h"

#include <Core/API/Swapchain.h>
#include <Core/Pass/RasterPass.h>
#include <Core/Window.h>
#include <Falcor.h>
#include <Utils/Math/Matrix.h>
#include <Utils/Threading.h>
#include <Utils/Timing/FrameRate.h>
#include <Utils/Timing/ProfilerUI.h>
#include <Utils/UI/Gui.h>

// Falcor.dll exports imgui's own symbols (Source/Falcor/CMakeLists.txt sets
// IMGUI_API=dllexport for its own build only), so a direct <imgui.h>
// include here - same as Falcor's own ProfilerUI.cpp does - links fine
// against that import lib. Used below to theme the HUD beyond what Gui's
// own widget wrappers expose (ApplyHudTheme).
#include <imgui.h>

#include <cfloat>
#include <chrono>
#include <cstdint>
#include <memory>
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

// Layout for the debug GUI windows (ADR-0009's spike scope: Falcor's own
// ImGui wrapper, not a bespoke augusta HUD - PresentationWorld's real UI
// doesn't exist yet).
// Y offsets are hand-measured (ImGui::GetWindowSize() logged at runtime),
// not computed - AutoResize means the real heights below (Stats 122px,
// Settings 182px, at time of measurement) only change if a window's own
// content (line count) changes, so a fixed-position layout has to be
// re-measured then, same as this fix did (Stats grew past Settings' old
// y=90 once the CPU/Memory/geometry lines were added). ~20px margin below
// each measured height.
constexpr Falcor::uint2 kStatsWindowPos(10, 10);
constexpr Falcor::uint2 kSettingsWindowPos(10, 150);
constexpr Falcor::uint2 kProfilerWindowPos(10, 350);
constexpr float kProfilerMinWidth = 700.0F;

// All 3 windows auto-resize to fit their content (no fixed size hint) -
// Gui::WindowFlags::AutoResize maps to ImGuiWindowFlags_AlwaysAutoResize,
// recomputed every frame, so this also tracks content that changes size
// at runtime (e.g. the Profiler window's event table).
// Not constexpr: FALCOR_ENUM_CLASS_OPERATORS' operator| isn't declared
// constexpr, so this has to be a plain const initialized at namespace
// scope (runs once, before main, same as any other global with a
// non-constant initializer).
const Falcor::Gui::WindowFlags kAutoResizeWindowFlags =
    Falcor::Gui::WindowFlags::Default | Falcor::Gui::WindowFlags::AutoResize;

// VS Code's Dark+ editor background (#1E1E1E), used as the default clear
// color so the rendered window blends with this editor's own chrome.
constexpr float kDefaultClearColorChannel = 0.1176F;

// Monochrome HUD theme - black/white/grey only, no color. Falcor's own
// ImGui wrapper only exposes a handful of style knobs (Gui.cpp's own
// constructor tweaks 3-4 colors the same way); everything else here goes
// through <imgui.h> directly. Note: Falcor::ProfilerUI (the Profiler
// window's contents) hardcodes its own bar/graph colors
// (Source/Falcor/Utils/Timing/ProfilerUI.cpp's kColorPalette etc.) rather
// than reading the ImGui style, so this theme can't reach those - only a
// patch to that vendored file could, which is out of scope for a
// colors-only pass (cmake/patches/ only carries the CMakePresets tweak
// today).
constexpr ImVec4 kAccentPrimary(1.0F, 1.0F, 1.0F, 1.0F);       // white
constexpr ImVec4 kAccentSecondary(0.65F, 0.65F, 0.65F, 1.0F);  // grey

// Neutral grey steps for widget backgrounds (idle -> hovered -> active),
// named so the repeated identical r=g=b literals below don't read as
// unexplained magic numbers.
constexpr float kGreyStepIdle = 0.16F;
constexpr float kGreyStepHovered = 0.20F;
constexpr float kGreyStepActive = 0.26F;
constexpr float kGreyStepScrollbarGrab = 0.24F;

void ApplyHudTheme() {
  ImGuiStyle& style = ImGui::GetStyle();

  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09F, 0.10F, 0.11F, 0.92F);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09F, 0.10F, 0.11F, 0.0F);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08F, 0.09F, 0.10F, 0.96F);
  style.Colors[ImGuiCol_Border] = ImVec4(0.20F, 0.22F, 0.24F, 0.7F);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.11F, 0.13F, 0.14F, 1.0F);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(kGreyStepIdle, kGreyStepIdle, kGreyStepIdle, 1.0F);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09F, 0.10F, 0.11F, 0.8F);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13F, 0.14F, 0.15F, 1.0F);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(kGreyStepHovered, kGreyStepHovered, kGreyStepHovered, 1.0F);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(kGreyStepActive, kGreyStepActive, kGreyStepActive, 1.0F);
  style.Colors[ImGuiCol_CheckMark] = kAccentPrimary;
  style.Colors[ImGuiCol_SliderGrab] = kAccentSecondary;
  style.Colors[ImGuiCol_SliderGrabActive] = kAccentPrimary;
  style.Colors[ImGuiCol_Button] = ImVec4(kGreyStepIdle, kGreyStepIdle, kGreyStepIdle, 1.0F);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.35F);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.55F);
  style.Colors[ImGuiCol_Header] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.20F);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.35F);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.50F);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.25F);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.55F);
  style.Colors[ImGuiCol_ResizeGripActive] = kAccentPrimary;
  style.Colors[ImGuiCol_Separator] = ImVec4(0.20F, 0.22F, 0.24F, 0.7F);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.6F);
  style.Colors[ImGuiCol_SeparatorActive] = kAccentPrimary;
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08F, 0.09F, 0.10F, 0.6F);
  style.Colors[ImGuiCol_ScrollbarGrab] =
      ImVec4(kGreyStepScrollbarGrab, kGreyStepScrollbarGrab, kGreyStepScrollbarGrab, 1.0F);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(kAccentSecondary.x, kAccentSecondary.y, kAccentSecondary.z, 0.5F);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = kAccentPrimary;
  style.Colors[ImGuiCol_PlotLines] = kAccentSecondary;
  style.Colors[ImGuiCol_PlotLinesHovered] = kAccentPrimary;
  style.Colors[ImGuiCol_PlotHistogram] = kAccentSecondary;
  style.Colors[ImGuiCol_PlotHistogramHovered] = kAccentPrimary;
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccentPrimary.x, kAccentPrimary.y, kAccentPrimary.z, 0.30F);
}

const Falcor::Gui::DropdownList kCullModeList = {
    {static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::None), "None"},
    {static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::Front), "Front"},
    {static_cast<std::uint32_t>(Falcor::RasterizerState::CullMode::Back), "Back"},
};

constexpr float kAccentStripWidth = 3.0F;

// The mockup's title bars carried a colored left-edge strip
// (`.hud-title::before`) that plain ImGuiCol_Title* slots can't express -
// draw it directly. Must run with the target Gui::Window already open
// (i.e. from inside its scope): Gui::Window's constructor is what calls
// ImGui::Begin, and that's what makes GetWindowPos()/GetWindowDrawList()
// refer to the right window.
void DrawWindowAccentStrip() {
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const float title_height = ImGui::GetFrameHeight();
  ImGui::GetWindowDrawList()->AddRectFilled(window_pos,
                                            ImVec2(window_pos.x + kAccentStripWidth, window_pos.y + title_height),
                                            ImGui::GetColorU32(kAccentPrimary));
}

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
  std::uint32_t vertex_count = 0;
  std::uint32_t index_count = 0;

  Falcor::FrameRate frame_rate;
  // Set inside Draw() itself, spanning only the Clear+Cube command
  // recording - real CPU work, nothing else (not the profiler's own
  // GPU-timing sync, ImGui's command recording, submission, or present's
  // vsync/compositor wait - Falcor doesn't expose those split out, so
  // there's no clean "total" to pair this against). Deliberately NOT
  // paired with frame_rate's own getLastFrameTime() either: that clock
  // resets mid-Draw() (before DrawGui(), not at the RenderFrame()
  // boundary), so its interval isn't comparable to this one.
  float last_cpu_frame_time_ms = 0.0F;
  std::unique_ptr<Falcor::Gui> gui;
  std::unique_ptr<Falcor::ProfilerUI> profiler_ui;

  // Render settings, live-editable from the Settings window (DrawGui).
  Falcor::float4 clear_color{kDefaultClearColorChannel, kDefaultClearColorChannel, kDefaultClearColorChannel, 1.0F};
  Falcor::RasterizerState::CullMode cull_mode = Falcor::RasterizerState::CullMode::None;
  bool wireframe_enabled = false;
  bool vsync_enabled = false;
  float rotation_angle = 0.0F;

  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point last_frame_time;
  bool cursor_locked = false;

  Impl(const Config& config, input::EventSink& sink) : input_sink(sink) {
    // Falcor::OSServices::start()/stop() are SampleApp-internal (not
    // FALCOR_API-exported, so not linkable from outside Falcor.dll) -
    // Window/Device construction below doesn't appear to depend on them.
    Falcor::Threading::start();

    device = Falcor::make_ref<Falcor::Device>(Falcor::Device::Desc{});
    device->getProfiler()->setEnabled(true);

    Falcor::Window::Desc window_desc;
    window_desc.width = config.width;
    window_desc.height = config.height;
    window_desc.title = config.title;
    window_desc.resizableWindow = true;
    window = Falcor::Window::create(window_desc, this);

    RecreateSwapchain();
    const auto size = window->getClientAreaSize();
    CreateTargetFbo(size.x, size.y);
    BuildCubeGeometry();
    BuildCheckerboardTexture();
    BuildRasterPass();

    gui = std::make_unique<Falcor::Gui>(device, size.x, size.y);
    // Gui's constructor leaves its freshly created ImGuiContext current
    // (Utils/UI/Gui.cpp's GuiImpl ctor: CreateContext + SetCurrentContext,
    // never reset before returning) - same context ApplyHudTheme's
    // ImGui::GetStyle() call below ends up touching.
    ApplyHudTheme();
    // Falcor already bundles a monospace face (data/framework/fonts/
    // consolab.ttf) and loads it under the name "monospace" in Gui's own
    // constructor - reuse that instead of shipping a font of our own, to
    // match the technical/data-readout look from the mockup.
    gui->setActiveFont("monospace");

    start_time = std::chrono::steady_clock::now();
    last_frame_time = start_time;
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
  // runtime setter on Swapchain itself), so toggling VSync from the
  // Settings window means tearing down and recreating the whole
  // swapchain - same as handleWindowSizeChange does for a size change.
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
    vertex_count = static_cast<std::uint32_t>(vertices.size());
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
    // ADR exists for it), so cull_mode defaults to None rather than risk
    // the cube rendering as invisible from every angle - live-editable
    // from the Settings window regardless (DrawGui).
    RebuildRasterizerState();
  }

  void Draw() {
    auto* render_context = device->getRenderContext();

    const auto now = std::chrono::steady_clock::now();
    const float delta_time = std::chrono::duration<float>(now - last_frame_time).count();
    last_frame_time = now;
    rotation_angle += delta_time;

    {
      FALCOR_PROFILE(render_context, "Frame");

      {
        FALCOR_PROFILE(render_context, "Clear");
        render_context->clearFbo(target_fbo.get(), clear_color, 1.0F, 0, Falcor::FboAttachmentType::All);
      }

      {
        FALCOR_PROFILE(render_context, "Cube");

        const Falcor::float4x4 model =
            Falcor::math::matrixFromRotation(rotation_angle, Falcor::float3(0.3F, 1.0F, 0.0F));
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
    }
    // Stopped here, before Profiler::endFrame() below - that call does its
    // own mpFence->wait() ("Wait for GPU timings to be available from last
    // frame", Utils/Timing/Profiler.cpp), a real CPU-GPU stall that has
    // nothing to do with recording this frame's commands. Timing across it
    // would make last_cpu_frame_time_ms mostly measure GPU-timing-readback
    // wait, not CPU work - which is exactly why it used to come out close
    // to last_total_frame_time_ms regardless of how cheap the actual draw
    // calls were.
    last_cpu_frame_time_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - now).count();

    device->getProfiler()->endFrame(render_context);
    frame_rate.newFrame();

    DrawGui(render_context);
  }

  // Falcor's own ImGui wrapper (Gui/ProfilerUI) rather than a bespoke
  // augusta overlay - see the file header comment. The profiler window
  // mirrors Falcor::SampleApp::renderUI()'s own pattern: its open/close
  // state IS the profiler's enabled flag, so closing the window also
  // stops the profiler from timing events until it's reopened.
  void DrawGui(Falcor::RenderContext* render_context) {
    gui->beginFrame();

    {
      Falcor::Gui::Window stats_window(gui.get(), "Stats", Falcor::uint2(0, 0), kStatsWindowPos,
                                       kAutoResizeWindowFlags);
      DrawWindowAccentStrip();
      // Falcor::to_string() reports getAverageFrameTime(), smoothed over
      // the last 60 frames - kept as the stable "true" FPS reading. CPU
      // below is single-frame and unsmoothed, and only the Clear+Cube
      // command-recording portion of the frame (see last_cpu_frame_time_ms) -
      // not comparable to the FPS reading above, which covers the whole
      // paced frame.
      stats_window.text(Falcor::to_string(frame_rate));
      stats_window.text(fmt::format("CPU: {:.2f} ms", last_cpu_frame_time_ms));
      stats_window.text(fmt::format("Frame #{}", frame_rate.getFrameCount()));
      // getCurrentRSS(): resident/working set size for this process
      // (Core/Platform/OS.h) - actual RAM in use, not committed/virtual
      // size, and not GPU memory (Falcor exposes no VRAM query).
      constexpr double kBytesPerMebibyte = 1024.0 * 1024.0;
      stats_window.text(
          fmt::format("Memory: {:.1f} MB", static_cast<double>(Falcor::getCurrentRSS()) / kBytesPerMebibyte));
      // Falcor exposes no GPU-side pipeline-statistics query (no
      // vertices/primitives-submitted counter to read back), so this is
      // what augusta itself already knows about what it's submitting:
      // the cube's own geometry plus 1 drawIndexed() call per frame -
      // exact today because there's exactly one draw call in the scene.
      stats_window.text(fmt::format("Vertices: {} | Triangles: {} | Draws: 1", vertex_count, index_count / 3));
    }

    {
      Falcor::Gui::Window settings_window(gui.get(), "Settings", Falcor::uint2(0, 0), kSettingsWindowPos,
                                          kAutoResizeWindowFlags);
      DrawWindowAccentStrip();
      settings_window.text(fmt::format("GPU: {}", device->getInfo().adapterName));
      settings_window.text(fmt::format("API: {}", device->getInfo().apiName));
      settings_window.text(fmt::format("Resolution: {}x{}", target_fbo->getWidth(), target_fbo->getHeight()));

      settings_window.rgbaColor("Clear color", clear_color);

      auto cull_mode_value = static_cast<std::uint32_t>(cull_mode);
      if (settings_window.dropdown("Cull mode", kCullModeList, cull_mode_value)) {
        cull_mode = static_cast<Falcor::RasterizerState::CullMode>(cull_mode_value);
        RebuildRasterizerState();
      }
      if (settings_window.checkbox("Wireframe", wireframe_enabled)) {
        RebuildRasterizerState();
      }
      if (settings_window.checkbox("VSync", vsync_enabled)) {
        RecreateSwapchain();
      }
    }

    bool profiler_open = device->getProfiler()->isEnabled();
    {
      // AutoResize alone settles on the narrowest width that fits the
      // event table, squeezing the graph (ProfilerUI::renderGraph fills
      // whatever's left of the window's width) down to a sliver - a
      // minimum-width constraint (checked by the next Begin(), i.e. the
      // Window constructor right below) keeps AutoResize for height while
      // guaranteeing the graph real estate.
      ImGui::SetNextWindowSizeConstraints(ImVec2(kProfilerMinWidth, 0.0F), ImVec2(FLT_MAX, FLT_MAX));
      Falcor::Gui::Window profiler_window(gui.get(), "Profiler", profiler_open, Falcor::uint2(0, 0), kProfilerWindowPos,
                                          kAutoResizeWindowFlags);
      if (profiler_open) {
        // Unlike Stats/Settings (no close button, so always re-opened),
        // this window's `open` flag is real and persists across frames -
        // only draw the strip while the window is actually pushed
        // (ImGui::Begin/GetWindowDrawList would otherwise run outside a
        // matching Begin/End pair when the window is closed).
        DrawWindowAccentStrip();
        if (!profiler_ui) {
          profiler_ui = std::make_unique<Falcor::ProfilerUI>(device->getProfiler());
        }
        profiler_ui->render();
      }
    }
    device->getProfiler()->setEnabled(profiler_open);

    gui->render(render_context, target_fbo, static_cast<float>(frame_rate.getLastFrameTime()));
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
    gui->onWindowResize(size.x, size.y);
  }

  void handleRenderFrame() override {
    // Unused: Window only invokes this from its own blocking msgLoop(),
    // which this Renderer never calls (see the file header comment) -
    // RenderFrame() below is called directly by ClientRuntime instead.
  }

  void handleKeyboardEvent(const Falcor::KeyboardEvent& event) override {
    if (gui->onKeyboardEvent(event)) {
      return;
    }
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
    if (gui->onMouseEvent(event)) {
      return;
    }
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
  // Sets impl_->last_cpu_frame_time_ms itself, around just the
  // command-recording portion - see the field's own comment (Impl) for
  // why that can't be measured from out here.
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

void Renderer::SetCursorLocked([[maybe_unused]] bool locked) {
  // TODO(sergioffpc): Falcor exposes no cursor-lock/hide hook (ADR-0009) -
  // needs a small patch to the vendored submodule (cmake/patches/falcor-
  // augusta.patch). Deferred: no input consumer calls this yet (mouselook
  // lands with gameplay input handling, M2+).
}

}  // namespace augusta::renderer
