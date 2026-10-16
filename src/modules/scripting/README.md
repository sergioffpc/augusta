# scripting

`augusta_scripting`, `augusta_lua_sandbox`. Server-only: no client links it.

## Responsibility

Runs a scenario's Game policy: one Lua script whose hooks are called by name,
each handed a read-only plain table and answering with plain data, under an
instruction limit, so a failing hook fails its call and never the engine.
`augusta_lua_sandbox` is the restricted environment it and the Parameters script
run in.

## Not its responsibility

- Validating what a hook returns into typed actions: simulation
  (`augusta/policy_actions.h`).
- Loading the script from the pack: the server.
- Any client-side policy: game policy is server-authoritative.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0039](../../../docs/adr/0039-lua-data-driven-configuration.md).
