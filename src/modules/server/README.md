# server

`augusta_server`, `augusta_server_application`, `augusta_server_metrics`,
`augusta_server_liveness`, `augusta_server_connection_health`,
`augusta_server_crash`, all in `augusta::server`. Server-only: the client never
links it.

## Responsibility

The server Runtime, augustad's: it runs a Host, or in replay mode a Replay
server, on the Network I/O and Simulation threads at the fixed tick rate, with
its config and its clock. With it, what the Runtime is made from and decides at
its edges:

- How augustad's phases classify their failures behind the application boundary.
- The metrics endpoint serving `/metrics` and `/livez`, and whether the tick
  loop is still running, which `/livez` answers.
- Each client's Connection health as metrics.
- What a crash leaves in the log: the fatal signal and a symbolized stack.

## Not its responsibility

- The entry point, the command line, the signal handlers and the Lifecycle it
  runs the Runtime in: augustad (`src/server`).
- Admitting clients, screening their commands, ticking and replicating a Match:
  host, which the Runtime only runs.
- The lock-free counters it counts with: metrics.
- The client's Runtime: client.

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md),
[ADR-0047](../../../docs/adr/0047-server-crash-reports.md),
[ADR-0049](../../../docs/adr/0049-metrics-and-liveness.md),
[ADR-0051](../../../docs/adr/0051-replay.md),
[ADR-0060](../../../docs/adr/0060-executables-are-glue.md).
