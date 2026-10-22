# hud

`augusta_hud`. Client-only: the headless server never links it. A debug tool:
only a Development build has it (ADR-0074).

## Responsibility

What the DebugHud shows: the system metrics the client measures - the frame
rate, averaged over recent frames, and the connection's numbers - turned into
the HUD's text lines, free of any GPU API.

## Not its responsibility

- Drawing the lines: renderer.
- Sourcing the connection's numbers and handing the lines over: the client
  runtime.
- Gameplay state: the HUD shows system metrics only.

## Decisions

[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md).
