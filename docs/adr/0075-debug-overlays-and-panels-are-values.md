# Debug Overlays and Panels Are Values Handed to the Renderer

Physics, collision, Smoke screens and the network each need to draw shapes into
the world and show Debug panels, and none of them may reach into the Renderer or
its GPU API. One mechanism serves them all: each frame, the client Runtime hands
the Renderer the frame's Debug overlay shapes and Debug panels as values, the
same way it hands the scene, the combat effects and the Debug HUD's lines
(ADR-0059). Like every debug tool, it exists only in a Development build
(ADR-0074).

**One vocabulary module.** `augusta_debug`, client-only and compiled only with
`AUGUSTA_DEBUG_TOOLS`, depends on no other module of the game. It holds the
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
it with Dear ImGui, which stays inside the Renderer, and `RenderFrame` returns
the frame's control changes as events. The client Runtime routes each event to
the area that built the panel, whose panel is a pure function of its state and
whose update is a pure function of its state and the event. A change to state
another thread owns travels by the way that thread already takes orders.

**Beside the Debug HUD and Debug views.** The three stay apart: the Debug HUD
remains `augusta_hud`'s text lines, Debug views still replace the frame inside
the Renderer (ADR-0067), and Debug overlays draw over whichever frame shows.
Whether the Renderer draws the HUD and the panels through one ImGui path is its
own business.

## Considered Options

- **A global debug-draw call** (`debug::DrawLine` from anywhere): convenient,
  but shared state written from every thread, and nothing a test can drive.
- **The Renderer drawing each shape kind itself**: one draw path per kind on the
  GPU, untested off it; tessellating on the CPU keeps the Renderer at two
  primitives.
- **Shapes with a lifetime the Renderer keeps**: one-shot producers get simpler,
  but the Renderer then holds a clock and a store; the helper gives the same to
  the producers that want it.
- **Each area writing Dear ImGui in a callback**: the most flexible panels, but
  ImGui and the Main/Render thread in every module, and panels testable only by
  drawing them.
- **The building code in `augusta_debug`, or in the client Runtime**: the first
  makes the vocabulary module depend on every area, and the second puts reusable
  logic in an executable, which is glue.

## Consequences

- An area's debug tool is tested without a GPU: its shapes and its panel are
  pure functions of its state.
- The Renderer's interface grows a call for the frame's shapes and one for its
  panels, and `RenderFrame` returns the panels' events.
- How a developer opens and closes a Debug panel or overlay while the game runs,
  and how the server's state reaches one, are decided apart.
