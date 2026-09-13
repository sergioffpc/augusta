# Client Runtime Decomposition

ClientRuntime splits into two ECS worlds, mapped onto the two client threads from [ADR-0005](0005-threading-model.md): PredictionWorld (Simulation thread, fixed tick — input + authoritative state in, immutable prediction state out) and PresentationWorld (Main/Render thread, per-frame — prediction state in, presentation state out to Renderer and Audio).

## Consequences

Phase-level detail within each world is deferred to a future, more detailed diagram (see [ADR-0024](0024-client-world-phase-pipelines.md)).
