# input

`augusta_input`. Client-only: the headless server never links it.

## Responsibility

The client's device-facing vocabulary of player input - raw key and mouse
events, the EventSink they are pushed to, the keymap - and sampling what
accumulated into one tick's Command, safely across the Main/Render thread that
pushes and the Prediction thread that samples.

## Not its responsibility

- Owning the window and pumping its events: renderer, which pushes them here.
- The Command type: gameplay.
- Reading the bindings from the config file: config (`augusta_client_config`).

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md).
