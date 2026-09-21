# Logging Policy: Level Semantics and Structured Message Format

Building on ADR-0027 (console-only, level tied to build type), this
defines what each level is for and how a log line is written, for both
client and server. `TRACE` is the highest-frequency tier: per-packet
network I/O, and per-tick lines that say something (a decision, a change),
never a marker that a phase ran. `DEBUG` is for internal decisions worth
seeing while diagnosing a problem but too noisy for routine reading, and
carries the once-a-second heartbeat below. `INFO` is
for what matters to someone tailing stdout in production: process/loop
startup and shutdown, connection state changes - never per-packet
traffic. `WARN` is an anomalous but recoverable condition needing no
intervention. `ERR` is an operation that failed and was aborted, with the
process continuing. `CRIT` is a state the process cannot safely continue
from.

Message bodies are logfmt-style: lowercase, short, `key=value` pairs, no
sentences. Every line starts with `subsystem=<name>` (e.g.
`simulationworld`, `serverruntime`, `networking`); `event=` is the common
second key, and callers add whatever else is relevant (`tick=`, `bytes=`,
...). This is convention, not an enforced schema. The single
process-wide `"augusta"` logger (ADR-0027) is kept rather than one logger
per module, so the console pattern is fixed here rather than left open as
ADR-0027 did: `<UTC ISO-8601 time> <LEVEL> <message>`, e.g.
`2024-02-01T12:00:00Z INFO subsystem=client event=starting` (see
`augusta::logging::FormatLine`).

No level is a safe place for credentials or session/auth tokens - this
applies unconditionally, including `TRACE`. Peer IPs are fine to log
(direct-IP connections, no matchmaking/relay to anonymize per
ARCHITECTURE.md §3, so the server already sees them) - it matters most
for `augusta::networking`, which is taking on GameNetworkingSockets
(Steam) session tickets.

## Volume

A line written every tick or every packet is read by nobody and, on the console
sink, delays the thread that writes it. Two rules keep the volume down:

- **Heartbeat.** The server's Simulation thread and the client's Prediction
  thread each write one `DEBUG` line a second, `event=heartbeat`, with counters
  for that second (ticks, messages, drops, corrections). The trend is in that
  line; per-phase timing is the profiler's job (the NVTX ranges), not the log's.
- **Peer-provoked warnings are limited.** A `WARN` a peer can cause as often as it
  likes (a malformed or out-of-turn message) goes through `LW_LIMITED` and a
  `logging::Throttle`: one line a second, ending `suppressed=<n>` when it stands
  for more. The heartbeat still counts every one.

## Consequences

- Existing call sites across `client/main.cpp`, `client/runtime.cpp`,
  `server/main.cpp`, `server/runtime.cpp`, and the
  Simulation/Prediction/Presentation world pipelines move from
  sentence-style (`"SimulationWorld: Commit phase"`) to logfmt
  (`subsystem=simulationworld event=commit`) in the same change that adds
  this ADR - a policy the repo doesn't yet follow on the day it's written
  isn't worth writing.
- Per-packet receipt logging in `ServerRuntime`/`ClientRuntime` moves from
  `INFO` to `TRACE`, so it no longer survives Release builds by default.
- ADR-0027's open question about a parseable console pattern is resolved
  by the fixed pattern above.
