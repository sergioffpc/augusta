# Client World Phase Pipelines

PredictionWorld runs 5 phases (CommandIngestion, Reconciliation, Movement, WeaponHandling, Commit) — no Ballistics/HitDetection/Damage/Scripts-Behaviours; those remain exclusively server-side, so the client predicts only immediate feedback, never a bullet's outcome. PresentationWorld runs 5 phases (Interpolation, Camera, Animation, AudioCues, Commit), translating the fixed-tick Prediction State into smooth, frame-rate-independent visuals/audio. Neither client world contains a Scripts/Behaviours phase — game policy is exclusively server-authoritative.
