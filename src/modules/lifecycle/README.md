# lifecycle

`augusta_lifecycle`. Shared: linked by both the client and the server.

## Responsibility

How an executable starts, runs and ends: the application boundary each `main`
runs behind, which ends in one classified Outcome, the terminal log event and
the exit status; and the runtime supervisor that owns the worker threads'
lifetimes, the stop request every loop watches and the runtime's first cause.

## Not its responsibility

- What each phase does and how its failures are classified: the executables.
- The error model: failure.
- Reading the command line and config file: config, before the lifecycle starts.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md).
