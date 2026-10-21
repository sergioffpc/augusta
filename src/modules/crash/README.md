# crash

`augusta_crash`. Shared: linked by both the client and the server.

## Responsibility

Leaves an executable's crash in its log: on a fatal signal it writes the signal
and the crashing thread's symbolized stack as CRITICAL lines naming the
executable's subsystem, in the log's own format, then lets the process die of
that signal so the kernel still dumps its core. Given a Run, the client's, its
lines end in the Run's `run=` and also reach the Run's log file, through a
descriptor opened when it is installed, and the crash line says where the core
is. It runs inside the signal handler, so it formats and writes its lines
itself, allocating nothing.

## Not its responsibility

- Installing it first thing, with the executable's subsystem and, where it has
  them, its Run ID, log file and core's location: each executable's entry point.
- Every other log line: logging, whose sink the handler writes past.
- Where the core goes and the debug info that reads it: the node's or the
  player's `systemd-coredump` and the build (ADR-0047, ADR-0055).

## Decisions

[ADR-0047](../../../docs/adr/0047-server-crash-reports.md),
[ADR-0055](../../../docs/adr/0055-client-release-symbols.md),
[ADR-0029](../../../docs/adr/0029-logging-policy.md).
