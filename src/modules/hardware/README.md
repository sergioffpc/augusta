# hardware

`augusta_hardware`. Shared: linked by both the client and the server.

## Responsibility

The machine a process runs on, as the OS reports it: its operating system, CPU
model and thread count, physical memory, the CPU and memory limits its cgroup
sets, as a Kubernetes pod's do, and its GPUs with their drivers' versions,
described as logfmt fields that each Runtime logs once as it starts, so a log
sent with a bug report says what the bug ran on, even before a Renderer starts.

## Not its responsibility

- Which GPU the Renderer draws with: renderer, which chooses it.
- The display and whether the mouse moves raw: window.
- The audio output device: audio, which opens it.
- Writing the line: the client and server Runtimes, through logging.

## Decisions

[ADR-0029](../../../docs/adr/0029-logging-policy.md) (the line's format).
