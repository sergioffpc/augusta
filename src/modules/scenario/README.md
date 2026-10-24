# scenario

`augusta_scenario` and `augusta_scenario_parameters_loader`, both in
`augusta::scenario`. Server-only: the client never links them.

## Responsibility

What the server loads from its verified pack at startup: the scenario it serves,
the Parameters its simulation and every client run on, read through Lua by the
Parameters loader, and its Game policy, read by policy's loader. It is loaded
before any socket or thread opens, so a pack without usable content fails like a
bad pack does, and the Host is handed it already loaded.

## Not its responsibility

- Verifying and reading the pack itself: assets.
- Reading a character's Clips: animation (`augusta/clips.h`). Scenario only
  refuses a character whose Clips do not load.
- Running the Game policy each tick: policy and simulation.
- Sending the Parameters to clients: host.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0039](../../../docs/adr/0039-lua-data-driven-configuration.md),
[ADR-0066](../../../docs/adr/0066-character-animation.md).
