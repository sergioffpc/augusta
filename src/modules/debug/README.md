# debug

`augusta_debug`. Client-only: the headless server never links it. A debug tool:
only a Development build has it (ADR-0074).

## Responsibility

The vocabulary every area's debug tools are written in (ADR-0075): the Debug
overlay shapes an area asks to be drawn into the world, as values (segments,
triangles, and oriented boxes, spheres, capsules and voxels drawn as a wireframe
or filled), their tessellation into the lines and triangles the Renderer draws,
free of any GPU API, and the keep-until helper that keeps a one-off event's
shapes drawn until a time the producer gives it. It also holds the Debug panels
an area asks to be shown, as descriptions of text, tables, history plots,
toggles, sliders and buttons, and the control events a developer's change to one
of them comes back as, naming the panel and the control. It depends on
`augusta_math` and the window's event types alone, never on an area module, the
Renderer, ImGui or a thread.

## Not its responsibility

- Drawing the shapes: renderer.
- Drawing and operating the panels, through Dear ImGui: renderer.
- Building a panel from an area's state, and updating that state from the
  panel's events: that area's module.
- Routing a panel's events to the area that built it: the client runtime.
- Turning an area's state into shapes: that area's module.
- Handing a frame's shapes to the Renderer: the client runtime.
- The Debug HUD's lines: hud.
- The clock the keep-until helper is read against: its producer, which passes
  the time.

## Decisions

[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
