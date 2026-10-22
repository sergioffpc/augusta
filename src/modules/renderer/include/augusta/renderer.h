#ifndef AUGUSTA_RENDERER_H_
#define AUGUSTA_RENDERER_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "augusta/math.h"
#include "augusta/window.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/debug_panels.h"
#include "augusta/debug_shapes.h"
#include "augusta/window_events.h"
#endif

/// \file
/// augusta::renderer draws through NVIDIA's NVRHI on Vulkan (ADR-0059), used
/// exclusively by the client. It draws into the client's single
/// window::Window, which the client Runtime owns and lends it, and makes its
/// Vulkan surface, but knows nothing of input: the client Runtime drains the
/// window's events itself (Window::Poll) and hands them to input.
///
/// Draining the window and RenderFrame are deliberately two separate calls,
/// not one opaque loop, because this engine's threads don't share a cadence
/// (ARCHITECTURE.md §8, ADR-0005): PredictionWorld ticks on the Prediction
/// thread at a fixed rate decoupled from how often a frame is actually
/// presented on the Main/Render thread. Window::Poll just drains the OS's
/// event queue (cheap, safe to call often); RenderFrame does the actual GPU
/// work and should be called at the app's presentation rate instead.
/// ClientRuntime's Run() loop decides that split.
///
/// The debug HUD it draws is the text lines it is handed (SetTextLines): what
/// they say is augusta::hud's. The Debug overlays it draws are the shapes it is
/// handed (SetDebugShapes), which augusta::debug describes and tessellates
/// (ADR-0075). The Debug panels it draws are the ones it is handed
/// (SetDebugPanels), operated by the window's events the client Runtime hands
/// it in Debug interaction (HandleDebugPanelInput), and their changes come
/// back from their own call (TakeDebugControlEvents), not from RenderFrame;
/// Dear ImGui, which draws them, stays inside the Renderer. It reports whether
/// Aftermath watches its device (GpuCrashDumps). A Shipping build (ADR-0074)
/// has none of them.
///
/// Interface scope: draws one static Scene (a list of world-space triangle
/// meshes seen from one camera), plus, per frame, however many RemotePlayer
/// instances PresentationWorld's Interpolation phase produces positions for
/// (SetRemotePlayers, below), each drawn with the mesh of its character
/// (SetCharacterMesh), the fight - tracers, impacts and muzzle flashes
/// (SetCombatEffects) - and the crosshair and hit marker over the frame
/// (SetOverlay): the Presentation State it consumes (ADR-0024), drawn with
/// Slang shaders (ADR-0014). Not drawn yet: the local player's own body and
/// weapon model, and skeletal animation.
namespace augusta::renderer {

/// Which Vulkan device the Renderer draws with.
enum class Adapter : std::uint8_t {
  /// A discrete GPU when there is one, else an integrated one.
  kHardware,
  /// The first CPU-type device, a software rasterizer such as Mesa's lavapipe:
  /// for a test on a machine with no GPU, such as CI's runner. Never a GPU.
  kSoftware,
};

/// Whether the Renderer runs under Vulkan's validation layer,
/// VK_LAYER_KHRONOS_validation, which the loader finds through
/// VK_ADD_LAYER_PATH. An error it reports is thrown by the Renderer call it
/// happened in (see Renderer).
enum class Validation : std::uint8_t {
  kOff,
  /// Under the layer if the loader finds it, else without, after a warning:
  /// for a Debug build, which a developer may run without it.
  kIfAvailable,
  /// Under the layer, or not at all: for a test, which must not pass without
  /// it.
  kRequired,
};

/// Initial renderer parameters.
struct Config {
  Adapter adapter = Adapter::kHardware;
  Validation validation = Validation::kOff;
};

/// A frame read back from the GPU: width by height pixels, row by row from the
/// top, 4 bytes each (red, green, blue, alpha), sRGB-encoded.
struct CapturedFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

#if AUGUSTA_DEBUG_TOOLS
/// Whether Nsight Aftermath watches the GPU for crash dumps, and where one
/// lands: what the Profiling Debug panel shows.
struct GpuCrashDumpStatus {
  /// Aftermath started: false in a build without its SDK, or without an NVIDIA
  /// driver.
  bool enabled = false;
  /// The device has VK_NV_device_diagnostics_config, so a dump says what the
  /// GPU was doing: false off an NVIDIA GPU, lavapipe's included.
  bool attached = false;
  /// The absolute directory the next dump lands in (gpu_crash_dump_files.h).
  std::filesystem::path directory;
};
#endif

/// One triangle-list mesh with its positions already in world space (Y-up,
/// right-handed, 1 unit = 1 m - ADR-0032). Winding is counter-clockwise seen
/// from the front face.
struct SceneMesh {
  /// The light grey a mesh is drawn in unless given a color.
  static constexpr float kDefaultGrey = 0.8F;

