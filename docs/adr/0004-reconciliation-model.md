# Reconciliation Model

Client-side prediction is corrected against the server using smooth corrective reconciliation (snap/blend), not exact resimulation. This is driven by PhysX's documented lack of cross-platform determinism: since the client and server can't guarantee bit-identical physics results, treating prediction as approximate/visual-only and correcting smoothly is more robust than assuming a replay-and-diff approach would ever match.
