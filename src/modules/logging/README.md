# logging

`augusta_logging`. Shared: linked by both the client and the server.

## Responsibility

Writes the process's log lines: one process-wide, thread-safe console sink, with
macros that compile away below `AUGUSTA_LOG_ACTIVE_LEVEL`. Boost.Log does the
work, hidden behind the module.

## Not its responsibility

- When to log and at which level: the logging policy, applied by each caller.
- File sinks and log shipping: no file sink; collecting the console is the
  platform's.

## Decisions

[ADR-0029](../../../docs/adr/0029-logging-policy.md),
[ADR-0036](../../../docs/adr/0036-boost-log.md).
