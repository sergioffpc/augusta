# Executables are Glue

augustad, augustac and every C++ executable under `tools/` hold only Glue: their
entry point, which runs one Runtime behind the application boundary (ADR-0033).
Mechanism lives in the modules under `src/modules/` and in the Runtimes, each
beside the executable it runs, and Game policy in the scenario's Lua scripts
(ADR-0022). The three are exclusive: no code is two of them.

Code inside an executable can be tested only through the whole executable, and
it is where every feature used to land: the Host, the server and client runtimes
and the two `CMakeLists.txt` were where most changes to the engine touched. The
Harness already showed the other way: the client's network connection and
PredictionWorld are a module, tested by hand-driven ticks.

## Decisions

- **What is Glue.** Anything worth a test of its own is not Glue and belongs in
  a module. Glue is the entry point alone, tested only end to end.
- **What is a module.** One building block of ARCHITECTURE Â§5: a directory
  `src/modules/<module>/` with one concept and one responsibility at its
  interface. It may build several internal targets, as the renderer and config
  modules do. A new block enters Â§5 before it gets a directory. Code is grouped
  by what changes together, not one module per file.
- **One consumer is enough.** A module may be used by one side only, scoped
  `CLIENT_ONLY` or `SERVER_ONLY` (ADR-0006), as the renderer, simulation and
  scripting modules are.
- **Acting on Game policy.** Applying a Policy action is a module's work; the
  Runtime only routes it there.
- **The Harness and the Host.** The client and the server without threads or a
  clock are modules a test drives by hand.
- **The Runtimes.** What runs the Harness or the Host live is a building block
  of its own, kept in its executable's directory rather than under
  `src/modules/`, since it has no consumer but that executable: client in
  `src/client/` (augustac's), server in `src/server/` (augustad's) and agent
  (the Agent's, in `tools/agent/native` as the tools layout has it), each in the
  namespace of its name (`augusta::client`, `augusta::server`,
  `augusta::agent`); the Host is `augusta::host`'s. A Runtime is built as a
  module is - internal targets named `augusta_<runtime>_*`, public headers under
  `include/augusta/`, a README stating its responsibility - and its executable
  is one more target beside them. A Runtime adds its executable's configuration,
  its threads, its clock and the wiring of the services the Harness or the Host
  lacks: the window, the GPU, audio, the metrics endpoint (ADR-0049). End-to-end
  tests link a Runtime, never an executable.
- **Conversions at a seam.** A conversion between two modules' types lives in
  the module whose seam it crosses. Every conversion between the game's state
  and messages is the Link's.
- **Names.** A module's targets are `augusta_<module>`, its headers under
  `include/augusta/`, its namespace `augusta::<module>` or one per concept, as
  in the gameplay module. An executable's own target holds its entry point
  alone.
- **Checked at configure time.** `cmake/AugustaModule.cmake` refuses a target
  that links an executable, and an executable that links more than the
  application, its Runtime (any target defined in the executable's own
  directory), crash, failure, logging and version. The build's own
  infrastructure that every binary links, such as a sanitizer's default options,
  is no module and passes.
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
- **Runtimes as Glue, inside the executables**: rejected. Each executable would
  link every module its Runtime wires, its Mechanism would be testable only
  through the executable, and the Agent's Runtime, linked by its Python module
  and its C++ tests, could not be shared.
- **Runtimes as modules under `src/modules/`**: rejected. A Runtime is what its
  executable runs and nothing else links it but its tests, so a module of its
  own split one program over two directories - `src/client/` with only
  `main.cpp`, `src/modules/client/` with everything it runs - without a second
  consumer to justify it.
