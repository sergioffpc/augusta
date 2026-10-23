# presentation

`augusta_presentation`. Client-only: the headless server never links it.

## Responsibility

Orchestrates PresentationWorld: the client's ECS pipeline, run once per render
frame on the Main/Render thread, turning the fixed-tick Prediction State and the
server's Authoritative State into frame-rate-independent Presentation State. It
owns the local view and camera, remote-player interpolation, reconciliation's
visual correction, tracers and effects, the animation engine and which audio
cues play.

Each frame it also reports the interpolation buffer's slack: how far the newest
Authoritative State update is ahead of the time remote players are shown at, in
seconds, positive while updates wait and zero or below once the buffer has run
dry. The Network Debug panel reads it against the Interpolation delay.

## Not its responsibility

- Drawing: renderer. Playing sound: audio.
- Predicting the local player: prediction.
- Deciding hits on players: simulation; a tracer is a visual only.
- Cosmetic bodies and Props' kinematic poses: the client-only dynamics module,
  which arrives with the client's first dynamic body, as does the Dynamics phase
  that calls it.

## Decisions

[ADR-0004](../../../docs/adr/0004-reconciliation-model.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0044](../../../docs/adr/0044-shot-lag-compensation-and-replication.md),
[ADR-0045](../../../docs/adr/0045-dynamic-bodies.md).
