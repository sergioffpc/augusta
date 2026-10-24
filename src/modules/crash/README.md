# crash

`augusta_crash`. Shared: linked by both the client and the server.

## Responsibility

Leaves an executable's crash in its log: on a fatal signal it writes the signal
and the crashing thread's symbolized stack as CRITICAL lines naming the
executable's subsystem, in the log's own format, then lets the process die of
that signal so the kernel still dumps its core. Its lines end in the log's
context, `run=`, `session=` and `match=` where set, read from logging without a
lock. Given a Run, the client's, they also reach the Run's log file, through a
descriptor opened when it is installed, and the crash line says where the core
is. It runs inside the signal handler, so it formats and writes its lines
itself, allocating nothing. On Windows a crash is an unhandled exception, met by
an unhandled-exception filter: its frame lines name each frame's module and
offset, and, given a minidump file, it writes a minidump of the process holding
the Run ID before the process ends.

## Not its responsibility

- Installing it first thing, with the executable's subsystem and, where it has
  them, its Run ID, log file, minidump file and core's location: each
  executable's entry point. The minidump's name is the client's
  (`src/client/run_files.h`).
- Every other log line: logging, whose sink the handler writes past.
- Where the core goes and the debug info that reads it: the node's or the
  player's `systemd-coredump` and the build (ADR-0047, ADR-0055).

## Decisions

[ADR-0047](../../../docs/adr/0047-server-crash-reports.md),
[ADR-0055](../../../docs/adr/0055-client-release-symbols.md),
[ADR-0029](../../../docs/adr/0029-logging-policy.md).
