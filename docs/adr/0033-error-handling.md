# Error Handling: std::expected, Exceptions, and Assertions by Layer

Recoverable failures in hot-path/tick logic (physics, networking,
reconciliation, serialization) return `std::expected<T, ErrorCode>` rather than
throwing — C++23 (ADR-0011) already makes it available, and avoiding
stack-unwinding cost matters on a fixed-rate tick loop. Failures during one-shot
startup work (config parsing, asset/pack loading at boot) may throw exceptions
instead, since that code isn't on the tick path and ergonomics matter more than
allocation-free unwinding. Violated internal invariants (bugs, not external
failures) use `assert`/abort in debug builds; in release, they escalate to a
`CRIT` log line (ADR-0029) followed by controlled shutdown, never silent
continuation.

An error is always a type, never a string: a function that reports failure
returns a dedicated error type (an `enum class`, or a struct or class when the
failure also needs context such as the offending key or file), never a
`std::string` or `const char*` message. Callers branch on the type's values,
tests assert on them, and wording lives in one `Describe…` function per module,
so a message change never breaks a caller. This applies to every
`std::expected<T, E>`; `E` is not a string.

An operational failure, one that decides what the Client or Server keeps
running, is described by the shared model in `augusta::failure`: a stable
`Code`, structured context, and the dependency's message as detail. Each `Code`
is classified centrally, in one place, into one `Disposition`, the scope it is
recovered at: `peer` (one message is dropped), `session` (one Session ends),
`subsystem` (an optional, non-authoritative subsystem stops or degrades
observably), `runtime` (the runtime stops and joins its workers before releasing
anything they use) or `process` (the process exits with a failure status). The
detail is for diagnosis only: recovery branches on the `Code`, never on text,
and the detail is never sent to a peer. A dependency's exception goes no further
than the call that converts it into a `Failure` (`failure::Guard`), and the
boundary that owns the disposition's scope writes the one `ERR` or `CRIT` line
for it (ADR-0029). Module error types predating the model stay until each domain
moves onto it; a module may keep its own type for outcomes that are not
operational failures (a malformed recording, a missing config key) and map it to
a `Code` at the boundary.

Runtime-boundary tests make dependencies fail through controlled fault injection
(`failure::Faults`): a runtime asks it at each named site (dependency
initialization, listener setup, worker creation and execution, transport send
and receive, recording write and flush, metrics endpoint acceptance) and fails
the way that dependency does when a test has armed the site. Nothing arms a site
outside a test, and an unarmed site costs one relaxed atomic load.

## Considered Options

- **Exceptions everywhere**: rejected — throwing on the simulation tick's hot
  path (ballistics, hit detection) risks unpredictable unwinding cost on a
  fixed-rate loop; ADR-0013 already benchmarks this code, and exceptions would
  need to be excluded from those measurements anyway.
- **`std::expected`/error codes everywhere, including boot-time code**: rejected
  as unnecessary discipline — boot-time failures aren't latency-sensitive, and
  propagating `std::expected` through one-shot initialization code adds ceremony
  without a payoff.