  std::vector<math::Vec3> positions;
  std::vector<std::uint32_t> indices;
  /// Base color, linear RGB in [0, 1].
  math::Vec3 color{kDefaultGrey, kDefaultGrey, kDefaultGrey};
};

/// The viewpoint a Scene is drawn from. The camera looks down its local -Z
/// axis with +Y up, before rotation is applied.
struct Camera {
  /// An unset camera's height, in metres: a standing player's eyes.
  static constexpr float kDefaultEyeHeight = 1.7F;
  /// How far back from the origin an unset camera stands, in metres.
  static constexpr float kDefaultDistance = 5.0F;
  /// An unset camera's vertical field of view, in radians (about 52 degrees).
  static constexpr float kDefaultVerticalFov = 0.9F;

  math::Vec3 position{0.0F, kDefaultEyeHeight, kDefaultDistance};
  math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  /// Vertical field of view, in radians.
  float vertical_fov = kDefaultVerticalFov;
};

/// Everything the renderer draws: static geometry plus the camera it is
/// seen from.
struct Scene {
  std::vector<SceneMesh> meshes;
  Camera camera;
};

/// Default color for RemotePlayer, until every prim contributes its own
/// (character content builds no SceneNode/base_color today, ADR-0041) - a
/// muted red, distinct from SceneMesh's default grey.
inline constexpr math::Vec3 kDefaultRemotePlayerColor{0.85F, 0.25F, 0.25F};

/// One other player, drawn as an instance of the mesh SetCharacterMesh
/// uploaded for its character (ADR-0042). position is where that mesh's own
/// local origin lands (matches physics::BodyState::position, a player's
/// feet - the same convention the character's mesh was cooked around, ADR-
/// 0041), drawn as authored: a stance shows once animation poses the mesh,
/// not before. The renderer doesn't know whose player this is;
/// ClientRuntime maps presentation::RemotePlayer into this (no presentation,
/// physics or protocol header here).
struct RemotePlayer {
  math::Vec3 position{};
  /// Where it faces, in radians about +Y: 0 looks down -Z, and a positive yaw
  /// turns left (a view's yaw). Its mesh turns with it about its origin.
  float yaw = 0.0F;
  math::Vec3 color = kDefaultRemotePlayerColor;
  /// The character, by its path, whose mesh this player is drawn with; a player
  /// whose character has no mesh is not drawn.
  std::string character;
};

/// One tracer, in world space: a streak from tail, where its bullet was a tick
/// ago, to head, where it is now, bright at the head.
struct Tracer {
  math::Vec3 head{};
  math::Vec3 tail{};
};

/// A glow at one point that fades out: an impact or a muzzle flash.
struct Glow {
  math::Vec3 position{};
  /// How much of it is left: 1 when it appears, down to 0 when it is gone.
  float fade = 1.0F;
};

/// The fight, as drawn this frame (ADR-0024's Presentation State).
struct CombatEffects {
  std::vector<Tracer> tracers;
  std::vector<Glow> impacts;
  std::vector<Glow> muzzle_flashes;
};

/// What shows over the frame, at its center: the crosshair and the hit marker.
struct Overlay {
  bool crosshair = false;
  bool hit_marker = false;
};

/// Draws into the client's single window, which it borrows, with the GPU device
/// and swapchain it owns. The client constructs exactly one, on the Main/Render
/// thread.
class Renderer {
 public:
  /// Creates the GPU device per config, to draw into window - a hidden one for
  /// a test that only reads frames back (CaptureFrame). window must outlive
  /// this Renderer. Throws std::runtime_error if device creation fails - there
  /// is no recoverable path for a client that can't render: no Vulkan 1.3, no
  /// device of config's adapter, or Validation::kRequired without its layer.
  ///
  /// Under validation, the constructor, RenderFrame and CaptureFrame also
  /// throw std::runtime_error once the validation layer has reported an
  /// error: API misuse is a bug, and fails where it is found.
  Renderer(const window::Window& window, const Config& config);

  /// The device and swapchain are unique hardware resources owned through Impl
  /// (see the .cpp) - ~Renderer waits for the GPU to go idle before tearing
  /// them down.
  ~Renderer();

  /// Non-copyable/non-movable, same reasoning as ClientRuntime (the sole
  /// owner ties device lifetime to the thread that constructed it).
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  /// Draws and presents one frame - the actual GPU work, at the window's size
  /// as of its last Poll. Call this at the app's target presentation rate
  /// (e.g. vsynced to the display), independently of how often the window is
  /// drained. From the Main/Render thread.
  ///
  /// Draws the scene last passed to SetScene (just the cleared frame and the
  /// debug HUD's lines until then).
  void RenderFrame();

  /// Reads back the frame the last RenderFrame drew, waiting for the GPU to
  /// finish it; empty (0 by 0) before the first RenderFrame. Slow: for tests
  /// and screenshots, not for every frame. From the Main/Render thread.
  [[nodiscard]] CapturedFrame CaptureFrame();

