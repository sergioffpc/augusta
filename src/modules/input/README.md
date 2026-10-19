# input

`augusta_input`. Client-only: the headless server never links it.

## Responsibility

Turns the window's device events into player input: the controls, the keymap
that binds them to the window's keys and the names augustac.yaml spells both by,
and what the events the client runtime hands it accumulate into, sampled as one
tick's Command, safely across the Main/Render thread that hands events over and
the Prediction thread that samples.

## Not its responsibility

- The window, its keys and its events: window, which input links only for the
  events' type (`augusta_window_events`).
- When the events are drained and handed over: the client runtime.
- The Command type: gameplay.
- Reading the bindings from the config file: config (`augusta_client_config`).

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md).
