# application

`augusta_application`. Shared: linked by both executables.

## Responsibility

The application boundary each executable's `main` runs behind: its phases run in
order and end in one classified Outcome, the terminal log event and the exit
status. On Windows it raises the system timer resolution to 1 ms for as long as
the phases run, so both executables' threads wake on time.

It also decides, before either executable reads its command line, whether
mimalloc serves the process (`CheckMimalloc`): from the facts `main` read of
mimalloc (redirected, a fresh block in its heap, its version), it answers yes,
or why not: malloc not redirected, or the block outside mimalloc's heap.
`DescribeMimallocError` words that reason as the one stderr line `main` writes.
It links no allocator itself, so its tests keep the platform's.

On Windows, `augustac` asks first whether the MSVC runtime it loaded is recent
enough (`CheckMsvcRuntime`): from the file version `main` read of its
`msvcp140.dll`, or that it could not be read, and the minimum `versions.env`
pins (the redistributable of the sysroot's toolset), it answers yes, or why not:
unreadable, or older. `DescribeMsvcRuntimeError` words that as the one stderr
line, naming the Visual C++ Redistributable to install.

## Not its responsibility

- What each phase does and how its failures are classified: the executables.
- The worker threads and the runtime's first cause: supervisor.
- The error model: failure.
- Reading the command line and config file: config, before the application
  boundary's phases start.
- Linking mimalloc and reading its facts: each executable's `main`, the only
  code that calls mimalloc (ADR-0078).
- Reading the loaded `msvcp140.dll`'s version and the pinned minimum:
  `augustac`'s `main` and its build (ADR-0079).

## Decisions

[ADR-0005](../../../docs/adr/0005-threading-model.md),
[ADR-0033](../../../docs/adr/0033-error-handling.md),
[ADR-0078](../../../docs/adr/0078-mimalloc-replaces-the-platform-malloc.md),
[ADR-0079](../../../docs/adr/0079-windows-builds-against-a-pinned-xwin-sysroot.md).
