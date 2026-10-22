# Client Symbols: a PDB With Each Release

Each release publishes `augustad-linux-x64.debug` beside the server binary
(ADR-0047), so a core dump of any published server can be read. The client gets
the same: each release publishes `augustac-windows-x64.pdb` beside
`augustac-windows-x64.zip`, the PDB of the `augustac.exe` in it, from that same
build. A crash of a released client, or the Aftermath `.nv-gpudmp` the Renderer
writes on a GPU crash/TDR, is read with it. A GPU crash's dumps are the Run's
(GLOSSARY.md), `dumps/augustac-<run-id>-<n>.nv-gpudmp` beside `augustac`, the
n-th from 1, and are pruned with its log (`run_files.h`).

**The build.** The Windows build (`clang-cl` and `lld-link`, ADR-0008) compiles
every target with `/Z7` (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` `Embedded`, set
in the root `CMakeLists.txt` for every configuration): the debug info goes into
each object file, which sccache caches, rather than into one PDB shared between
compiles (`/Zi`), which it cannot. Every target, as on Linux: a crash's frames
are mostly in the module libraries `augustac` links. In Release, `augustac`
links with `/DEBUG:FULL /OPT:REF /OPT:ICF`: `/DEBUG:FULL` has `lld-link` gather
the objects' debug info into `augustac.pdb` beside `augustac.exe`, and
`/OPT:REF` and `/OPT:ICF` are named because `/DEBUG` turns them off otherwise.
The code is the Release build's own: `/Z7` changes no code generation, and the
`.exe` differs only by its debug directory, which names the PDB. The other
executables, `augustad`'s development Windows build and the tests among them,
link no PDB.

**The release.** The release workflow's `client-x64-windows` job stages
`augustac.pdb` from the build it publishes as `augustac-windows-x64.pdb`,
uploads it with the client artifact, and the publish job attaches it to the
GitHub Release. CI's `client-x64-windows` job checks the build leaves it, and
that `augustac.exe`'s debug directory names it. It is not attested, as the
server's `.debug` is not: it is read only beside the `.exe` it matches, in the
attested zip.

**Matching a dump to its PDB.** A PDB reads only the `.exe` of its own link. The
`.exe`'s debug directory holds a CodeView (`RSDS`) record: the PDB's GUID, its
age and the path it was written to. The PDB holds the same GUID and age, and a
debugger loads a PDB only when both match. The `.exe`'s:

```text
dumpbin /headers augustac.exe   # Debug Directories: Format: RSDS, {GUID}, age, path
```

The PDB's (`llvm-pdbutil`, from LLVM):

```text
llvm-pdbutil dump --summary augustac.pdb    # GUID and Age
```

A debugger looks a PDB up by the file name the record names, `augustac.pdb`, so
the downloaded `augustac-windows-x64.pdb` is renamed to that and put on the
symbol path of whatever reads the dump: Visual Studio or WinDbg for a client
crash, Nsight Graphics for a `.nv-gpudmp` from `dumps/`. A dump records the GUID
and age of each module it loaded, so the tag a dump came from is the one whose
PDB matches.

## The Windows Client's Minidump

A crashing Windows client writes its own minidump, so a player needs no registry
key and no Windows Error Reporting setup for there to be a dump. `augustac`
installs the shared crash handler (ADR-0047) as soon as its Run starts, with the
Run ID, the Run's log file and `dumps/augustac-<run-id>.dmp` beside the
executable, named as the Run's other files are (`augusta/run_files.h`). On
Windows the handler is an unhandled-exception filter
(`SetUnhandledExceptionFilter`), which meets an exception no handler took in any
thread. No `SIGSEGV`, `SIGILL` or `SIGFPE` handler is installed beside it: the
CRT would take the main thread's exception into the signal handler first, and
the minidump would have no exception to show. `SIGABRT` keeps its handler, since
`abort()` raises no exception.

On a crash the filter writes
`CRITICAL subsystem=client event=crash exception=0x<code>` (`0xc0000005` for an
access violation), then one
`event=crash_frame index=<n> pc=0x<address> module="<file>" offset=0x<offset>`
line per frame, each ending in the log's context (`run=<id>`, and `session=` and
`match=` while set), to the console and the Run's log, as the Linux client's
lines go (ADR-0047). A frame names its module and the offset into it rather than
a symbol, since the zip carries no PDB: the release's PDB symbolizes it later
(`llvm-symbolizer --obj=augustac.exe` with `0x140000000 + offset`, the image
base `lld-link` gives an `.exe`, or the module's `lm` base plus offset in
WinDbg). The frames are unwound from the exception's context by each module's
unwind tables (`RtlVirtualUnwind`), so the first is the faulting instruction,
not the dispatch of the exception. A test holds both lines to
`logging::FormatLine`, as the signal lines are held.

It then writes the minidump through `MiniDumpWriteDump` (dbghelp), as
`MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory`: the threads'
stacks, the modules' data segments and the memory they point to, which shows the
variables around the crash in a few MB, without the whole process. A user stream
of type `0x41554752` (`AUGR`, `crash::kRunIdStreamType`) holds the Run ID in its
canonical form, so a renamed dump still names its Run. The filter then ends the
process with the exception's code, without Windows Error Reporting. An abort
writes the same lines, with `signal=SIGABRT`, and the same minidump, with no
exception in it.

The dump is opened in WinDbg or Visual Studio with the release's PDB on the
symbol path, as above: `.ecxr` then `k` shows the crashing frame. A death test
in CI's `client-x64-windows` job crashes a child that installed the handler,
then checks the dump exists, its user stream holds the Run ID, and the log holds
the crash line and a frame line.

The minidump is written from inside the crashed process, so a corrupted process
(a broken heap, a held loader lock) may fail to write it, and that is accepted:
the lines are written first. A stack overflow, or a second crash in another
thread or in the report itself, may leave no lines and no dump, as on the
server: the first crash to report ends the process.

## The Linux Client's `.debug`

The Linux client gets the server's own scheme (ADR-0047): each release publishes
`augustac-linux-x64.debug` beside `augustac-linux-x64.tar.gz`. Release builds
compile every target with `-g`; the build links `augustac` with a build ID and
splits its debug info off into `augustac.debug`
(`llvm-objcopy --only-keep-debug`, then `--strip-debug` with a
`.gnu_debuglink`), as `augustad`'s, through the same `augusta_split_debug_info`
(`cmake/AugustaDebugInfo.cmake`). The tarred `augustac` keeps its symbol table.
The release's `client-x64-linux` job publishes the `augustac.debug` of the build
it tars, and the staging check fails unless its build ID is the tarred
`augustac`'s and the `.gnu_debuglink` names it (`scripts/stage-client.sh`,
ADR-0058). Like the PDB, it is not attested.

The Linux client installs the server's crash handler (ADR-0047): a crash logs
its signal and its symbolized stack, read from the symbol table the tarred
`augustac` keeps, to the console and to the Run's log, each line ending in the
Run's `run=`. It raises `RLIMIT_CORE`'s soft limit to the hard limit and dies of
the signal, so the core stays in the player's `systemd-coredump`, and the crash
line names how to get it out, `core="coredumpctl dump augustac"`
(`coredumpctl dump augustac -o core`). The core is read with the `augustac` it
came from and the release's `.debug`, renamed `augustac.debug`, the name the
`.gnu_debuglink` gives, and put beside it, where gdb looks:

```text
gdb augustac core -ex bt
lldb augustac -c core -o bt
```

The core records the build ID of each module it mapped
(`eu-unstrip -n --core core`), so the tag a core came from is the one whose
`.debug` has that build ID (`llvm-readelf --notes augustac.debug`).

## Considered Options

- **`/Zi` for the compile**: rejected. It writes one PDB that every compile of a
  target shares, which sccache does not cache (ADR-0008), and `/FS` serializes
  the writes. `/Z7` gives the linker the same debug info from the objects.
- **A separate `windows` preset for releases**: rejected. The release would then
  build something other than what CI and developers build and test. `/Z7` and
  the PDB cost only link time and disk.
- **A symbol server**: rejected for now. One PDB per release, attached to it, is
  enough for the releases there are; a symbol server can serve the same files
  later.
- **Windows Error Reporting's `LocalDumps`**: rejected. It keeps a full dump
  only under a registry key an administrator sets, which no player will, and
  names it by process and PID rather than by the Run.
- **Crashpad or Breakpad**: rejected, as for the server (ADR-0047): their own
  symbol format and tooling, and Crashpad's out-of-process handler, for what one
  `MiniDumpWriteDump` call from the filter does. Writing from inside the crashed
  process is the accepted cost.
- **`MiniDumpWithFullMemory`**: rejected. The whole process, hundreds of MB with
  the GPU's mapped memory, for a file a player has to send; the data segments
  and the memory the stacks point to show the variables around the crash.
- **Symbolizing the frame lines in the client** (dbghelp's `SymFromAddr`):
  rejected. It needs the PDB, which the zip does not carry, and loads it inside
  a crashed process.

## Consequences

- Every Windows object file is larger, carrying its debug info, and the Release
  link of `augustac` is slower, writing the PDB. sccache caches the objects as
  before.
- The release gains a large asset, the PDB, which no player needs to run the
  client.
- A Windows client's crash leaves its own minidump in `dumps/`, read with the
  release's PDB, in place of a dump Windows Error Reporting may have kept; WER
  is not asked for one. The player sends it with the Run's log: nothing is
  uploaded, and what the dump holds of the player's memory stays with them.
- A Windows crash's frame lines are unsymbolized in the log: the PDB, which no
  player has, names their functions.
- Every Windows executable linking the crash module loads `dbghelp.dll`, a
  system DLL.
