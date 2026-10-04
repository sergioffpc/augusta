# Coding Standards for Generated C++ Code

Judgment calls for C++ code written by agents in this repo, beyond what tooling
already checks. Complements [ADR-0012](../adr/0012-coding-style.md) (Google C++
Style Guide, enforced mechanically by `clang-format`/`clang-tidy`) — don't repeat
anything `clang-tidy` already catches (naming case, brace style, function line
count, etc.).

## Baseline

Everything not listed below follows Claude Code's own general defaults: avoid
premature abstraction, comment only the non-obvious *why*, don't add error
handling for scenarios that can't happen, no compatibility shims. This doc records
only where Augusta diverges from or sharpens those defaults.

## Single responsibility

A function does one nameable operation — if describing it needs "and" ("validates
the input **and** updates state **and** logs the result"), split it. A class has
one reason to change (e.g. `physics::World` shouldn't also serialize network
state). This is independent of the 60-line `clang-tidy` threshold: a function can
violate either without violating the other.

## Simplicity (KISS)

Prefer the simplest design that satisfies the current requirement. Extra
complexity — an abstraction layer, a template, a configurability hook — needs a
concrete justification, not just "might be useful later." A single `if` beats a
strategy pattern when there are only ever two cases.

## Duplication (DRY)

Tolerate duplication over the wrong abstraction: three similar blocks of code are
better than a shared helper that doesn't actually capture a single concept. There
is no fixed occurrence count that forces extraction — extract when the shared
concept is clear, not on a schedule.

## Decision vs. mechanism

Within C++, keep the code that *decides* what to do separate from the code that
*executes* it, even inside the same module — e.g. in the renderer, "which resource
to bind given this state" (decision) is a different function from "issue the
D3D12 call" (execution); in networking, the reconciliation strategy (decision) is
separate from the transport send/receive (mechanism). Extract the decision into
its own function so it stays unit-testable even when the surrounding code isn't
(see Testing below).

This is a C++-internal pattern, distinct from **Game policy** (Lua, see
[CONTEXT.md](../../CONTEXT.md)) — don't call C++ decision code "policy"; that term
is reserved for the Lua-implemented gameplay rules.

## Comments

Every header opens with a module comment just above its namespace, starting
`/// \file` so Doxygen takes it as the file's description (ADR-0046): what
the module is for and where it fits (who uses it, what it depends on), what it
leaves to others and who does it, its threads or ownership where they matter,
and the ADR that decides it. It does not list what the header declares.

A symbol a header declares publicly gets a `///` comment, which Doxygen
publishes in the API reference, only when it says
something the name doesn't: units, ranges, what nullopt or an empty value
means, which thread calls it, preconditions, why it exists. A comment that only
restates the name (`/// The translation part of transform.` on `TranslationOf`)
is deleted, not kept. Private members, and the implementation files (`.cpp`),
keep the general default: `//`, and only for the non-obvious *why*.

## Testing

Required (GoogleTest, ADR-0013): logic that is deterministic and not judged by
eye or ear — simulation logic, ECS systems, physics/ballistics math, networking,
serialization, reconciliation. Applies to new code under `src/`, not to
exploratory spikes or prototype branches.

Not required: rendering output and audio output — these are validated visually
and aurally, not through assertions. When decision logic is mixed with
rendering/audio execution in the same module, extract the decision (see above) so
it's covered even though the execution around it isn't.

A non-functional requirement with a measure a test can check (NFR-02,
NFR-03, NFR-05, NFR-06) gets one, through the Harness, ticked by hand rather
than by a clock (ADR-0013).

Reach for a property-based test (RapidCheck) when an invariant holds over a
whole input domain — a serialization round-trip, a value that must never go
negative, a state that must converge — not in place of example-based tests,
which stay the default.

A bug found by the fuzzer or by a shrunk RapidCheck case gets a
regression test in the pull request that fixes it: the minimized input as a
fixture, the case as an example-based test.

Lua gameplay scripts (ADR-0022) are tested inside the real engine, through
`simulation::World`, never through a fake Lua harness (ADR-0013).

## Naming

When a name represents a domain concept, use the canonical term from
[CONTEXT.md](../../CONTEXT.md) (e.g. `Tick`, `Hitbox`, `ADS`), not a synonym the
glossary lists under `_Avoid_`.

## Ownership

Default to unique ownership / value types, matching the ECS's data-oriented
style. Reach for `shared_ptr` only when a lifetime is genuinely shared and
ambiguous — not as a default way to avoid thinking about ownership.

## Error handling

See [ADR-0033](../adr/0033-error-handling.md). Never return an error as a string —
always as a type.

## Enforcement

Everything `clang-format`/`clang-tidy` already check (ADR-0012) is not repeated
here. Everything else in this doc is enforced by agents reading it plus human
code review — no additional CI gate for it yet.
