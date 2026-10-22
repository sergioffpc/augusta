# debug

`augusta_debug`. Client-only: the headless server never links it. A debug tool:
only a Development build has it (ADR-0074).

## Responsibility

The vocabulary every area's debug tools are written in (ADR-0075): the Debug
overlay shapes an area asks to be drawn into the world, as values, and their
tessellation into the lines and triangles the Renderer draws, free of any GPU
API. It depends on `augusta_math` and the window's event types alone, never on
an area module, the Renderer, ImGui or a thread.

## Not its responsibility

- Drawing the shapes: renderer.
- Turning an area's state into shapes: that area's module.
- Handing a frame's shapes to the Renderer: the client runtime.
- The Debug HUD's lines: hud.

## Decisions

[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
