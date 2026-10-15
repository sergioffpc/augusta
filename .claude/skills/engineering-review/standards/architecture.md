# Architecture standard

**Objective:** module boundaries, dependency direction and abstractions match
the design the project states, and domain concepts mean one thing across
modules. Also covers cross-module integration.

## Questions

- What does each module own (state, decisions, resources), and does any other
  module change that state directly?
- Does every dependency point the way the stated architecture allows? Which
  edges does no document justify?
- Where a boundary is enforced (build targets, link visibility, boundary tests),
  what does the enforcement actually check, and what can slip past it?
- Does each abstraction have more than one real user, or hide a real decision?
  Which abstractions are pass-through layers?
- Is a domain concept (a unit, an identifier, a lifecycle state) defined once,
  and do both sides of every boundary interpret it identically?
- Is decision code separate from the mechanism that executes it where the
  project requires it?
- Where two modules must agree (a format writer and reader, a producer and
  consumer), what keeps them in step: a shared definition, a test, nothing?

## Failure patterns

- A dependency cycle, or a low-level module naming a high-level one's types.
- Two sources of truth for one value or rule, drifting apart.
- A boundary enforced by convention only, already crossed somewhere.
- A "shared" module that is a dumping ground with no single reason to change.
- Leaky abstraction: callers depend on the implementation behind it.
- A concept with the same name and different meanings on two sides of a boundary
  (units, inclusive versus exclusive ranges, ownership).
- Writer and reader of one contract with no check that they agree.

## Evidence required

The dependency edge or ownership violation, cited at both ends; the document or
ADR it contradicts. A `DESIGN` finding names the concrete consequence: a defect
class it invites, a change it makes expensive, a requirement it blocks. "Could
be cleaner" is not a consequence.

## Verification

Build-graph and include analysis; the project's boundary tests and what they
glob; grep for a type's uses across modules; comparing a contract's writer and
reader side by side. Record which of these ran.
