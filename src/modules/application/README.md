# application

`augusta_application`. Shared: linked by both executables.

## Responsibility

The application boundary each executable's `main` runs behind: its phases run in
order and end in one classified Outcome, the terminal log event and the exit
status.

## Not its responsibility

- What each phase does and how its failures are classified: the executables.
- The worker threads and the runtime's first cause: supervisor.
- The error model: failure.
- Reading the command line and config file: config, before the application
  boundary's phases start.

## Decisions

[ADR-0033](../../../docs/adr/0033-error-handling.md).
