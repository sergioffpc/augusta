# Server Crash Reports: Kernel Core Dumps, a Logged Stack, and Split Debug Info

A crash of `augustad` leaves three things: a log of the fatal signal and the
crashing thread's symbolized stack, a kernel core dump, and, kept for every
published build, the debug info that reads that dump.

**The log.** `augustad` installs a handler for the fatal signals (`SIGSEGV`,
`SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`) first thing in `main`
(`src/server/crash.h`). It writes one
`CRITICAL subsystem=server event=crash signal=<name>` line, then one
`event=crash_frame index=<n> pc=0x<address> symbol="<function>"` line per frame,
in the console pattern of ADR-0029, to stdout. It runs in a signal handler, so
it writes past `augusta::logging`'s sink (which allocates and locks) with
`write(2)` into a fixed buffer, and formats the lines itself; a test holds them
to `logging::FormatLine`. It captures the stack with glibc's `backtrace`, which
unwinds by the binary's unwind tables rather than frame pointers, and symbolizes
with Abseil's `absl::Symbolize`, which reads the binary's symbol table without
allocating. It then puts every fatal signal back to its default action and
raises the signal again, so the process dies of it as it would have, and the
kernel dumps its core. That needs `augustad` not to be the container's PID 1,
whose signals to itself the kernel drops, so the image runs it under `tini`,
which forwards it `SIGTERM` and exits with its status. A crash in another
thread, or in the report itself, meets the default action and ends the process
at once. It also raises `RLIMIT_CORE`'s soft limit to the hard limit, since a
limit of 0 writes no core. The log names functions; file and line come from the
core.

**The core.** Where the kernel writes a core is the node's
`kernel.core_pattern`, which no container can set. The k3s node must run
`systemd-coredump` as that handler: the kernel pipes a container's core to it in
the host's namespaces, it stores it compressed under `/var/lib/systemd/coredump`
within its size limits, and `coredumpctl` lists and extracts it. The chart needs
no volume for it.

**The debug info.** Linux Release builds compile every target with `-g`, since a
core's frames are mostly in the module libraries `augustad` links. The build
splits the debug info off `augustad` into `augustad.debug`
(`objcopy --only-keep-debug`, then `--strip-debug` with a `.gnu_debuglink`),
keeping the symbol table the handler reads, and links it with a build ID, which
is what ties a core to its binary and debug info. Debug info reads only the
binary of its own build, so each published binary has its own:

- The server image `sha-<12>`, which is what the cluster runs, including a
  release's (its tag's commit's image): CI publishes the debug info of the same
  Docker build as `sha-<12>-debuginfo` (the Dockerfile's `debuginfo` stage, a
  single file at `/augustad.debug`), attested like the image, kept as long as
  it.
- The release's `augustad-linux-x64`, built by the release workflow and not in
  any image: the release attaches `augustad-linux-x64.debug` from that same
  build. It does not read a cluster core.

A core is read with its binary, that binary's debug info and the core:
`gdb -e augustad -s augustad.debug -c core`, the binary taken from the image the
pod ran.

## Considered Options

- **Breakpad or Crashpad minidumps**: rejected. They write a small minidump from
  the process and need their own symbol format (`dump_syms`) and tooling to read
  it, and Crashpad an out-of-process handler beside the server in the container.
  The kernel already writes a complete core for nothing, and gdb reads it with
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
- On Windows (a development build only) the same handler runs through the CRT's
  `signal`, symbolizing through the PDB; no core is written there.
