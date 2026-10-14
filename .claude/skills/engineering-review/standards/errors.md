# Error handling and observability standard

**Objective:** every failure is detected, reported once at the layer that owns
its recovery, recovered at the right scope, and leaves resources released and
state consistent. Operators can tell what happened from logs and metrics.

## Questions

- For each fallible call on the path: is the result checked? Is the failure
  propagated, recovered, or dropped, and does that match the project's
  error-handling policy for that layer?
- What scope does each failure stop: one message, one session, one subsystem,
  the process? Is that decided in one place or ad hoc?
- On an error path, are locks released, handles closed, partial writes rolled
  back, and invariants restored?
- Does a third-party exception or error escape the layer that should convert it?
- Is a failure logged once, at the owning boundary, or at every layer it passes?
  Does any log or error message leak to a peer what it should not?
- Can an operator distinguish this failure from others (distinct code, context,
  metric)? Is anything that matters invisible?
- Does recovery retry? With what bound and backoff? Can it loop?
- Are invariant violations checked in release builds where integrity depends on
  them, or only by debug asserts?

## Failure patterns

- An ignored return value on a path where failure is plausible.
- Catch-all that swallows and continues with half-updated state.
- An error turned into a default value ("0", empty) indistinguishable from
  success.
- Recovery that restarts the work but leaks the previous attempt's resources.
- The same failure logged at three layers, or at none.
- An assert as the only guard of an integrity-critical invariant.

## Evidence required

The fallible call, its failure mode (with the condition that triggers it) and
the path that mishandles it, cited to the point where state or resources go
wrong.

## Verification

Fault injection (a failing allocation, socket, file, or dependency stub) in a
throwaway worktree; tests that assert on error codes; reading the logs a failing
test emits.
