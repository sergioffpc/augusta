# client

`augusta_client`, `augusta_client_application`, `augusta_client_content`,
`augusta_client_frame_mapping`, `augusta_client_lobby_readiness`,
`augusta_client_net_stats`. Client-only: the headless server never links it.

## Responsibility

The client Runtime, augustac's: it owns one of every client module and runs the
Harness live, with its config, its Main/Render thread and the wiring of the
Harness to the renderer, the HUD, input, PresentationWorld and audio. With it,
what the Runtime is made from and decides at its edges:

- What the client loads from its verified pack: the scene, the characters other
  players bring, the map's collision and every cue's sound.
- How augustac's phases classify their failures behind the application boundary.
- The conversions between the harness's, presentation's and the renderer's
  types.
- What the client loads in the Lobby before it reports Ready.
- The connection's numbers as the debug HUD shows them and Nsight Systems plots
  them.

## Not its responsibility

- The entry point, the command line and the Lifecycle it runs the Runtime in:
  augustac (`src/client`).
- Playing: the Harness sends commands, receives the server's state and predicts.
- The window, drawing, input, audio and what the HUD's lines say: window,
  renderer, input, audio and hud, which the Runtime only wires together.
- The Agent's Runtime: agent, under `tools/agent/native`.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0021](../../../docs/adr/0021-client-runtime-decomposition.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md),
[ADR-0043](../../../docs/adr/0043-lobby-and-match-lifecycle.md),
[ADR-0059](../../../docs/adr/0059-renderer-nvrhi.md),
[ADR-0060](../../../docs/adr/0060-executables-are-glue.md).
