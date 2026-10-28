# hardware

`augusta_hardware`. Shared: linked by both the client and the server.

## Responsibility

The machine a process runs on, as the OS reports it: its operating system, CPU
model and thread count, physical memory, the CPU and memory limits its cgroup
sets, as a Kubernetes pod's do, and its GPUs with their drivers' versions,
described as logfmt fields that each Runtime logs once as it starts, so a log
sent with a bug report says what the bug ran on, even before a Renderer starts.
The line's `allocator` field names what serves malloc: `mimalloc/<version>` from
the version the executable read of it, or `platform` in a sanitizer build, which
runs none.

## Not its responsibility

- Which GPU the Renderer draws with: renderer, which chooses it.
- The display and whether the mouse moves raw: window.
- The audio output device: audio, which opens it.
- Writing the line: the client and server Runtimes, through logging.
- Reading mimalloc's version: the executable's `main`, the only code that calls
  mimalloc (ADR-0078).

## Decisions

[ADR-0029](../../../docs/adr/0029-logging-policy.md) (the line's format),
[ADR-0078](../../../docs/adr/0078-mimalloc-replaces-the-platform-malloc.md) (the
allocator field).
