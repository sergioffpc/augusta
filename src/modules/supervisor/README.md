# supervisor

`augusta_supervisor`. Shared: linked by both Runtimes and the Harness.

## Responsibility

The runtime supervisor: it owns the worker threads' lifetimes, the stop request
every loop watches and the runtime's first cause, which it hands back to the
application boundary to report.

## Not its responsibility

- The application boundary each `main` runs behind: application.
- What each worker does: the Runtimes and the Harness.
- The error model: failure.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md).
