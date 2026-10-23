# debug

`augusta_debug`. Client-only: the headless server never links it. A debug tool:
only a Development build has it (ADR-0074).

## Responsibility

The vocabulary the client Runtime and the Renderer share for debug tools
(ADR-0075), as values free of any GPU API: the Debug overlay shapes an area asks
to be drawn into the world (segments, triangles, and oriented boxes, spheres,
capsules and voxels drawn as a wireframe or filled), with their tessellation
into the lines and triangles the Renderer draws and a keep-until helper that
keeps a one-off event's shapes drawn until a time the producer gives, at most as
many as its capacity; the Debug panels an area asks to be shown (text, tables,
history plots, toggles, sliders and buttons), with the control events a
developer's change to one comes back as, naming the panel and the control; Debug
interaction, the state F1 enters and leaves in which the panels take the mouse
and keyboard and the game gets neither, with which of the window's events go to
the game and which to the panels, and what entering and leaving ask of the
game's input; and the Debug index, the panel the client shows in Debug
interaction listing its debug tools, each switched on or off from it. It depends
on `augusta_math` and the window's event types alone, never on an area module,
the Renderer, ImGui or a thread.

## Not its responsibility

- Drawing the shapes: renderer.
- Drawing and operating the panels, through Dear ImGui: renderer.
- Building a panel from an area's state, and updating that state from the
  panel's events: that area's module.
- Routing a panel's events to the area that built it: the client runtime.
- Which key enters Debug interaction, and letting go of the game's keys and
  releasing its cursor when asked: input, through the client runtime.
- Which tools the Debug index lists, and what switching one does: the client
  runtime.
- Turning an area's state into shapes: that area's module.
- Handing a frame's shapes to the Renderer: the client runtime.
- The Debug HUD's lines: hud.
- The clock the keep-until helper is read against: its producer, which passes
  the time.

## Decisions

[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
