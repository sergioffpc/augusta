# presentation

`augusta_presentation`. Client-only: the headless server never links it.

## Responsibility

Orchestrates PresentationWorld: the client's ECS pipeline, run once per render
frame on the Main/Render thread, turning the fixed-tick Prediction State and the
server's Authoritative State into frame-rate-independent Presentation State. It
owns the local view and camera, remote-player interpolation, reconciliation's
visual correction, tracers and effects, the animation engine and which audio
cues play. In a Development build (ADR-0074) it also holds the Physics Debug
overlays (ADR-0075): which are on, the Physics Debug panel that switches them,
and each frame's shapes, built from the plain values physics and gameplay hand
it: every body the newest Authoritative State update reported, as its capsule,
and every other player's Hitboxes where the frame shows that player, placed by
gameplay's shared placing and outlined in one colour per body part.

## Not its responsibility

- Drawing: renderer. Playing sound: audio.
- The shape and panel vocabulary, and which tools the Debug index lists: debug,
  and the client runtime.
- Predicting the local player: prediction.
- Deciding hits on players: simulation; a tracer is a visual only. Where a
  Hitbox stands: gameplay, which the server judges with too.
- Cosmetic bodies and Props' kinematic poses: the client-only dynamics module,
  which arrives with the client's first dynamic body, as does the Dynamics phase
  that calls it.

## Decisions

[ADR-0004](../../../docs/adr/0004-reconciliation-model.md),
[ADR-0024](../../../docs/adr/0024-client-world-phase-pipelines.md),
[ADR-0044](../../../docs/adr/0044-shot-lag-compensation-and-replication.md),
[ADR-0045](../../../docs/adr/0045-dynamic-bodies.md),
[ADR-0074](../../../docs/adr/0074-debug-tools-compiled-out-of-shipping-builds.md),
[ADR-0075](../../../docs/adr/0075-debug-overlays-and-panels-are-values.md).
