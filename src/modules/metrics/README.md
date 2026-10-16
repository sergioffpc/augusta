# metrics

`augusta_metrics`. Server-only: the client never links it.

## Responsibility

The lock-free counter, gauge and histogram the server's threads write in place
where an event happens, and their Prometheus form. It knows nothing of what is
counted: whoever counts builds on it.

## Not its responsibility

- What the Host counts: host (`augusta/host_metrics.h`).
- Each client's Connection health: `src/server` (Connection health collector).
- Serving `/metrics` and `/livez`: `src/server` (the metrics endpoint).

## Decisions

[ADR-0049](../../../docs/adr/0049-metrics-and-liveness.md).
