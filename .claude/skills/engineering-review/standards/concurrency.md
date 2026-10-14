# Concurrency standard

**Objective:** every piece of shared state has a defined owner and a defined way
to cross threads, and startup, steady state and shutdown are free of races,
deadlocks and lost wake-ups.

## Questions

- Build the **thread map** first: every thread, what it owns, what it reads from
  others and through what (lock, atomic, queue, immutable snapshot). Compare
  with the documented threading model.
- For each piece of shared mutable state: which lock or atomic protects every
  access, including reads, logging and metrics? Which accesses skip it?
- Lock order: what locks can be held together, and is the order the same
  everywhere? Is any callback, virtual call or third-party call made while
  holding a lock? Could it re-enter?
- Atomics: what memory order does each use, and what does the code after the
  load assume was published? Is `relaxed` used where a happens-before edge is
  needed?
- Condition variables: is every wait in a predicate loop? Can the notify happen
  before the wait and be lost?
- Snapshots: is a published value truly immutable after publication? Can a
  reader combine values from two publications?
- Shutdown: how does each thread learn to stop, and is the signal observed on
  every blocking path? Are threads joined before the resources they use are
  destroyed? What happens if stop arrives during startup?
- Failure: when a worker fails, what do the other threads do, and is the first
  failure preserved?
- Signal handlers: do they do only async-signal-safe work?

## Failure patterns

- Check-then-act across two locked sections.
- A getter returning a reference to lock-protected data.
- A callback from a library thread mutating owner-thread state.
- Two locks taken in opposite orders on different paths.
- A blocking call (socket, queue pop, sleep) that never sees the stop request.
- Destructor order releasing a resource before a thread using it is joined.
- `relaxed` flag publication of non-atomic data.

## Evidence required

The two (or more) accesses, cited, with the thread each runs on and the
synchronisation, or its absence, between them. An interleaving that produces the
failure, written step by step. Without a concrete interleaving it is a `RISK`.

## Verification

ThreadSanitizer over tests that exercise the path (it only sees executed
interleavings; a clean run is V2 for those paths, not proof); stress tests with
many iterations and injected delays; a shutdown test started at each phase.
