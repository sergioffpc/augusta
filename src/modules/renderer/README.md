# renderer

`augusta_renderer`, `augusta_renderer_vertices`,
`augusta_renderer_vulkan_device_choice`,
`augusta_renderer_gpu_crash_dump_files`. Client-only: the headless server never
links it.

## Responsibility

Draws through NVRHI on Vulkan with Slang shaders into the client's single
window, which it is lent: it has the window make its Vulkan surface, creates its
GPU device and swapchain, and draws the Presentation State - the scene, remote
players, the fight's effects, the overlay - and the debug HUD's text lines over
them, rendering a frame apart from when the window's events are drained. In a
Development build it also draws the Debug overlays' shapes it is handed each
frame (`SetDebugShapes`), tessellated by debug, over the scene and the effects
and under the overlay and the debug HUD (ADR-0075), and the Debug panels it is
handed each frame (`SetDebugPanels`) over everything, through Dear ImGui, which
it feeds the window's events the client runtime hands it in Debug interaction
(`HandleDebugPanelInput`) and whose control changes it hands back as debug's
events (`TakeDebugControlEvents`), and it reports whether Aftermath watches its
device and where a dump would land (`GpuCrashDumps`), for the Profiling Debug
panel. A Shipping build compiles none of them: no HUD, panels or shapes pass, no
ImGui, no embedded font and no debug (ADR-0074).

## Not its responsibility

- The window and its device events, and draining them: window, which the client
  runtime owns and polls.
- Input: the renderer knows nothing of it.
- What the debug HUD's lines say: hud.
- What a Debug overlay shows, and its shapes' tessellation: each area's module
  and debug.
- What a Debug panel shows and what its events change: each area's module.
- Whether the client is in Debug interaction, and routing the panels' events:
  the client runtime, by debug's state.
- What is shown and where the camera is: presentation.
- How often each call runs: the client runtime.
- What its GPU crash dumps are named: the client's Run (client_run.h).

## Decisions

[ADR-0014](../../../docs/adr/0014-shading-language.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
