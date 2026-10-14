# Correctness standard

**Objective:** the code does what its contract says for every input and state it
can receive. Language-independent; language specifics are in their own standard
(for example [cpp.md](cpp.md)).

## Questions

- What is the function's contract (preconditions, postconditions, units, ranges,
  what an empty or absent value means)? Where is it written, and do all callers
  meet it?
- What invariants does a type or module maintain? Which operations can leave
  them broken, even transiently, and who can observe that state?
- For each state machine: are all states and transitions explicit? What happens
  on an unexpected event in each state, on a repeated event, on an event after
  terminal state?
- Edge cases: zero, one, maximum, empty, overflow, wraparound, negative,
  non-finite floats, duplicate identifiers, out-of-order input.
- Does an algorithm's correctness depend on an unstated property of its input
  (sorted, unique, bounded, non-empty)? Who guarantees it?
- Are naming and types honest: does a name promise more or less than the code
  does? Does a type admit values the domain forbids?
- Who owns each resource, for how long, and who may observe it after release?

## Failure patterns

- Off-by-one at a boundary; inclusive versus exclusive confusion.
- Counter or sequence wraparound compared with plain `<`.
- A state reachable by an event order the author did not consider.
- A precondition checked by one caller and not another.
- Silent clamping or truncation that hides an error.
- Float comparisons or accumulations that break an invariant over time.
- An `else` or `default` branch that treats an unknown value as a known one.

## Evidence required

The input or state sequence that breaks the contract, and the path that admits
it, with every guard on that path inspected. For a `BUG`, the full trace or a
reproduction; otherwise it is a `RISK`.

## Verification

Hand trace with concrete values; an existing test extended in a throwaway
worktree; a property-based test over the input domain; a debug build with
assertions enabled.
