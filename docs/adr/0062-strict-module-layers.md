# Strict Module Layers

The modules under `src/modules/` form six Layers, and a module uses only modules
of the Layer directly below its own and of the Foundation. Two exceptions hold:
every Layer may use the Foundation, and a Runtime, whose job is wiring modules
of several Layers together, may use any Layer below. Executables stand on top,
outside the Layers, and use only the application, the one Runtime they run and
the Foundation.

Each Layer knows one interface, the one directly below it, so a change reaches
at most the Layer above. Where a module used to reach further down, the strict
rule asks for the interface the Layer below should already offer: gameplay in
front of physics, the Link in front of the network.

## The Layers

From the bottom:

| Layer         | Responsibility                                                                                                                                                                               | Modules                                                                                                                          |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| L0 Foundation | Types and utilities all code shares, knowing nothing of the game but its time and its shared limits; uses no other module.                                                                   | math, tick, limits, failure, version, logging                                                                                    |
| L1 Engine     | Wraps the external engines the Domain builds on, knowing no game rule and no Pack.                                                                                                           | physics, scripting                                                                                                               |
| L2 Domain     | The game's vocabulary: its rules and the bodies that are not rules, Game policy over the scripting sandbox, and the formats and transports that carry no game state.                         | gameplay, dynamics, policy, protocol, assets, networking, window                                                                 |
| L3 Services   | One piece of one concern each, for the Cores and Runtimes to compose: a world, a device, the Link, content, thread supervision, configuration.                                               | prediction, presentation, simulation, scenario, input, renderer, hud, audio, link, capture, content, supervisor, config, metrics |
| L4 Cores      | The Harness and the Host: client and server without threads or a clock, which a test drives by hand.                                                                                         | harness, host                                                                                                                    |
| L5 Runtimes   | Each runs a Core live for one executable: threads, clock, the executable's configuration, and the services the Core lacks (window, GPU, audio, metrics). The application boundary sits here. | client, server, agent, application                                                                                               |
| Executables   | Outside the Layers: an entry point that runs one Runtime behind the application.                                                                                                             | augustac, augustad, the Agent's Python module                                                                                    |

A module's side (`CLIENT_ONLY`, `SERVER_ONLY` or shared, ADR-0006) is
independent of its Layer.

## Decisions

- **The Foundation is closed.** Its modules use no `augusta` module, not even
  each other, so any Layer can take them without taking anything else.
- **Runtimes may use any Layer below.** A Runtime wires the services its Core
  lacks to the Core, and those services sit in several Layers; the Harness, by
  definition, has no window or GPU, so it cannot be the one that reaches them.
- **The rule is about the link graph.** What counts is what a target links. A
  type of a lower Layer may reach a higher one through a public header, as the
  window's events reach the client Runtime through the renderer and go on to
  input; otherwise each Layer would wrap every type of every Layer below it.
- **Outside the rule.** A module's internal targets, tests, the Agent's
  in-process testing server, and the Python tools (ADR-0060) are not Layered.
- **Checked at configure time.** Each module declares its Layer to
  `cmake/AugustaModule.cmake`, which refuses a link the rule forbids. It
  replaces `tests/core_boundary.cmake`, whose rules the Layers now imply;
  `tests/protocol_boundary.cmake` stays, since it holds the protocol to less
  than its Layer allows (ADR-0038).
- **Existing code.** Today's violations are named exceptions in the check; the
  list only shrinks, each migration removing its entries, and the check drops it
  once it is empty. The migration moves one cohesive cluster at a time, as
  ADR-0060's does.

## Consequences

- **Fronts the strict rule asks for.** gameplay is the only way to physics for
  the rules, and dynamics, a client-only module, for Cosmetic bodies and Props'
  kinematic poses (ADR-0045). The Link makes every conversion between the game's
  state and messages and carries them over networking; the Harness and the Host
  no longer convert (ADR-0038, ADR-0060). content reads the Map's collision,
  sounds and meshes out of the Pack, so physics and audio no longer know Packs.
- **Splits the strict rule asks for.** The window leaves the renderer, and the
  HUD becomes text lines a Runtime hands the renderer (ADR-0059). lifecycle
  becomes supervisor (Services) and application (Runtimes). policy leaves
  scripting (ADR-0022). The Runtimes leave the executables and become modules
  (ADR-0060), each owning its own configuration over the generic config reader.
  primitives becomes tick, which also takes the tick schedule (ADR-0005), and
  limits (ADR-0038). The Prediction State's type moves to gameplay, so
  presentation reads it without using prediction.
- **No Layer above may skip.** A new dependency that skips a Layer is refused at
  configure time, so it is resolved by the module directly below growing the
  interface its caller needs.

## Considered Options

- **Relaxed Layers** (a module uses any Layer below, never its own or one
  above): rejected. Today's code nearly satisfies it already, which is the
  point: it lets coupling grow unseen, so a change to physics reached ten
  modules. It would need no fronts, and so would never ask for the interfaces
  the strict rule makes explicit.
- **Pure strict Layers, without the Foundation exception**: rejected. Every
  Layer would re-export the math types, the log macros and the failure model to
  the one above.
- **Executables as a seventh Layer**: rejected. An entry point that links only
  the application and one Runtime has nothing to layer; the rule for it is
  simpler stated on its own.
- **Runtimes kept inside the executables, as Glue**: rejected. Under the strict
  rule an executable would have to link Services directly, and the Agent's
  Runtime, which its Python module and its C++ tests both link, could not be
  shared.
