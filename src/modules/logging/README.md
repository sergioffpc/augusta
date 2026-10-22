# logging

`augusta_logging`. Shared: linked by both the client and the server.

## Responsibility

Writes the process's log lines: one process-wide, thread-safe console sink, a
file sink where the process adds one, and a context of `run=`, `session=` and
`match=` fields appended to every line once set, which a crash handler can also
read without a lock, with macros that compile away below
`AUGUSTA_LOG_ACTIVE_LEVEL`. Boost.Log does the work, hidden behind the module.

## Not its responsibility

- When to log and at which level: the logging policy, applied by each caller.
- Which file a process logs to, and which context it sets: the client, which
  alone has a Run log and a context; the server has neither.
- Log shipping: collecting the console is the platform's.

## Decisions

[ADR-0027](../../../docs/adr/0027-logging.md),
[ADR-0029](../../../docs/adr/0029-logging-policy.md),
[ADR-0036](../../../docs/adr/0036-boost-log.md).
