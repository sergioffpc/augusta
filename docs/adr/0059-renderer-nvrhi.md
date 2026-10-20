# Renderer: NVRHI

The client renders through NVIDIA's NVRHI on Vulkan, in a GLFW window, on
Windows and Linux alike. NVRHI and GLFW come from the repository's vcpkg
manifest. The implementation sits behind the `augusta::renderer::Renderer`
interface, which its callers use unchanged.

**Why not Falcor.** Falcor, NVIDIA's D3D12 framework the client first used, is a
research framework with no upstream commits since January 2025. Building it
pulls a 2022 packman bundle and an embedded Python, and the client's zip
(ADR-0058) ships all of it: FreeImage, assimp, OpenVDB, USD, NRD, hdf5 and
Python. Nightly `sbom-scan` finds over a hundred high or critical CVEs in them,
and patching an abandoned dependency doesn't scale. Through Falcor the client
uses only a window, a GPU device and swapchain, a rasterization pipeline, its
Slang shaders and a debug HUD.

**Why NVRHI.** It is MIT-licensed, maintained by NVIDIA, in vcpkg, and is what
NVIDIA's RTX Kit SDKs (DLSS, NRD, RTXDI) build on. The alternatives weighed:

- **Donut** is a sample framework with its own scene and main loop, and is not
  in vcpkg.
- **slang-rhi** compiles Slang at runtime, and is not in vcpkg.
- **Raw Vulkan** means more code for the same result.

**Why Vulkan alone.** One backend runs on both platforms, with one shader format
and one GPU crash-dump path. D3D12 is not kept beside it: a runtime choice
between backends would double what has to be tested, for no gain to a player.

## Decisions

- **Vulkan 1.3 minimum.** Dynamic rendering and synchronization2, which NVRHI
  draws with, are core from 1.3 on. The Renderer refuses a loader or device
  older than that when it is constructed, with an error that says so (ADR-0033).
- **The system's loader.** The client reaches Vulkan through the loader the GPU
  driver installs (`vulkan-1.dll` on Windows, `libvulkan.so.1` on Linux), found
  at runtime through GLFW, and calls it through Vulkan-Hpp's dynamic dispatcher,
  as NVRHI does. It links no loader, and none is in its package.
- **Device choice.** `Adapter::kHardware` takes a discrete GPU when there is
  one, else an integrated one. `Adapter::kSoftware` takes the first CPU-type
  device, a software rasterizer, and fails when there is none, so a test asking
  for one never runs on a GPU or on nothing. Either must support Vulkan 1.3 and
  present to the window. Neither is a setting in the client's config file.
- **Validation.** `Config::validation` runs the Renderer under Vulkan's
  validation layer, from vcpkg's `vulkan-validationlayers`, which the loader
  finds through `VK_ADD_LAYER_PATH`; it is never in a release package. A Debug
  build asks for it if available, and runs without it, after a warning, where
  the loader finds none. The smoke test requires it, and fails to start without
  it. An error the layer reports is thrown by the Renderer call it happened in,
  so API misuse fails where it is found.
- **Window and input.** GLFW, in the window module (`augusta_window`) and
  private to it: the OS window and its device events. Its header names no GLFW
  type; of GPU APIs it knows only Vulkan's handles, to find the system's loader
  and make the window's Vulkan surface with `glfwCreateWindowSurface`, the one
  thing that differs between the platforms, and it knows no input. The Renderer
  creates its window and owns it. The window's `Poll()` returns the device
  events since the last call; the client Runtime calls it on its Main/Render
  thread and hands them to the input module, which links the window only for the
  events' type, so neither the window nor the Renderer knows anything of input.
  Cursor lock is GLFW's disabled cursor with raw mouse motion, which needs no
  patch.
- **Shaders.** Slang (ADR-0014), compiled to SPIR-V at build time by `slangc`
  from vcpkg's `shader-slang`, a host tool in the manifest. The SPIR-V is
  embedded in the executable, so there are no shader files to ship or find, and
  the client ships no `slang.dll`.
- **Debug HUD.** Dear ImGui from vcpkg, without a platform binding, since the
  HUD takes no input: the Renderer gives ImGui each frame's size. It is drawn by
  a small ImGui renderer on NVRHI adapted from Donut's (MIT). Its monospace
  font, JetBrains Mono Bold (SIL Open Font License 1.1), is kept in the
  repository with its licence and embedded in the executable at build time. The
  Renderer only draws text lines; what they say is the hud module's
  (`augusta_hud`), free of any GPU API, and the client Runtime hands its lines
  to the Renderer.
- **GPU crash dumps.** Nsight Aftermath, from the SDK an installed Nsight
  Graphics bundles, which the build autodetects; without it the build and the
  client are unchanged. With it, the Renderer has Aftermath watch Vulkan devices
  before it makes its own, and turns on `VK_NV_device_diagnostics_config`
  (resource tracking and automatic checkpoints) and
  `VK_NV_device_diagnostic_checkpoints` where the device has them; a device
  without them, such as lavapipe or a GPU not NVIDIA's, is made exactly as
  without Aftermath. Once Vulkan reports the device lost, the Renderer waits a
  few seconds for the dump before it throws. A dump is a `.nv-gpudmp` beside the
  executable. The build stages the SDK's shared libraries beside the executable
  and in the client's package. The code is the same on both platforms.
- **Testing.** `Config` can choose `kSoftware`, a hidden window and validation,
  and `Renderer::CaptureFrame` reads the last frame back. A smoke test drives
  the Renderer through its interface on Mesa's lavapipe, a software Vulkan
  driver, and checks the pixels it drew, so it runs on CI's runners, which have
  no GPU. On Windows lavapipe comes from vcpkg's `mesa` port, through the
  manifest's `lavapipe` feature, for tests only: CI's `client-x64-windows` job
  and the `windows-debug` preset turn it on, and a release's build leaves it
  out. The test's environment makes it the loader's only driver
  (`VK_DRIVER_FILES`). The same feature has vcpkg's `vulkan-loader`, which the
  test loads where no GPU driver installed the system's; nothing links it, and
  no package ships it.

## Consequences

The Renderer is the library target `augusta_renderer`, which the client Runtime
links, beside `augusta_window` and `augusta_hud`. Nothing of Falcor's is in the
repository, the client's zip (ADR-0058) or its SBOM, and the zip holds no Vulkan
loader, validation layer, DXC or Slang library. Building lavapipe on Windows
compiles LLVM, which needs Visual Studio's ATL component; it is slow once, then
served from the vcpkg binary cache.
