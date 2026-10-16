# host

`augusta_host`, `augusta_host_replay`, `augusta_host_match`,
`augusta_host_commands`, `augusta_host_misbehaviour`, `augusta_host_admission`,
`augusta_host_heartbeat`. Server-only: the client never links them.

## Responsibility

Where every client connects on the server: the Host is its socket and
SimulationWorld without threads or a clock, so a test drives a Match by hand,
one network round and one tick at a time — the counterpart of the client's
Harness. It holds the bookkeeping kept around a Match (who is in it and whether
a join is admitted, the Command intake and its sanity gate, each peer's
misbehaviour count, the admission deadline, the heartbeat counts), the server's
edge with the Networking Protocol (`augusta/host_wire.h`) and with
SimulationWorld, what it counts for the metrics endpoint, the Match capture it
writes and its retention, and the Replay server, which serves a recorded Match
through the Host's own pieces.

## Not its responsibility

- Threads, the clock, the metrics endpoint and the lifecycle around a Host:
  `src/server` (ServerRuntime).
- What it is handed at startup — the scenario, Parameters and Game policy:
  scenario.
- The lock-free counters it counts with: metrics.
- Reading and writing the capture file format: capture.
- The conversions both peers share: wire.

## Decisions

[ADR-0038](../../../docs/adr/0038-networking-protocol-messages.md),
[ADR-0049](../../../docs/adr/0049-metrics-and-liveness.md),
[ADR-0050](../../../docs/adr/0050-match-capture-and-reenactment.md),
[ADR-0051](../../../docs/adr/0051-replay.md),
[ADR-0060](../../../docs/adr/0060-executables-are-glue.md).
