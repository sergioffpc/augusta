# Renderer: NVRHI

> Supersedes ADR-0009.

The Windows client renders through NVIDIA's NVRHI on D3D12, in a GLFW window.
Both come from the repository's vcpkg manifest, for Windows only. The
implementation sits behind the `augusta::renderer::Renderer` interface, which
its callers use unchanged.

**Why not Falcor.** Falcor (ADR-0009) is a research framework with no upstream
commits since January 2025. Building it pulls a 2022 packman bundle and an
embedded Python, and the client's zip (ADR-0058) ships all of it: FreeImage,
assimp, OpenVDB, USD, NRD, hdf5 and Python. Nightly `sbom-scan` finds over a
hundred high or critical CVEs in them, and patching an abandoned dependency
doesn't scale. Through Falcor the client uses only a window, a D3D12 device and
swapchain, a rasterization pipeline, its Slang shaders and a debug HUD.

**Why NVRHI.** It is MIT-licensed, maintained by NVIDIA, in vcpkg, and is what
NVIDIA's RTX Kit SDKs (DLSS, NRD, RTXDI) build on. Its Vulkan backend stays
unused but keeps a Linux client possible. The alternatives weighed:

- **Donut** is a sample framework with its own scene and main loop, and is not
  in vcpkg.
- **slang-rhi** compiles Slang at runtime, and is not in vcpkg.
- **Raw D3D12** means more code, and no Vulkan path.

## Decisions

- **Window and input.** GLFW, owned by the Renderer as before: there is still no
  separate Window module. GLFW's callbacks feed `input::EventSink`. Cursor lock
  is GLFW's disabled cursor with raw mouse motion, which needs no patch.
- **Shaders.** Slang (ADR-0014), compiled to DXIL at build time by `slangc` from
  vcpkg's `shader-slang`, through DXC from vcpkg's `directx-dxc`: both are host
  tools in the manifest. The DXIL is embedded in the executable, so there are no
  shader files to ship or find, and the client ships neither `slang.dll` nor
  `dxcompiler.dll`.
- **Debug HUD.** Dear ImGui from vcpkg, drawn by a small ImGui renderer on NVRHI
  adapted from Donut's (MIT), with a freely licensed monospace font kept in the
  repository.
- **Aftermath.** Optional, autodetected from an installed Nsight Graphics as
  before, and attached to the D3D12 device NVRHI exposes.
- **Dropped.** The D3D12 Agility SDK, NVAPI, packman and Falcor's Python.
- **Testing.** `Config` can choose WARP, the software adapter, and a hidden
  window, and `Renderer::CaptureFrame` reads the last frame back. A smoke test
  drives the Renderer through its interface on CI's Windows runner, which has no
  GPU, and checks the pixels it drew.

## Consequences

The NVRHI Renderer is its own library target, `augusta_renderer_nvrhi`, built
and tested in CI beside Falcor's but linked by no executable until it draws all
that Falcor's does. Then `augustac` switches to it and Falcor is removed from
the repository. The two are never a runtime choice. Published `falcor-*`
releases stay as they are.