  /// Replaces the drawn scene, uploading its geometry to the GPU. From the
  /// Main/Render thread. Throws std::runtime_error if a mesh index is out
  /// of range for its positions or the scene has too many vertices to draw.
  void SetScene(const Scene& scene);

  /// Replaces the camera the next RenderFrame draws from, leaving scene
  /// geometry untouched. Unlike SetScene, this touches no GPU resource -
  /// cheap enough to call once every RenderFrame, the same shape as
  /// SetRemotePlayers. From the Main/Render thread.
  void SetCamera(const Camera& camera);

  /// Sets the mesh every RemotePlayer of character is drawn with from then on,
  /// in the character's own root space (ADR-0041); meshes stay for the life of
  /// the Renderer. Not meant to be called every frame: the client calls it in
  /// the Lobby, never during a match (ADR-0043).
  /// From the Main/Render thread. Throws std::runtime_error if a mesh index is
  /// out of range for its positions.
  void SetCharacterMesh(const std::string& character, const SceneMesh& mesh);

  /// Replaces the drawn remote-player instances via a persistently-mapped
  /// host-visible buffer - unlike SetScene/SetCharacterMesh, cheap enough to
  /// call once every RenderFrame. Draws every instance it is given: how many
  /// players there can be is the protocol's business, not the renderer's. The
  /// buffer grows only when a call needs more room than any before it, so a
  /// steady player count never allocates. From the Main/Render thread. An
  /// empty span draws nothing - how a player who left disappears; a player
  /// whose character has no mesh is skipped.
  void SetRemotePlayers(std::span<const RemotePlayer> remote_players);

  /// Replaces the drawn tracers, impacts and muzzle flashes, each turned to
  /// face the camera last passed to SetCamera - so call it after that, once
  /// every RenderFrame. Glows add light to what is behind them, and are hidden
  /// by what is in front. Cheap enough to call every frame, the same way as
  /// SetRemotePlayers. From the Main/Render thread.
  void SetCombatEffects(const CombatEffects& effects);

  /// Replaces what shows over the frame, sized in pixels whatever the window's
  /// size, over everything else but the debug HUD. Cheap enough to call every
  /// frame. From the Main/Render thread.
  void SetOverlay(const Overlay& overlay);

#if AUGUSTA_DEBUG_TOOLS
  /// Replaces the debug HUD's text lines, drawn green in the frame's top left,
  /// one under the other, over everything else, by every RenderFrame from then
  /// on; none until the first call. Cheap enough to call every frame. From the
  /// Main/Render thread.
  void SetTextLines(std::vector<std::string> lines);

  /// Replaces the Debug overlay shapes the next RenderFrame draws, tessellated
  /// by augusta::debug: alpha-blended over the frame at its output resolution,
  /// after the scene and the fight's effects and before the overlay and the
  /// debug HUD, each hidden behind what is in front of it unless marked to show
  /// through walls. Shapes live one frame: the client hands the frame's shapes,
  /// none included, before every RenderFrame. The buffers they are written to
  /// grow as a frame needs and never shrink, as SetRemotePlayers' do, so it is
  /// cheap enough to call every frame. From the Main/Render thread.
  void SetDebugShapes(const debug::Shapes& shapes);

  /// Replaces the Debug panels the next RenderFrame draws over everything else,
  /// through Dear ImGui: each opens with its title bar, and no close button (a
  /// panel closes from the Debug index), at a fixed default position given by
  /// its place in panels, the first under the debug HUD and each next one
  /// cascading down and right from it, and keeps, by its id, the place a
  /// developer drags it to until the client exits. No layout is saved, so
  /// every run opens them at their default positions. Panels live one frame:
  /// the client hands the frame's panels, none included, before every
  /// RenderFrame. From the Main/Render thread.
  void SetDebugPanels(debug::Panels panels);

  /// Hands the panels the window's events, oldest first, in Debug interaction
  /// only: the cursor's position, the mouse buttons and the keys, which reach
  /// the panels by the next RenderFrame. Out of Debug interaction the client
  /// hands none, and the panels take no input. From the Main/Render thread.
  void HandleDebugPanelInput(std::span<const window::Event> events);

  /// The changes the developer made to the panels' controls in the
  /// RenderFrames since the last call, oldest first, each naming its panel and
  /// control: none for a greyed-out control. Cheap enough to call every frame,
  /// after RenderFrame. From the Main/Render thread.
  [[nodiscard]] std::vector<debug::ControlEvent> TakeDebugControlEvents();

  /// Whether Aftermath watches this Renderer's device, and where a dump would
  /// land now. From the Main/Render thread.
  [[nodiscard]] GpuCrashDumpStatus GpuCrashDumps() const;
#endif

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_H_
