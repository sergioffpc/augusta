# Reconciliation Model

Client-side prediction is corrected against the server using smooth corrective reconciliation (snap/blend), not exact resimulation. This is driven by PhysX's documented lack of cross-platform determinism: since the client and server can't guarantee bit-identical physics results, treating prediction as approximate/visual-only and correcting smoothly is more robust than assuming a replay-and-diff approach would ever match.

## Comparing like with like

The server's state always describes an earlier moment than the client's current one: the command it answers had to travel to the server, and the state back. Comparing it with the client's *current* state would read the client's own head start as an error and pull it back. So:

- **Sequence numbers tie the two together.** Every command the client sends carries a sequence number (ADR-0038), and every state the server sends carries the highest sequence of the recipient's that the server has processed. That state is the server's body *after* that command.
- **The client keeps a short history** of the body it predicted after each recent command, keyed by sequence (`prediction::History`). On an update it looks up the entry for the acknowledged sequence and compares the server's state with that one: the same command on both sides. History older than the acknowledged sequence is discarded.
- **The error is applied to the present.** The correction is applied to the client's *current* state, and to every later entry in the history. Shifting the history keeps it consistent with the corrected trajectory, so the next update measures only what is left of the error instead of counting it again.
- **An acknowledgement is acted on once.** The server repeats the same acknowledged sequence while it waits for input, with a state that has moved on; that entry is already gone from the history, so nothing is compared. An acknowledgement for a command the history no longer holds is ignored the same way.

## The correction

The decision is a pure function (`prediction::ResolveCorrection`), separate from the physics that applies it (`physics::World::Correct`, which moves the body without resetting its fall):

- An error of at least 2 m is a **snap**: the whole error is applied at once (too far to blend and look right, most plausibly a respawn or teleport the client has not caught up to).
- A smaller error is **blended**: 30% of what is left is removed per server update, one arriving per tick. The error therefore falls monotonically and never overshoots, to 4% of its size within 9 ticks (150 ms), the correction budget of NFR-02.
- Velocity and stamina are corrected by the same share. A stance the server holds that differs from the predicted one (it refused a change, e.g. under a low ceiling) is taken outright.

## Known limit

While the server has no new command for a player it repeats the last movement for a short time (ADR-0038) and the acknowledged sequence stays put. The extra movement it makes is not part of any command the client sent, so it shows up as a small error at the next acknowledged sequence and is corrected like any other.
