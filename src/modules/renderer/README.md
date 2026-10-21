# renderer

`augusta_renderer`, `augusta_renderer_vertices`,
`augusta_renderer_vulkan_device_choice`. Client-only: the headless server never
links it.

## Responsibility

Draws through NVRHI on Vulkan with Slang shaders into the client's single
window, which it is lent: it has the window make its Vulkan surface, creates its
GPU device and swapchain, and draws the Presentation State - the scene, remote
players, the fight's effects, the overlay - and the debug HUD's text lines over
them, rendering a frame apart from when the window's events are drained.

## Not its responsibility

- The window and its device events, and draining them: window, which the client
  runtime owns and polls.
- Input: the renderer knows nothing of it.
- What the debug HUD's lines say: hud.
- What is shown and where the camera is: presentation.
- How often each call runs: the client runtime.

## Decisions

[ADR-0014](../../../docs/adr/0014-shading-language.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md).
