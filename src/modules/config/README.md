# config

`augusta_config`, `augusta_client_config`, `augusta_server_config`. Shared:
linked by both the client and the server. `augusta_client_config` is client-only
and `augusta_server_config` server-only.

## Responsibility

Reads each executable's startup settings from its YAML file instead of
command-line arguments: the shared mechanism (`--config`, `--help`, `--version`,
sections, dotted keys, strict errors, paths relative to `base_dir`), and each
executable's own keys - `augustac.yaml` with the player's controls and
`--reenact`, and `augustad.yaml`.

## Not its responsibility

- The simulation's Parameters: gameplay's type, loaded by the server from Lua.
- Naming keys and controls: input.
- Acting on the settings: the executables.

## Decisions

[ADR-0034](../../../docs/adr/0034-runtime-config-file.md).
