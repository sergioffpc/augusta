# scenario

`augusta_scenario`, `augusta_scenario_parameters_loader`,
`augusta_scenario_policy_loader`, all in `augusta::scenario`. Server-only: the
client never links them.

## Responsibility

What the server loads from its verified pack at startup: the scenario it serves,
the Parameters its simulation and every client run on, and its Game policy, read
through Lua by the two loaders. It is loaded before any socket or thread opens,
so a pack without usable content fails like a bad pack does, and the Host is
handed it already loaded.

## Not its responsibility

- Verifying and reading the pack itself: assets.
- Running the Game policy each tick: policy and simulation.
- Sending the Parameters to clients: host.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0039](../../../docs/adr/0039-lua-data-driven-configuration.md).
