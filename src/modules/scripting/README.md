# scripting

`augusta_scripting`, `augusta_lua_sandbox`. Server-only: no client links it.

## Responsibility

The sandboxed Lua embedding: loads a script into a Lua state of its own and
calls its hooks by name, each handed a read-only plain table and answering with
plain data, under an instruction limit, so a failing hook fails its call and
never the engine. `augusta_lua_sandbox` is the restricted environment every
script, the Parameters script included, runs in.

## Not its responsibility

- Game policy (which hooks there are, the views they are handed and what their
  answers may decide): policy.
- Loading a script from the pack: scenario.

## Decisions

[ADR-0022](../../../docs/adr/0022-gameplay-scripting-language.md),
[ADR-0039](../../../docs/adr/0039-lua-data-driven-configuration.md).
