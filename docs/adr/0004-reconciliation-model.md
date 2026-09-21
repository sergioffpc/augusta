# Reconciliation Model

Client-side prediction is reconciled against the server by **restore and replay**: when an authoritative state arrives, the client puts its body at that state and steps again the commands the server has not yet acknowledged, so its present is the server's past with its own commands carried forward. The player is shown a smoothed view of the result; the simulation itself is not blended.

PhysX's documented lack of cross-platform determinism does not stand in the way. The replay does not assume it reproduces what the server would have done, only that it starts from what the server did; whatever difference is left is what the next acknowledgement corrects. What determinism rules out is comparing two runs for equality, which reconciliation never does.

Correcting by a fraction of the position, velocity and stamina error, and shifting the predicted history by the same amount, was rejected: it assumes movement is the same wherever the body is. It is wrong wherever the shifted body meets geometry, a clamp (stamina at 0 or 1), or anything the body carries besides its position (a refused stance, a refused sprint, a fall). Stepping the commands again carries all of that through the same `physics::World::Step` the prediction uses.

## Comparing like with like

The server's state always describes an earlier moment than the client's current one: the command it answers had to travel to the server, and the state back. Comparing it with the client's *current* state would read the client's own head start as an error. So:

- **Sequence numbers tie the two together.** Every command the client sends carries a sequence number (ADR-0038), and every state the server sends carries the highest sequence of the recipient's that the server has processed. That state is the server's body *after* that command.
- **The client keeps a short history** (`prediction::History`) of each command it sent and the body it predicted after it, keyed by sequence. On an update it looks up the entry for the acknowledged sequence, which is what the server's state is compared with (the same command on both sides), and discards it and everything older.
- **An acknowledgement is acted on once.** The server repeats the same acknowledged sequence while it waits for input, with a state that has moved on; that entry is already gone from the history, so nothing is restored or replayed. An acknowledgement for a command the history no longer holds is ignored the same way.

## The replay

- The body is put at the server's state with `physics::World::Restore`. The server's state does not carry what `Step` also needs, the fall and ground tracking (`physics::FallState`), so the history keeps the fall the client predicted after each command and restores that one.
- Each command still held is stepped again, oldest first, by the same fixed tick; the result replaces the state predicted after it in the history, so the next acknowledgement is compared with a state that already has this one's correction in it.
- The current tick's own command is then predicted as usual, from the replayed body.
- The cost is one `Step` per unacknowledged command per acknowledgement: 6 to 10 at the round trip NFR-02 assumes.

Stance, sprint and stamina come out of the replay like everything else: a stance the server refused stays refused, and a sprint stamina no longer allows is walked.

## Hiding the jump

The replay puts the predicted body somewhere else at once, by however much the prediction was off. That is a jump in the simulation, and the presentation hides it (`presentation::Correction`):

- `prediction::State` carries `total_correction`, the sum of every jump a replay has made. A reader that sees only some of the ticks (presentation runs once per frame, not per tick) gets the jumps between two states it saw from the difference of their totals, every one and none twice.
- The body is shown where it was, and slides to where it is: the offset fades with a time constant of 47 ms, to 4% of the jump within 9 ticks (150 ms), the correction budget of NFR-02.
- A jump of at least 2 m is shown at once, together with what was still fading: too far to look right sliding, most plausibly a respawn or teleport.

## Known limits

- While the server has no new command for a player it repeats the last movement for a short time (ADR-0038) and the acknowledged sequence stays put. The extra movement it makes is not part of any command the client sent, so it shows up as a small error at the next acknowledged sequence and is corrected like any other.
- The fall restored for the replay is the client's own prediction at that command, not the server's, which does not send it. It matters only while the body is in the air.
