# window

`augusta_window`, `augusta_window_events`. Client-only: the headless server
never links them.

## Responsibility

The client's single OS window, a GLFW one, and its device events: it opens the
window, maps GLFW's keys, mouse buttons and cursor onto the engine's own Key and
events, and returns the events received since the last `Poll()`, apart from when
frames are presented. It finds the system's Vulkan loader and makes the window's
Vulkan surface, so GLFW never leaves the module; beyond Vulkan's handles it
knows no GPU API, and it knows no input.

## Not its responsibility

- Creating and owning the window and its Vulkan surface, and all other Vulkan:
  renderer.
- Turning the events into Commands, and which key does what: input.
- When the events are drained and who gets them: the client runtime.

## Decisions

[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md).
