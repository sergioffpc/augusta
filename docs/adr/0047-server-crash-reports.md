# Server Crash Reports: Kernel Core Dumps, a Logged Stack, and Split Debug Info

A crash of `augustad` leaves three things: a log of the fatal signal and the
crashing thread's symbolized stack, a kernel core dump, and, kept for every
published build, the debug info that reads that dump.

**The log.** `augustad` installs a handler for the fatal signals (`SIGSEGV`,
`SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`) first thing in `main`. The handler is
the crash module's (`augusta_crash`, `augusta/crash.h`), shared between client
and server so the two never drift in format or in how they unwind; each
executable installs it with its own subsystem, and with the file its minidump
goes to and its Run ID where it has them, which the server does not. It writes
one `CRITICAL subsystem=<subsystem> event=crash signal=<name>` line
(`subsystem=server` for `augustad`), then one
`event=crash_frame index=<n> pc=0x<address> symbol="<function>"` line per frame,
in the console pattern of ADR-0029, to stdout. It runs in a signal handler, so
it writes past `augusta::logging`'s sink (which allocates and locks) with
`write(2)` into a fixed buffer, and formats the lines itself; a test holds them
to `logging::FormatLine`. It captures the stack with `_Unwind_Backtrace`, from
the libunwind linked into the binary (ADR-0008), which unwinds by the binary's
unwind tables rather than frame pointers, and symbolizes with Abseil's
`absl::Symbolize`, which reads the binary's symbol table without allocating. It
then puts every fatal signal back to its default action and raises the signal
again, so the process dies of it as it would have, and the kernel dumps its
core. That needs `augustad` not to be the container's PID 1, whose signals to
itself the kernel drops, so the image runs it under `tini`, which forwards it
`SIGTERM` and exits with its status. A crash in another thread, or in the report
itself, meets the default action and ends the process at once. It also raises
`RLIMIT_CORE`'s soft limit to the hard limit, since a limit of 0 writes no core.
The log names functions; file and line come from the core.

**The client's lines.** `augustac` installs the same handler as soon as its Run
starts (ADR-0027), with its Run ID and the Run's log file. Its crash lines then
end in the log's context, as every other line does (ADR-0029): `run=<id>`, and
`session=` and `match=` while they are set. The handler cannot take the
context's lock, so logging keeps a copy of it for the handler in a fixed buffer
of atomics, written under a sequence number (`logging::CopyContext`): a read
that a change overlaps is read again, and one that keeps being overlapped, as
when the crash interrupted the change itself, ends in no context. The lines
reach the Run's log file as well as stdout, through a descriptor `Install` opens
ahead of any crash with `O_APPEND`, since the handler cannot open one. On Linux
its crash line also says where the core is, `core="coredumpctl dump augustac"`:
the player's `systemd-coredump` keeps it (ADR-0055). The Run's log therefore
ends in the crash's lines, which is how the next launch knows the Run crashed.

**The core.** Where the kernel writes a core is the node's
`kernel.core_pattern`, which no container can set. The k3s node must run
`systemd-coredump` as that handler: the kernel pipes a container's core to it in
the host's namespaces, it stores it compressed under `/var/lib/systemd/coredump`
within its size limits, and `coredumpctl` lists and extracts it. The chart needs
no volume for it.

**The debug info.** Linux Release builds compile every target with `-g`, since a
core's frames are mostly in the module libraries `augustad` links. The build
splits the debug info off `augustad` into `augustad.debug`
(`llvm-objcopy --only-keep-debug`, then `--strip-debug` with a
`.gnu_debuglink`), keeping the symbol table the handler reads, and links it with
a build ID, which is what ties a core to its binary and debug info. Debug info
reads only the binary of its own build, so each published binary has its own:

- The server image `sha-<12>`, which is what the cluster runs, including a
  release's (its tag's commit's image): CI publishes the debug info of the same
  Docker build as `sha-<12>-debuginfo` (the Dockerfile's `debuginfo` stage, a
  single file at `/augustad.debug`), attested like the image, kept as long as
  it.
- The release's `augustad-linux-x64` and `augustad-linux-arm64`, built by the
  release workflow and in no image: the release attaches each one's
  `augustad-linux-<arch>.debug` from that same build. It does not read a cluster
  core.

A core is read with its binary, that binary's debug info and the core:
`lldb augustad -c core -o 'target symbols add augustad.debug'`, the binary taken
from the image the pod ran.

## Considered Options

- **Breakpad or Crashpad minidumps**: rejected. They write a small minidump from
  the process and need their own symbol format (`dump_syms`) and tooling to read
  it, and Crashpad an out-of-process handler beside the server in the container.
  The kernel already writes a complete core for nothing, and lldb reads it with
  the ELF debug info as built.
- **Cores to a `hostPath` volume** (a plain-path `core_pattern` the kernel
  resolves in the container's mount namespace): rejected. The pattern still has
  to be set on the node, it applies to every process there, the directory must
  be writable by the image's unprivileged user, and nothing bounds its size.
  `systemd-coredump` has none of these problems.
- **`std::stacktrace` (C++23) or Boost.Stacktrace for the logged stack**:
  rejected. Both allocate and take locks when they symbolize, which in a signal
  handler can deadlock a process that crashed inside `malloc` - and a hung
  server writes no core. Abseil is already in the dependency tree (protobuf,
  under GameNetworkingSockets).
- **Abseil's own failure signal handler**: rejected. It always writes its own
  format to stderr first, which is not ADR-0029's, and its x86 unwinder follows
  frame pointers, which the Release build omits.
- **Keeping the debug info in the image**: rejected. It would make every image
  the node pulls several times larger for a file read only after a crash.

## Consequences

- The k3s node must have `systemd-coredump` installed and owning
  `kernel.core_pattern`, in place of Ubuntu's default, apport, which is not made
  to keep a container's core. That is node setup, outside the repository.
- Only the thread that installs the handler could have an alternate signal
  stack, so none does: a stack overflow cannot run the handler, and leaves the
  core without the logged stack.
- The image has no shell or debugger (ADR-0054): a core is read off the node,
  never inside the pod, and a live server is inspected with `kubectl debug`.
- On Windows the same module installs an unhandled-exception filter in place of
  the `SIGSEGV`, `SIGILL` and `SIGFPE` handlers, and keeps `SIGABRT`'s. An
  exception's crash line names its code, `event=crash exception=0x<code>`, in
  place of `signal=<name>` (an abort's still names `signal=SIGABRT`); its frame
  lines name a module and an offset, and the client's crash writes a minidump
  (ADR-0055). The development-only Windows server build is given no minidump
  file, so it writes neither a minidump nor a core.
- The client's release binary gets its counterpart of the release's
  `augustad-linux-x64.debug`, a PDB of the same build, in ADR-0055.
