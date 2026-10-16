# Executables are Glue

augustad, augustac and every C++ executable under `tools/` hold only Glue: their
entry point, their phases from config to runtime, the threads they own, the
modules they build and the wiring of one module's output into another's input.
Mechanism lives in the modules under `src/modules/`, and Game policy in the
scenario's Lua scripts (ADR-0022). The three are exclusive: no code is two of
them.

Code inside an executable can be tested only through the whole executable, and
it is where every feature has landed: the Host, the server and client runtimes
and the two `CMakeLists.txt` are where most changes to the engine touch. The
Harness already shows the other way: the client's network connection and
PredictionWorld are a module, tested by hand-driven ticks, and the client's
runtime only adds threads and a clock around them.

## Decisions

- **What is Glue.** Anything worth a test of its own is not Glue and belongs in
  a module. Glue is tested only end to end, through the executable's application
  or runtime.
- **What is a module.** One building block of ARCHITECTURE §5: a directory
  `src/modules/<module>/` with one concept and one responsibility at its
  interface. It may build several internal targets, as the renderer and config
  modules do. A new block enters §5 before it gets a directory. Code is grouped
  by what changes together, not one module per file.
- **One consumer is enough.** A module may be used by one side only, scoped
  `CLIENT_ONLY` or `SERVER_ONLY` (ADR-0006), as the renderer, simulation and
  scripting modules are.
- **Acting on Game policy.** Applying a Policy action is a module's work; the
  executable only routes it there.
- **The Host.** The server's socket and SimulationWorld without threads or a
  clock are a `SERVER_ONLY` module, the counterpart of the Harness. augustad's
  runtime adds its threads, its clock, the metrics endpoint (ADR-0049) and its
  lifecycle.
- **Conversions at a seam.** A conversion between two modules' types lives in
  the module whose seam it crosses, as the Harness's own wire conversions do. A
  conversion both sides make lives in the wire module.
- **Names.** A module's targets are `augusta_<module>`, its headers under
  `include/augusta/`, its namespace `augusta::<module>` or one per concept, as
  in the gameplay module. The `augustad_` and `augustac_` prefixes name Glue
  only: `<executable>`, `<executable>_application` and `<executable>_runtime`.
- **Checked at configure time.** `cmake/AugustaModule.cmake` refuses a target
  under `src/server/` or `src/client/` other than those three, a unit test that
  links an executable's target, and an end-to-end test that links one other than
  its application or runtime. Code that predates this decision is a named
  exception in the check; the list only shrinks, each migration removing its
  entries, and the check drops it once it is empty.
- **Existing code.** Migrated gradually, one cohesive cluster at a time, never
  in one change.
- **Python tools** keep their own project layout (`tools/<tool>/`) and are not
  bound by this decision.

## Considered Options

- **A module only once a second executable needs it**: rejected. It keeps
  Mechanism that one side alone uses inside that side's executable, testable
  only through it, and the renderer, simulation and scripting modules each
  already have a single consumer.
- **A module per testable piece**: rejected. It fragments one concept over many
  targets, so understanding it means reading all of them; cohesion sets the
  module, and finer splits stay internal targets.
