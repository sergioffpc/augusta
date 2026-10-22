# Logging: `augusta::logging`, the Console and the Client's Run Log

`augusta::logging` is a module behind fast macros (`LOG_TRACE`/`LOG_DEBUG`/
`LOG_INFO`/`LOG_WARN`/`LOG_ERROR`/`LOG_CRITICAL`), backed by a single
process-wide, thread-safe logger. The library underneath is Boost.Log
(ADR-0036). Both executables write to a colored console sink; the client also
writes every line to a file, and the server writes to no file.

**The server logs only to its console.** It is deployed to k3s (see ADR-0026),
where stdout is captured by the platform and shipped to a Logstash pipeline;
writing to a local file inside the pod would be redundant and wouldn't survive
pod eviction, since k8s gives no guarantee of persistent local disk across
restarts. This follows twelve-factor-style logging: the app treats logs as an
event stream to stdout and leaves capture/routing to the execution environment.

**The client also logs to `logs/augustac-<run-id>.log`** beside its executable,
one file per Run (GLOSSARY.md), named by its Run ID, which the client takes at
launch, before it reads its config, so even a config failure is in the file. A
player's console is captured by nobody and closes with the process, so without
the file a playtest that goes wrong leaves nothing to send. Each line in the
file is the console's line without colors. The file stays on the player's
machine: nothing is uploaded. If the file cannot be opened, the client logs a
warning and carries on with the console alone. The folder is beside the
executable, which ADR-0058 makes writable, not a per-user state folder.

**The client keeps its last 10 Runs.** At startup it deletes the files of every
older Run, its logs in `logs/` and its Crash dumps in `dumps/` (ADR-0055)
together, by Run ID, so a Run counts once whichever of its files are left; a
file not named as a Run's is never touched. A crashed window closes before the
player reads it, so the next launch tells them: when the previous Run's log ends
in a crash line (ADR-0047), the client writes one `WARN` line,
`event=previous_run_crashed`, naming that Run and its files. When a terminal
failure ends a Run, its one terminal event (ADR-0033) also gives the path of the
Run's log. Everything goes to the terminal; there are no dialogs.

Compiled-in log level is tied to build type (`AUGUSTA_LOG_ACTIVE_LEVEL`): Debug
keeps `trace` and above, Release strips everything below `info` at compile time.
Every record is flushed to every sink as written, so nothing logged is lost in a
crash.

## Consequences

- Running the server outside k8s (e.g. a developer running it directly) means
  logs only exist in that terminal for that run — there is no local log file to
  tail afterwards.
- A client folder holds the files of at most 10 Runs; the pruning is the
  client's, at startup, not the logger's.
- The console output format (pattern string) is fixed by ADR-0029, which also
  sets the context fields the client's lines carry.
