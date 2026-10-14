# Authoritative simulation standard

**Objective:** the authority decides all state that matters, each step is
reproducible from its inputs to the degree the design promises, and clients
converge on the authority's state. Load for fixed-step simulations, client
prediction and reconciliation, replication, physics integration, lag
compensation, and replay.

## Questions

- **Authority:** which state does the authority own, and is there any path where
  a client's message sets that state without validation? Do client and authority
  run the same rules, and where they differ, which wins?
- **Step:** what does one tick consume and produce? Is it a function of its
  inputs and prior state, or does it read clocks, I/O, random sources or thread
  timing? Is the step order of systems fixed and documented?
- **Determinism:** what determinism does the design promise (bit-exact, same
  platform, approximate)? Does code rely on more than that, for example by
  comparing predicted and authoritative values exactly?
- **Time:** are tick counts, wall time and network time kept distinct? Are units
  consistent across the simulation, physics and presentation?
- **Prediction and reconciliation:** what does the client keep to replay, and is
  it bounded? On correction, is state restored completely (not partially) before
  replay? What happens to inputs the authority never acknowledges?
- **Replication:** does every state change that a client needs reach it? Can a
  client see an inconsistent mix of two ticks? What happens to entities created
  and destroyed between two updates?
- **Lag compensation and history:** how far back does history go, who bounds the
  rewind, and can a client claim a time outside it?
- **Replay and capture:** does replaying recorded inputs reproduce the recorded
  outcome to the promised degree, and is divergence detected?

## Failure patterns

- A client-sent value (position, hit, timestamp) trusted by the authority.
- A system reading wall-clock time inside a tick.
- Partial state restore before replay, leaving stale components.
- Unbounded input history when acknowledgements stop.
- Entity identifiers reused while a client still references the old entity.
- A rewind window a client can extend by lying about its delay.

## Evidence required

The state, the path that changes it, and the authority check present or missing
on that path; or the input sequence under which client and authority diverge
beyond what the design accepts.

## Verification

Deterministic tests that run a tick sequence twice and compare; property tests
of convergence; impaired-link netcode tests; replaying a capture and comparing
outcomes; a crafted client message in a test.
