# Renderer: NVRHI

> Supersedes ADR-0009.

The client renders through NVIDIA's NVRHI on Vulkan, in a GLFW window, on
Windows and Linux alike. NVRHI and GLFW come from the repository's vcpkg
manifest. The implementation sits behind the `augusta::renderer::Renderer`
interface, which its callers use unchanged.

**Why not Falcor.** Falcor (ADR-0009) is a research framework with no upstream
commits since January 2025. Building it pulls a 2022 packman bundle and an
embedded Python, and the client's zip (ADR-0058) ships all of it: FreeImage,
assimp, OpenVDB, USD, NRD, hdf5 and Python. Nightly `sbom-scan` finds over a
hundred high or critical CVEs in them, and patching an abandoned dependency
doesn't scale. Through Falcor the client uses only a window, a GPU device and
swapchain, a rasterization pipeline, its Slang shaders and a debug HUD.

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
  validation layer, from vcpkg's `vulkan-validationlayers`: on in Debug builds
  and in the smoke test, never in a release package. The loader finds it through
  `VK_ADD_LAYER_PATH`, and a Renderer asked for validation without it fails to
  start. An error the layer reports is logged and thrown by the Renderer call it
  happened in, so API misuse fails where it is found.
- **Window and input.** GLFW, owned by the Renderer: there is no separate Window
  module. GLFW's callbacks feed `input::EventSink`, and its
  `glfwCreateWindowSurface` makes the Vulkan surface, the one thing that differs
  between the platforms. Cursor lock is GLFW's disabled cursor with raw mouse
  motion, which needs no patch.
- **Shaders.** Slang (ADR-0014), compiled to SPIR-V at build time by `slangc`
  from vcpkg's `shader-slang`, a host tool in the manifest. The SPIR-V is
  embedded in the executable, so there are no shader files to ship or find, and
  the client ships no `slang.dll`.
- **Debug HUD.** Dear ImGui from vcpkg, with its GLFW binding, drawn by a small
  ImGui renderer on NVRHI adapted from Donut's (MIT). Its monospace font,
  JetBrains Mono Bold (SIL Open Font License 1.1), is kept in the repository
  with its licence and embedded in the executable at build time. Its text comes
  from code free of any GPU API.
- **Aftermath.** Optional and autodetected from an installed Nsight Graphics,
  attached to the Vulkan device through `VK_NV_device_diagnostics_config` and
  `VK_NV_device_diagnostic_checkpoints`, and a no-op without it.
- **Dropped.** D3D12, DXGI, DXC, the D3D12 Agility SDK, NVAPI, packman and
  Falcor's Python.
- **Testing.** `Config` can choose `kSoftware`, a hidden window and validation,
  and `Renderer::CaptureFrame` reads the last frame back. A smoke test drives
  the Renderer through its interface on Mesa's lavapipe, a software Vulkan
  driver, and checks the pixels it drew, so it runs on CI's runners, which have
  no GPU. On Windows lavapipe comes from vcpkg's `mesa` port, through the
  manifest's `lavapipe` feature, for tests only, and the test's environment
  makes it the loader's only driver (`VK_DRIVER_FILES`). The same feature has
  vcpkg's `vulkan-loader`, which the test loads where no GPU driver installed
  the system's; nothing links it, and no package ships it.

## Consequences

The Renderer is the library target `augusta_renderer`, which `augustac` links.
Nothing of Falcor's is in the repository, the client's zip (ADR-0058) or its
SBOM, and the zip holds no Vulkan loader, validation layer, DXC or Slang
library. Building lavapipe on Windows compiles LLVM, which needs Visual Studio's
ATL component; it is slow once, then served from the vcpkg binary cache.
Published `falcor-*` releases stay as they are.
