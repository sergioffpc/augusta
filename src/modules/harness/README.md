# harness

`augusta_harness`. Client-only: the headless server never links it.

## Responsibility

Where anything that plays talks to the server: a Session is the client's network
connection and PredictionWorld without a window or a GPU, so the client, a
scripted Agent and an automated test play the same way. It holds the Runner that
runs a Session in real time, the client's edge with the Networking Protocol
(`augusta/harness_wire.h`), the Intents an Agent acts through, the Captured
player's reenactment and the netcode statistics a run leaves.

## Not its responsibility

- The window, input devices, rendering, audio and PresentationWorld:
  `src/client` wires those around a Session.
- Predicting a tick: prediction.
- Reading a capture file: capture.
- Python bindings for Agents: `tools/agent`.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0021](../../../docs/adr/0021-client-runtime-decomposition.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md),
[ADR-0052](../../../docs/adr/0052-python-scripted-agents.md).
