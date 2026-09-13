# Logging: `augusta::logging`, a Header-Only spdlog Wrapper, Console-Only

`augusta::logging` is a header-only module wrapping spdlog behind fast
macros (`TRACE`/`DEBUG`/`INFO`/`WARN`/`ERR`/`CRIT`), backed by a single
process-wide, thread-safe logger writing only to a colored console sink —
no file sink, no rotation, for either client or server. The server is
deployed to k3s (see ADR-0026), where stdout is captured by the platform
and shipped to a Logstash pipeline; writing to a local file inside the pod
would be redundant and wouldn't survive pod eviction, since k8s gives no
guarantee of persistent local disk across restarts. This follows
twelve-factor-style logging: the app treats logs as an event stream to
stdout and leaves capture/routing to the execution environment.

Compiled-in log level is tied to build type (`SPDLOG_ACTIVE_LEVEL`):
Debug keeps `trace` and above, Release strips everything below `info` at
compile time. `flush_on(warn)` guarantees warnings/errors survive a crash;
lower levels are buffered.

## Consequences

- Running the server outside k8s (e.g. a developer running it directly)
  means logs only exist in that terminal for that run — there is no local
  log file to tail afterwards.
- The console output format (pattern string) is not fixed by this ADR;
  whoever wires up the Logstash pipeline may need a structured/parseable
  pattern, which could mean revisiting the default text pattern later.
