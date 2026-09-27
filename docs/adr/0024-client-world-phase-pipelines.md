# Client World Phase Pipelines

PredictionWorld runs 5 phases (CommandIngestion, Reconciliation, Movement, WeaponHandling, Commit) — no Ballistics/HitDetection/Damage/Scripts-Behaviours; deciding a bullet's outcome (where it hits, what damage it does) remains exclusively server-side, so the client predicts only immediate fire feedback, never a bullet's outcome, and learns of its hits only from the server's Hit confirmation (ADR-0044). PresentationWorld runs 5 phases (Interpolation, Camera, Animation, AudioCues, Commit), translating the fixed-tick Prediction State into smooth, frame-rate-independent visuals/audio.

Drawing a bullet is not deciding it: every client's PresentationWorld draws each Shot the server announces (ADR-0044) by computing its trajectory with the same ballistics math the server uses, for the tracer and the impact on the Map. That trajectory is a visual only: hits on players are drawn only from the server's hit messages, never from it. So the trajectory math of `augusta_ballistics` is shared by client and server, while HitDetection and Damage run only in SimulationWorld.

Neither client world contains a Scripts/Behaviours phase — game policy is exclusively server-authoritative.
