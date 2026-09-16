# Logging Policy: Level Semantics and Structured Message Format

Building on ADR-0027 (spdlog, console-only, level tied to build type), this
defines what each level is for and how a log line is written, for both
client and server. `TRACE` is the highest-frequency tier: per-tick ECS
phase transitions (SimulationWorld/PredictionWorld/PresentationWorld) and
per-packet network I/O. `DEBUG` is for internal decisions worth seeing
while diagnosing a problem but too noisy for routine reading. `INFO` is
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
ADR-0027 did: `[%Y-%m-%d %H:%M:%S.%e] [%l] %v`.

No level is a safe place for credentials, session/auth tokens, or player
IPs - this applies unconditionally, including `TRACE`. It matters most
for `augusta::networking`, which is taking on GameNetworkingSockets
(Steam) session tickets and peer addresses.

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
