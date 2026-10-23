# Debug Overlays and Panels Are Values Handed to the Renderer

Physics, collision, Smoke screens, lighting and the network each need to draw
shapes into the world and show Debug panels, and none of them may reach into the
Renderer or its GPU API. One mechanism serves them all: each frame, the client
Runtime hands the Renderer the frame's Debug overlay shapes and Debug panels as
values, the same way it hands the scene, the combat effects and the Debug HUD's
lines (ADR-0059). Like every debug tool, it exists only in a Development build
(ADR-0074).

**One vocabulary module.** `augusta_debug`, client-only and compiled only with
`AUGUSTA_DEBUG_TOOLS`, depends on `augusta_math` and the window's event types
alone, never on an area module, the Renderer, ImGui or a thread. It holds the
shape and panel types, the tessellation of shapes into lines and triangles, and
a helper that keeps shapes until a time. The Renderer draws them; the client
Runtime composes them (ADR-0021).

**Each area builds its own.** The code that turns an area's state into shapes or
a panel lives in that area's module, such as `physics::DebugShapes(world)`,
compiled only in a Development build and depending on `augusta_debug` only
there. The client Runtime calls it and passes the result on; it builds nothing
itself.

**Shapes.** A producer asks for segments, oriented boxes, spheres, capsules,
triangles and voxels, each with a colour and an alpha. `augusta_debug`
tessellates them, so the Renderer draws only lines and triangles, opaque or
translucent, and PhysX's visualization lines go in as they come. A shape is
hidden behind what is in front of it unless it is marked to show through walls,
as a Hitbox behind cover is. The Renderer draws them at the output resolution,
after tonemapping and before the Debug HUD, so a temporal upscaler does not
smear thin lines, and over whatever the frame shows, a Debug view included.

**One frame, one thread.** The shapes and panels are assembled on the
Main/Render thread every frame, after PresentationWorld's Commit and before
`RenderFrame`, from data that already reaches that thread: the Prediction State,
the Server view, and the client World's visualization, copied out there because
Presentation runs `simulate()` (ADR-0024). There is no new channel between
threads: a producer on another thread hands what it shows through the channel it
already publishes on, in a Development build only. A shape lives one frame; the
Renderer keeps nothing between frames. What must stay longer, such as a shot the
server announced once, is its producer's state, kept with the helper.

**Panels.** A Debug panel is a description: a title, then text, tables, history
plots, toggles, sliders and buttons, each control with an id. The Renderer draws
it with Dear ImGui, which stays inside the Renderer, and hands back the frame's
control changes as events from a call of their own, so `RenderFrame` is the same
call in both kinds of build. The client Runtime routes each event to the area
that built the panel, whose panel is a pure function of its state and whose
update is a pure function of its state and the event. A change to state another
thread owns travels by the way that thread already takes orders.

**Shown, or in Debug interaction.** A Debug panel that is on is shown over the
frame but takes no input: the cursor stays captured and the game plays on. F1
enters Debug interaction, from a captured or a released cursor: the cursor is
released, the Debug panels take the mouse and keyboard, the Debug index shows,
and the game gets nothing. F1 leaves it for the state it was entered from;
Escape leaves it with the cursor released, as Escape does in the game, and does
nothing else. Debug overlays never take input. Which state the client is in is a
small pure state in `augusta_debug`, driven by F1 and Escape; the client Runtime
only routes by it. In Debug interaction it hands the window's events to the
Renderer, which feeds them to ImGui, rather than to `input::Input`, which drops
whatever was held so no key stays pressed; out of it ImGui gets none, having no
platform binding of its own (ADR-0059). The cursor is locked only while `Input`
captures it and the client is out of Debug interaction. `Input` knows nothing of
debug tools.

**The Debug index.** One Debug panel, built in `augusta_debug` from the entries
the client Runtime passes it (a name and whether it is on), lists the Debug HUD
and each area's Debug panel; an area's Debug overlays are switched inside that
area's panel and stay drawn once it is closed. The index lists only the tools
the client's mode can feed, so a Replay viewer's has no Debug overlays until
they are drawn there. Turning on an overlay fed by a stream of the server's
state subscribes to that stream (ADR-0038), and turning it off unsubscribes;
when the server's config does not allow the stream, the area's panel says so.
Panels open at fixed default positions, never remembered between runs.

**Beside the Debug HUD and Debug views.** The three stay apart: the Debug HUD
remains `augusta_hud`'s text lines, Debug views still replace the frame inside
the Renderer (ADR-0067), and Debug overlays draw over whichever frame shows.
Whether the Renderer draws the HUD and the panels through one ImGui path is its
own business. The line between an overlay and a view is where the state lives: a
Debug overlay draws only values already on the CPU, such as a Map's local lights
or where its probes stand, built by the module that holds them, even when that
state is the Renderer's input; what only the GPU knows, such as a probe's light
or how many lights a screen tile traces, is a Debug view.

## Considered Options

- **A global debug-draw call** (`debug::DrawLine` from anywhere): convenient,
  but shared state written from every thread, and nothing a test can drive.
- **The Renderer drawing each shape kind itself**: one draw path per kind on the
  GPU, untested off it; tessellating on the CPU keeps the Renderer at two
  primitives.
- **Overlays the Renderer draws from its own GPU state** (probes shaded by their
  light): shows more over the world, but a second draw path inside the Renderer
  that no test reaches without a GPU; the same state reads as well in a Debug
  view.
- **Shapes with a lifetime the Renderer keeps**: one-shot producers get simpler,
  but the Renderer then holds a clock and a store; the helper gives the same to
  the producers that want it.
- **Each area writing Dear ImGui in a callback**: the most flexible panels, but
  ImGui and the Main/Render thread in every module, and panels testable only by
  drawing them.
- **A key per tool**: one press to any tool, but more tools than free function
  keys, and nothing that says which key does what.
- **A console**: every tool and setting by name, but text input, a parser and a
  character callback the Window does not have, for what toggles already do.
- **Panels that always take input when shown**: one state fewer, but a developer
  could not play with the network panel in view.
- **The building code in `augusta_debug`, or in the client Runtime**: the first
  makes the vocabulary module depend on every area, and the second puts reusable
  logic in an executable, which is glue.

## Consequences

- An area's debug tool is tested without a GPU: its shapes and its panel are
  pure functions of its state.
- The Renderer's interface grows a call for the frame's shapes, one for its
  panels, one for the window's events in Debug interaction and one that hands
  back the panels' events; `RenderFrame` keeps its signature.
- How the server's state reaches a Debug overlay is decided apart (ADR-0038,
  ADR-0044).
- `input::Input` grows a way to drop the keys and buttons held, and the client
  Runtime's frame loop routes the window's events by the Debug interaction
  state.
