# wire

`augusta_wire`. Shared: linked by both the client and the server.

## Responsibility

The conversions both peers make between the Networking Protocol's plain types
and the shared core's (a Command, a stance, a pack's hash), converted here once.
Pure field-by-field copies.

## Not its responsibility

- Each peer's own types: `src/server/wire.h` and `augusta/harness_wire.h`.
- Whether a value is acceptable: whoever takes it in.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md).
