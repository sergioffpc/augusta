# renderer

`augusta_renderer`, `augusta_renderer_vertices`,
`augusta_renderer_debug_hud_text`, `augusta_renderer_device_choice`,
`augusta_renderer_glfw_keys`. Client-only: the headless server never links it.

## Responsibility

Draws through NVRHI on Vulkan with Slang shaders, and owns the client's single
GLFW window with its GPU device and swapchain. It pumps the window's events into
input's EventSink and draws the Presentation State - the scene, remote players,
the fight's effects, the overlay - and the debug HUD, keeping pumping events and
rendering a frame as two separate calls.

## Not its responsibility

- The input event vocabulary: input.
- What is shown and where the camera is: presentation.
- How often each call runs: the client runtime.
- Gameplay state in the debug HUD: it shows system metrics only.

## Decisions

[ADR-0014](../../../docs/adr/0014-shading-language.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md).
